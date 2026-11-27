#include "lan_transport.h"

#include "lan_mdns.h"

#include <asio/thread_pool.hpp>
#include <atomic>
#include <chrono>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/server.hpp>

#include <minirtc.h>

namespace crossdesk::lan {
namespace {
using Clock = std::chrono::steady_clock;
using Server = websocketpp::server<websocketpp::config::asio_tls>;
using Client = websocketpp::client<websocketpp::config::asio_tls_client>;
using Hdl = websocketpp::connection_hdl;
using Error = websocketpp::lib::error_code;
using Context = websocketpp::lib::asio::ssl::context;
using ResolveResults = asio::ip::tcp::resolver::results_type;

// System resolvers may keep running after cancellation. A lookup owns no
// session state and can only post results while its transport is attached.
struct NameLookup {
  std::mutex mutex;
  std::function<void(Error, ResolveResults)> deliver;
  void Cancel() {
    std::lock_guard lock(mutex);
    deliver = {};
  }
  static bool Start(std::shared_ptr<NameLookup> lookup, std::string name,
                    unsigned port) {
    static asio::thread_pool workers(4);
    static std::atomic<unsigned> pending{0};
    if (pending.fetch_add(1) >= 24) {
      --pending;
      return false;
    }
    asio::post(
        workers, [lookup = std::move(lookup), name = std::move(name), port] {
          {
            std::lock_guard lock(lookup->mutex);
            if (!lookup->deliver) {
              --pending;
              return;
            }
          }
          asio::io_context io;
          asio::ip::tcp::resolver resolver(io);
          Error error;
          auto results =
              resolver.resolve(name, std::to_string(port),
                               asio::ip::tcp::resolver::numeric_service, error);
          --pending;
          std::lock_guard lock(lookup->mutex);
          if (lookup->deliver) lookup->deliver(error, std::move(results));
        });
    return true;
  }
};
constexpr const char* kPath = "/crossdesk/lan/v2";
constexpr const char* kProtocol = "crossdesk.lan.v2";
void ValidateListener(const std::string& address, unsigned port) {
  if (port > 65535) throw std::runtime_error("Invalid LAN port");
  if (address == kAllInterfaces) return;
  const auto interfaces = LocalIpInterfaces();
  if (!IsIpAddress(address) ||
      std::none_of(interfaces.begin(), interfaces.end(),
                   [&](const auto& i) { return i.address == address; }))
    throw std::runtime_error("Selected LAN interface is unavailable");
}
Error ConfigureAcceptor(asio::ip::tcp::acceptor& acceptor, bool automatic_port,
                        bool ipv6, bool dual_stack) {
  Error error;
  if (ipv6) {
    acceptor.set_option(asio::ip::v6_only(!dual_stack), error);
    if (error) return error;
  }
#ifdef _WIN32
  (void)automatic_port;
  acceptor.set_option(asio::socket_base::reuse_address(false), error);
  if (error) return error;
  const BOOL exclusive = TRUE;
  if (::setsockopt(acceptor.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&exclusive),
                   sizeof(exclusive)) == SOCKET_ERROR)
    return Error(WSAGetLastError(), asio::error::get_system_category());
#else
  // Fixed ports may rebind after TIME_WAIT. OS-assigned ports need no reuse;
  // exclusive allocation also avoids wildcard listener overlap on BSD.
  acceptor.set_option(asio::socket_base::reuse_address(!automatic_port), error);
#endif
  return error;
}
void CheckListenError(const Error& error) {
  if (!error) return;
  if (error == asio::error::address_in_use)
    throw std::runtime_error("LanPortInUse");
  if (error == asio::error::access_denied)
    throw std::runtime_error("LanPortPermissionDenied");
  throw std::runtime_error("Cannot listen on LAN port: " + error.message());
}
void CheckExistingEndpoint(const std::string& address, unsigned port) {
#ifdef __APPLE__
  // BSD permits a specific-address listener alongside a wildcard listener with
  // SO_REUSEADDR. Detect that live listener without treating TIME_WAIT or our
  // established sessions as occupied ports. No application data is sent.
  asio::io_context io;
  asio::ip::tcp::socket socket(io);
  asio::steady_timer deadline(io, std::chrono::milliseconds(200));
  Error result;
  bool timed_out = false;
  socket.async_connect(
      {asio::ip::make_address(address), static_cast<unsigned short>(port)},
      [&](const Error& error) {
        result = error;
        deadline.cancel();
      });
  deadline.async_wait([&](const Error& error) {
    if (error) return;
    timed_out = true;
    Error ignored;
    socket.close(ignored);
  });
  io.run();
  if (timed_out) throw std::runtime_error("LanPortCheckFailed");
  if (!result) throw std::runtime_error("LanPortInUse");
  if (result != asio::error::connection_refused) CheckListenError(result);
#else
  (void)address;
  (void)port;
#endif
}
void CheckExistingListener(const std::string& address, unsigned port) {
#ifdef __APPLE__
  if (address == kAllInterfaces || address == "0.0.0.0") {
    // BSD permits wildcard/specific overlap with SO_REUSEADDR. Probe binds on
    // each exact address to detect specific listeners without sending traffic
    // through VPN adapters which may accept connects to otherwise unused ports.
    // The final wildcard bind detects an existing wildcard listener itself.
    asio::io_context io;
    auto addresses = LocalIpInterfaces();
    addresses.push_back({"127.0.0.1"});
    if (address == kAllInterfaces) addresses.push_back({"::1"});
    for (const auto& interface : addresses) {
      const auto ip = asio::ip::make_address(interface.address);
      if (address == "0.0.0.0" && ip.is_v6()) continue;
      asio::ip::tcp::acceptor probe(io);
      Error error;
      probe.open(ip.is_v6() ? asio::ip::tcp::v6() : asio::ip::tcp::v4(), error);
      CheckListenError(error);
      if (ip.is_v6()) probe.set_option(asio::ip::v6_only(true), error);
      CheckListenError(error);
      probe.set_option(asio::socket_base::reuse_address(true), error);
      CheckListenError(error);
      probe.bind({ip, static_cast<unsigned short>(port)}, error);
      CheckListenError(error);
    }
    return;
  }
#endif
  CheckExistingEndpoint(address, port);
}
// Fall back only when the host has no usable IPv6 stack. A busy requested
// port must still be reported, never silently degrade a dual-stack listener.
bool SupportsDualStack() {
  asio::io_context io;
  asio::ip::tcp::acceptor probe(io);
  Error error;
  probe.open(asio::ip::tcp::v6(), error);
  if (!error) probe.set_option(asio::ip::v6_only(false), error);
  if (!error) probe.bind({asio::ip::address_v6::any(), 0}, error);
  if (error == asio::error::address_family_not_supported ||
      error == std::errc::protocol_not_supported ||
      error == asio::error::no_protocol_option ||
      error == asio::error::operation_not_supported ||
      error == std::errc::address_not_available)
    return false;
  CheckListenError(error);
  return true;
}
std::string SocketAddress(asio::ip::address address) {
  if (address.is_v6() && address.to_v6().is_v4_mapped())
    address = asio::ip::make_address_v4(asio::ip::v4_mapped, address.to_v6());
  if (address.is_v4()) return address.to_string();
  auto ipv6 = address.to_v6();
  const auto scope = ipv6.scope_id();
  ipv6.scope_id(0);
  return ipv6.to_string() + (scope ? "%" + std::to_string(scope) : "");
}
std::shared_ptr<Context> TlsContext() {
  auto context = std::make_shared<Context>(Context::tls);
  if (SSL_CTX_set_min_proto_version(context->native_handle(), TLS1_3_VERSION) !=
          1 ||
      SSL_CTX_set_max_early_data(context->native_handle(), 0) != 1)
    throw std::runtime_error("TLS 1.3 is required for LAN connections");
  SSL_CTX_set_options(context->native_handle(), SSL_OP_NO_COMPRESSION);
  SSL_CTX_set_session_cache_mode(context->native_handle(), SSL_SESS_CACHE_OFF);
  return context;
}
}  // namespace
struct Transport::Channel::State {
  std::atomic<bool> closing{false};
  std::mutex budget_mutex;
  size_t sent_count = 0, sent_bytes = 0, received_count = 0, received_bytes = 0;
  bool authenticated = false, opened = false, terminal = false;
  Clock::time_point created = Clock::now(), last_ping = Clock::now(),
                    ping_sent{}, ping_window = Clock::now();
  unsigned inbound_pings = 0;
  std::string pending_ping, error;
  std::function<void(const std::string&)> send, ping;
  std::function<void()> close, graceful_close;
  ChannelPtr logical;
  std::string candidate_address;
  bool selected = false;
};
struct Transport::Impl {
  asio::io_context io;
  asio::executor_work_guard<asio::io_context::executor_type> work{
      io.get_executor()};
  asio::steady_timer timer{io};
  std::thread thread;
  std::atomic<bool> stopping{false};
  std::atomic<uint64_t> next_id{1};
  std::map<uint64_t, std::shared_ptr<Server>> servers;
  std::map<uint64_t, std::shared_ptr<Client>> clients;
  std::map<Hdl, ChannelPtr, std::owner_less<Hdl>> channels;
  static constexpr unsigned kMaxCandidates = 16;
  struct Dial {
    explicit Dial(asio::io_context& io)
        : stagger(io), deadline(io), fallback_timer(io) {}
    std::shared_ptr<NameLookup> lookup = std::make_shared<NameLookup>();
    asio::steady_timer stagger, deadline, fallback_timer;
    std::function<void()> cancel_mdns;
    std::string multicast_host;
    bool mdns_started = false, mdns_pending = false, mdns_found = false;
    std::set<std::string> candidates;
    ChannelPtr channel;
    unsigned port = 0;
    std::string expected_key, error;
    bool resolving = true, local_name = false;
    unsigned attempted = 0;
    long long dns_ms = 0;
    int resolver_error = 0;
    std::deque<std::string> addresses;
    std::map<uint64_t, ChannelPtr> active;
  };
  std::map<uint64_t, std::shared_ptr<Dial>> dials;
  size_t preauth = 0;
  std::set<uint64_t> retired_listeners;
  std::mutex event_mutex;
  std::vector<Event> events;
  Impl() {
    Tick();
    thread = std::thread([this] { io.run(); });
  }
  bool AdmissionBlocked() {
    std::lock_guard lock(event_mutex);
    // Keep closed-but-not-yet-consumed channel events in the admission budget.
    return events.size() >= 64;
  }
  void Emit(Event::Kind kind, const ChannelPtr& c, std::string payload = {}) {
    std::lock_guard lock(event_mutex);
    events.push_back({kind, c, std::move(payload)});
  }
  void Finish(const ChannelPtr& c) {
    if (c->state->terminal) return;
    c->state->closing = true;
    c->state->terminal = true;
    if (c->incoming && !c->state->authenticated) --preauth;
    const auto parent = c->state->logical;
    if (!parent) {
      Emit(Event::Closed, c, c->state->error);
    } else if (c->state->selected) {
      parent->state->error = c->state->error;
      Finish(parent);
    } else if (auto it = dials.find(parent->id); it != dials.end()) {
      auto dial = it->second;
      dial->active.erase(c->id);
      if (ErrorRank(c->state->error) > ErrorRank(dial->error))
        dial->error = c->state->error;
      asio::post(io, [this, dial] { Pump(dial); });
    }
  }
  void Abort(const ChannelPtr& c) {
    if (c->state->terminal) return;
    c->state->closing = true;
    if (c->state->close) c->state->close();
  }
  void Tick() {
    timer.expires_after(std::chrono::seconds(1));
    timer.async_wait([this](const Error& error) {
      if (error || stopping) return;
      const auto now = Clock::now();
      std::vector<ChannelPtr> expired;
      for (const auto& [_, c] : channels) {
        auto& s = *c->state;
        if (s.terminal || s.closing) continue;
        if ((!s.opened && now - s.created >= std::chrono::seconds(8)) ||
            (!s.pending_ping.empty() &&
             now - s.ping_sent >= std::chrono::seconds(15))) {
          s.error = s.opened ? "HeartbeatTimeout" : "ConnectionTimeout";
          expired.push_back(c);
          continue;
        }
        if (s.opened && s.pending_ping.empty() &&
            now - s.last_ping >= std::chrono::seconds(5)) {
          s.pending_ping = RandomHex(8);
          s.last_ping = s.ping_sent = now;
          s.ping(s.pending_ping);
        }
      }
      for (auto& c : expired) Abort(c);
      for (auto it = retired_listeners.begin();
           it != retired_listeners.end();) {
        const auto id = *it;
        if (std::none_of(channels.begin(), channels.end(),
                         [id](const auto& item) {
                           return item.second->listener == id;
                         })) {
          servers.erase(id);
          it = retired_listeners.erase(it);
        } else
          ++it;
      }
      Tick();
    });
  }
  template <class Endpoint>
  void Attach(Endpoint* endpoint, Hdl hdl, const ChannelPtr& c) {
    channels[hdl] = c;
    c->state->send = [this, endpoint, hdl, cweak = std::weak_ptr<Channel>(c)](
                         const std::string& text) {
      Error e;
      endpoint->send(hdl, text, websocketpp::frame::opcode::text, e);
      if (e)
        if (auto c = cweak.lock()) Abort(c);
    };
    c->state->ping = [this, endpoint, hdl, cweak = std::weak_ptr<Channel>(c)](
                         const std::string& nonce) {
      Error e;
      endpoint->ping(hdl, nonce, e);
      if (e)
        if (auto c = cweak.lock()) Abort(c);
    };
    c->state->graceful_close = [this, endpoint, hdl,
                                cweak = std::weak_ptr<Channel>(c)] {
      Error error;
      endpoint->close(hdl, websocketpp::close::status::going_away, "", error);
      if (error)
        if (auto c = cweak.lock()) Abort(c);
    };
    c->state->close = [this, endpoint, hdl, cweak = std::weak_ptr<Channel>(c)] {
      Error e;
      auto connection = endpoint->get_con_from_hdl(hdl, e);
      if (!e && connection) {
        connection->get_raw_socket().cancel(e);
        connection->get_raw_socket().shutdown(
            asio::ip::tcp::socket::shutdown_both, e);
        connection->get_raw_socket().close(e);
      }
      if (auto c = cweak.lock()) Finish(c);
    };
  }
  static int ErrorRank(const std::string& error) {
    if (error.rfind("HostKeyChanged:", 0) == 0) return 4;
    if (error == "InvalidCertificate" || error == "FingerprintMissing")
      return 3;
    if (error == "IncompatibleProtocol" || error == "UnsupportedNetworkPath")
      return 2;
    return error.empty() ? 0 : 1;
  }
  void OpenClient(const ChannelPtr& c, const std::string& address,
                  unsigned port, const std::string& expected_key) {
    try {
      auto client = std::make_shared<Client>();
      Configure(client.get());
      auto context = TlsContext();
      context->set_verify_mode(asio::ssl::verify_peer);
      context->set_verify_callback(
          [c, expected_key](bool, asio::ssl::verify_context& verification) {
            X509_STORE_CTX* store = verification.native_handle();
            auto* certificate = X509_STORE_CTX_get_current_cert(store);
            // Self-signed LAN certificates establish the encrypted channel.
            // Password verification authorizes access at the receiving host;
            // it does not authenticate this certificate's device identity.
            const bool valid = X509_STORE_CTX_get_error_depth(store) == 0 &&
                               VerifyLanCertificate(certificate);
            if (valid && c->state->logical) {
              c->peer_fingerprint = CertificateFingerprint(certificate);
              if (c->peer_fingerprint.empty()) {
                c->state->error = "FingerprintMissing";
                return false;
              }
              if (!expected_key.empty() &&
                  c->peer_fingerprint != expected_key) {
                c->state->error = "HostKeyChanged:" + c->peer_fingerprint;
                return false;
              }
            }
            if (!valid) c->state->error = "InvalidCertificate";
            return valid;
          });
      client->set_tls_init_handler([context](Hdl) { return context; });
      Error error;
      auto connection = client->get_connection(
          "wss://" + FormatEndpoint(address, port) + kPath, error);
      if (error) {
        c->state->error = "ConnectionFailed";
        Finish(c);
        return;
      }
      connection->add_subprotocol(kProtocol);
      Attach(client.get(), connection->get_handle(), c);
      clients[c->id] = client;
      client->connect(connection);
    } catch (...) {
      c->state->error = "ConnectionFailed";
      Abort(c);
      Finish(c);
    }
  }
  void EndDial(std::shared_ptr<Dial> dial, const ChannelPtr& winner = {}) {
    if (!dials.erase(dial->channel->id)) return;
    dial->lookup->Cancel();
    dial->fallback_timer.cancel();
    if (dial->cancel_mdns) dial->cancel_mdns();
    const auto total = std::chrono::duration_cast<std::chrono::milliseconds>(
                           Clock::now() - dial->channel->state->created)
                           .count();
    const std::string family =
        winner
            ? (winner->remote_address.find(':') == std::string::npos ? "IPv4"
                                                                     : "IPv6")
            : "none";
    dial->channel->dial_diagnostic =
        "dns_ms=" + std::to_string(dial->resolving ? total : dial->dns_ms) +
        " resolver_error=" + std::to_string(dial->resolver_error) +
        " candidates=" + std::to_string(dial->candidates.size()) +
        " mdns=" + (dial->mdns_started ? "1" : "0") +
        " attempts=" + std::to_string(dial->attempted) + " selected=" + family +
        " total_ms=" + std::to_string(total);
    dial->stagger.cancel();
    dial->deadline.cancel();
    for (auto& [_, candidate] : dial->active)
      if (candidate != winner) Abort(candidate);
    if (!winner) {
      dial->channel->state->error = dial->error;
      Finish(dial->channel);
    }
  }
  void Pump(const std::shared_ptr<Dial>& dial) {
    if (!dials.count(dial->channel->id)) return;
    if (stopping || dial->channel->state->closing) {
      EndDial(dial);
      return;
    }
    if (dial->attempted >= kMaxCandidates) dial->addresses.clear();
    if (dial->mdns_pending &&
        (HasHandshakeError(dial) || dial->attempted >= kMaxCandidates)) {
      dial->cancel_mdns();
      dial->mdns_pending = false;
    }
    if (!dial->multicast_host.empty() && !dial->mdns_started &&
        ErrorRank(dial->error) <= 1 && !dial->resolving &&
        ((dial->addresses.empty() && dial->active.empty()) ||
         dial->attempted >= kMaxCandidates - 2)) {
      StartMulticast(dial);
    }
    // Reserve the last two attempts for link-local discovery. If it returns
    // nothing, the remaining system candidates can use those slots.
    if (dial->mdns_pending && !dial->mdns_found &&
        dial->attempted >= kMaxCandidates - 2)
      return;
    if (dial->addresses.empty()) {
      if (dial->active.empty() && !dial->resolving && !dial->mdns_pending)
        EndDial(dial);
      return;
    }
    if (dial->active.size() >= 2) return;
    if (channels.size() >= 24 || AdmissionBlocked()) {
      if (dial->active.empty()) {
        dial->error = "Busy";
        EndDial(dial);
      }
      return;
    }
    auto c = std::make_shared<Channel>();
    c->id = next_id++;
    c->state = std::make_shared<Channel::State>();
    c->state->logical = dial->channel;
    c->state->candidate_address = std::move(dial->addresses.front());
    dial->addresses.pop_front();
    dial->active[c->id] = c;
    ++dial->attempted;
    OpenClient(c, c->state->candidate_address, dial->port, dial->expected_key);
    ScheduleNext(dial);
  }
  void ScheduleNext(const std::shared_ptr<Dial>& dial) {
    if (dial->addresses.empty()) return;
    // If both sockets stall, leave time for later addresses before the common
    // deadline.
    auto delay = std::chrono::milliseconds(250);
    if (dial->active.size() == 2) {
      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          dial->deadline.expiry() - Clock::now());
      delay =
          std::clamp(remaining / static_cast<int>(dial->addresses.size() + 1),
                     delay, std::chrono::milliseconds(2000));
    }
    dial->stagger.expires_after(delay);
    dial->stagger.async_wait([this, dial](const Error& e) {
      if (e || dial->addresses.empty() || !dials.count(dial->channel->id))
        return;
      if (dial->active.size() == 2) {
        auto oldest = dial->active.begin()->second;
        Abort(oldest);
      } else {
        Pump(dial);
      }
    });
  }
  void Resolve(const ChannelPtr& c, const DialEndpoint& target,
               const std::string& key) {
    auto dial = std::make_shared<Dial>(io);
    dial->channel = c;
    dial->port = target.port;
    dial->local_name =
        target.host.size() > 6 &&
        target.host.compare(target.host.size() - 6, 6, ".local") == 0;
    if (kUseMulticastFallback && dial->local_name) {
      dial->multicast_host = target.host;
      // Leave room within the same deadline when DNS stalls or returns a
      // black-holed address. A healthy system result wins without any
      // multicast.
      dial->fallback_timer.expires_at(c->state->created +
                                      std::chrono::seconds(4));
      dial->fallback_timer.async_wait([this, dial](Error e) {
        if (!e && dials.count(dial->channel->id)) StartMulticast(dial);
      });
    }
    dial->expected_key = key;
    dials[c->id] = dial;
    c->state->close = [this, weak = std::weak_ptr<Dial>(dial)] {
      if (auto current = weak.lock()) EndDial(current);
    };
    dial->deadline.expires_at(c->state->created + std::chrono::seconds(8));
    dial->deadline.async_wait([this, dial](const Error& e) {
      if (e) return;
      if (ErrorRank(dial->error) <= 1)
        dial->error =
            dial->resolving && dial->candidates.empty()
                ? (dial->local_name ? "LocalNameTimeout" : "DnsTimeout")
                : "DirectPortUnreachable";
      EndDial(dial);
    });
    dial->lookup->deliver = [this, weak = std::weak_ptr<Dial>(dial)](
                                Error e, ResolveResults results) {
      asio::post(io, [this, weak, e, results = std::move(results)] {
        if (auto current = weak.lock();
            current && dials.count(current->channel->id))
          Resolved(current, e, results);
      });
    };
    if (!NameLookup::Start(dial->lookup, target.host + ".", target.port)) {
      dial->error = "Busy";
      EndDial(dial);
    }
  }
  void Resolved(const std::shared_ptr<Dial>& dial, const Error& e,
                const ResolveResults& results) {
    dial->resolving = false;
    dial->resolver_error = e.value();
    dial->dns_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       Clock::now() - dial->channel->state->created)
                       .count();
    std::vector<asio::ip::address> addresses;
    for (const auto& entry : results)
      addresses.push_back(entry.endpoint().address());
    QueueAddresses(dial, addresses, false);
    if (dial->candidates.empty())
      dial->error = e ? (dial->local_name ? "LocalNameFailed" : "DnsFailed")
                      : "DnsNoUsableAddress";
    Pump(dial);
  }
  bool HasHandshakeError(const std::shared_ptr<Dial>& dial) const {
    if (ErrorRank(dial->error) > 1) return true;
    return std::any_of(dial->active.begin(), dial->active.end(),
                       [](const auto& entry) {
                         return ErrorRank(entry.second->state->error) > 1;
                       });
  }
  void StartMulticast(const std::shared_ptr<Dial>& dial) {
    if (dial->mdns_started || HasHandshakeError(dial)) return;
    dial->mdns_started = dial->mdns_pending = true;
    dial->fallback_timer.cancel();
    dial->cancel_mdns = QueryLocalName(
        io, dial->multicast_host,
        [this, weak = std::weak_ptr<Dial>(dial)](
            std::vector<asio::ip::address> addresses, bool complete) {
          auto d = weak.lock();
          if (!d || !dials.count(d->channel->id)) return;
          d->mdns_pending = !complete;
          if (!HasHandshakeError(d) && QueueAddresses(d, addresses, true)) {
            if (d->active.size() == 2) {
              if (d->mdns_found) {
                ScheduleNext(d);
              } else {
                const auto oldest = d->active.begin()->second;
                Abort(oldest);
              }
            }
            d->mdns_found = true;
          }
          Pump(d);
        });
  }
  // Interleave both address families before applying the candidate limit.
  // Newly discovered multicast addresses take priority over unused DNS answers.
  bool QueueAddresses(const std::shared_ptr<Dial>& dial,
                      const std::vector<asio::ip::address>& addresses,
                      bool prioritize) {
    std::deque<std::string> families[2];
    std::set<std::string> seen;
    int family = 0;
    for (const auto& address : addresses) {
      if (address.is_v6() && address.to_v6().is_v4_mapped()) continue;
      auto ip = SocketAddress(address);
      if (!IsIpAddress(ip) || !seen.insert(ip).second) continue;
      if (dial->candidates.count(ip)) {
        if (!prioritize) continue;
        auto pending =
            std::find(dial->addresses.begin(), dial->addresses.end(), ip);
        if (pending == dial->addresses.end()) continue;
        dial->addresses.erase(pending);
      }
      int index = address.is_v6() ? 1 : 0;
      if (families[0].empty() && families[1].empty()) family = index;
      families[index].push_back(std::move(ip));
    }
    std::vector<std::string> queued;
    while ((!families[0].empty() || !families[1].empty()) &&
           queued.size() < kMaxCandidates) {
      auto& queue = families[family];
      if (!queue.empty()) {
        dial->candidates.insert(queue.front());
        queued.push_back(std::move(queue.front()));
        queue.pop_front();
      }
      family = 1 - family;
    }
    dial->addresses.insert(
        prioritize ? dial->addresses.begin() : dial->addresses.end(),
        queued.begin(), queued.end());
    if (!queued.empty() && ErrorRank(dial->error) <= 1)
      dial->error = "DirectPortUnreachable";
    return !queued.empty();
  }
  template <class Endpoint>
  void Configure(Endpoint* endpoint) {
    endpoint->clear_access_channels(websocketpp::log::alevel::all);
    endpoint->clear_error_channels(websocketpp::log::elevel::all);
    endpoint->init_asio(&io);
    endpoint->set_max_message_size(256 * 1024);
    endpoint->set_max_http_body_size(0);
    endpoint->set_open_handshake_timeout(8000);
    endpoint->set_close_handshake_timeout(1000);
    endpoint->set_open_handler([this, endpoint](Hdl hdl) {
      auto it = channels.find(hdl);
      if (it == channels.end()) return;
      auto c = it->second;
      if (c->state->closing) {
        // A WebSocket++ resolver may have completed after the first
        // cancellation.
        if (c->state->close) c->state->close();
        return;
      }
      Error e;
      auto connection = endpoint->get_con_from_hdl(hdl, e);
      if (e) {
        Abort(c);
        return;
      }
      const auto local = connection->get_raw_socket().local_endpoint(e);
      if (e) {
        Abort(c);
        return;
      }
      const auto remote = connection->get_raw_socket().remote_endpoint(e);
      if (e) {
        Abort(c);
        return;
      }
      c->local_address = SocketAddress(local.address());
      c->remote_address = SocketAddress(remote.address());
      const auto interfaces = LocalIpInterfaces();
      if (MiniRtcIsDirectAddressPair(c->local_address.data(), c->local_address.size(),
                                    c->remote_address.data(), c->remote_address.size()) != 1 ||
          std::none_of(
              interfaces.begin(), interfaces.end(),
              [&](const auto& i) { return i.address == c->local_address; })) {
        c->state->error = "UnsupportedNetworkPath";
        Abort(c);
        return;
      }
      const auto protocol =
          c->incoming
              ? connection->get_subprotocol()
              : connection->get_response_header("Sec-WebSocket-Protocol");
      if (protocol != kProtocol) {
        c->state->error = "IncompatibleProtocol";
        Abort(c);
        return;
      }
      if (c->state->logical &&
          c->remote_address != c->state->candidate_address) {
        c->state->error = "UnsupportedNetworkPath";
        Abort(c);
        return;
      }
      c->state->opened = true;
      if (auto parent = c->state->logical) {
        if (parent->state->closing || !dials.count(parent->id)) {
          Abort(c);
          return;
        }
        c->state->selected = true;
        parent->local_address = c->local_address;
        parent->remote_address = c->remote_address;
        parent->peer_fingerprint = c->peer_fingerprint;
        parent->state->opened = true;
        parent->state->send = c->state->send;
        parent->state->close = c->state->close;
        parent->state->graceful_close = c->state->graceful_close;
        EndDial(dials.at(parent->id), c);
        Emit(Event::Open, parent);
      } else {
        Emit(Event::Open, c);
      }
    });
    auto closed = [this, endpoint](Hdl hdl) {
      auto it = channels.find(hdl);
      if (it != channels.end()) {
        auto& state = *it->second->state;
        if (state.error.empty() && !state.opened && !state.closing) {
          Error error;
          auto connection = endpoint->get_con_from_hdl(hdl, error);
          if (!error && connection) {
            const auto failure = connection->get_ec();
            const bool http_replied = connection->get_response_code() !=
                                      websocketpp::http::status_code::uninitialized;
            const bool malformed = failure == websocketpp::error::make_error_code(
                                                  websocketpp::error::http_parse_error);
            state.error = state.logical && (http_replied || malformed)
                              ? "IncompatibleProtocol"
                              : "ConnectionFailed: " + failure.message();
          } else {
            state.error = "ConnectionFailed";
          }
        }
        const auto id = it->second->id;
        const bool incoming = it->second->incoming;
        Finish(it->second);
        channels.erase(it);
        if (!incoming) asio::post(io, [this, id] { clients.erase(id); });
      }
    };
    endpoint->set_close_handler(closed);
    endpoint->set_fail_handler(closed);
    endpoint->set_message_handler(
        [this](Hdl hdl, typename Endpoint::message_ptr message) {
          auto it = channels.find(hdl);
          if (it == channels.end()) return;
          auto c = it->second;
          auto& s = *c->state;
          if (s.closing) return;
          const auto& text = message->get_payload();
          if (message->get_opcode() != websocketpp::frame::opcode::text ||
              text.empty() || text.size() > 256 * 1024 ||
              ++s.received_count > 256 ||
              (s.received_bytes += text.size()) > 1024 * 1024) {
            Abort(c);
            return;
          }
          if (!s.logical || s.selected)
            Emit(Event::Message, s.logical ? s.logical : c, text);
        });
    endpoint->set_ping_handler([this](Hdl hdl, std::string nonce) {
      auto it = channels.find(hdl);
      if (it == channels.end()) return false;
      auto c = it->second;
      auto& s = *c->state;
      auto now = Clock::now();
      if (now - s.ping_window >= std::chrono::seconds(5)) {
        s.ping_window = now;
        s.inbound_pings = 0;
      }
      if (s.closing || nonce.size() != 16 || ++s.inbound_pings > 2) {
        Abort(c);
        return false;
      }
      return true;
    });
    endpoint->set_pong_handler([this](Hdl hdl, std::string nonce) {
      auto it = channels.find(hdl);
      if (it == channels.end()) return;
      auto& s = *it->second->state;
      if (!s.pending_ping.empty() && nonce == s.pending_ping)
        s.pending_ping.clear();
    });
  }
};
Transport::Transport() : impl_(std::make_unique<Impl>()) {}
Transport::~Transport() { Shutdown(); }
Transport::Listener Transport::Listen(const Identity& identity,
                                      const std::string& address,
                                      unsigned port) {
  ValidateListener(address, port);
  auto context = TlsContext();
  context->use_certificate_chain_file(identity.certificate().string());
  context->use_private_key_file(identity.private_key().string(), Context::pem);
  auto promise = std::make_shared<std::promise<Listener>>();
  auto future = promise->get_future();
  asio::post(impl_->io, [this, address, port, context, promise] {
    try {
      const auto binding = address == kAllInterfaces && !SupportsDualStack()
                               ? std::string("0.0.0.0")
                               : address;
      if (port) CheckExistingListener(binding, port);
      auto server = std::make_shared<Server>();
      impl_->Configure(server.get());
      const uint64_t id = impl_->next_id++;
      server->set_tls_init_handler([context](Hdl) { return context; });
      server->set_tcp_pre_init_handler(
          [this, endpoint = server.get(), id](Hdl hdl) {
            Error error;
            auto connection = endpoint->get_con_from_hdl(hdl, error);
            if (error) return;
            if (impl_->stopping || impl_->retired_listeners.count(id) ||
                impl_->preauth >= 16 || impl_->AdmissionBlocked() ||
                impl_->channels.size() >= 24) {
              connection->get_raw_socket().close(error);
              return;
            }
            auto c = std::make_shared<Channel>();
            c->id = impl_->next_id++;
            c->listener = id;
            c->incoming = true;
            c->state = std::make_shared<Channel::State>();
            ++impl_->preauth;
            impl_->Attach(endpoint, hdl, c);
          });
      server->set_validate_handler([endpoint = server.get()](Hdl hdl) {
        auto c = endpoint->get_con_from_hdl(hdl);
        if (c->get_resource() != kPath || !c->get_request_body().empty() ||
            !c->get_request_header("Transfer-Encoding").empty() ||
            (!c->get_request_header("Content-Length").empty() &&
             c->get_request_header("Content-Length") != "0"))
          return false;
        const auto& protocols = c->get_requested_subprotocols();
        if (protocols.size() != 1 || protocols[0] != kProtocol) return false;
        c->select_subprotocol(kProtocol);
        return true;
      });
      auto bound_acceptor = std::make_shared<Server::acceptor_ptr>();
      server->set_tcp_pre_bind_handler(
          [port, binding, bound_acceptor](Server::acceptor_ptr acceptor) {
            *bound_acceptor = acceptor;
            return ConfigureAcceptor(*acceptor, port == 0,
                                     binding.find(':') != std::string::npos,
                                     binding == kAllInterfaces);
          });
      Error listen_error;
      server->listen(asio::ip::tcp::endpoint(asio::ip::make_address(binding),
                                             static_cast<unsigned short>(port)),
                     listen_error);
      CheckListenError(listen_error);
      const auto actual_port = server->get_local_endpoint(listen_error).port();
      CheckListenError(listen_error);
#ifndef _WIN32
      if (!port) {
        // Allocation is already exclusive. Enable TIME_WAIT reuse before any
        // accepted socket inherits options so the saved port can restart too.
        (*bound_acceptor)->set_option(asio::socket_base::reuse_address(true),
                                      listen_error);
        CheckListenError(listen_error);
      }
#endif
      server->set_tcp_pre_bind_handler({});
      server->start_accept();
      impl_->servers[id] = server;
      promise->set_value({id, actual_port});
    } catch (...) {
      promise->set_exception(std::current_exception());
    }
  });
  return future.get();
}
void Transport::StopAccept(uint64_t listener) {
  asio::post(impl_->io, [this, listener] {
    auto it = impl_->servers.find(listener);
    if (it != impl_->servers.end()) {
      Error e;
      it->second->stop_listening(e);
      impl_->retired_listeners.insert(listener);
      for (auto& [_, c] : impl_->channels)
        if (c->listener == listener && !c->state->authenticated) {
          c->state->error = "ListenerChanged";
          impl_->Abort(c);
        }
    }
  });
}
Transport::ChannelPtr Transport::Connect(const DialEndpoint& target,
                                         const std::string& expected_key) {
  auto c = std::make_shared<Channel>();
  c->id = impl_->next_id++;
  c->state = std::make_shared<Channel::State>();
  asio::post(impl_->io, [this, c, target, expected_key] {
    if (impl_->stopping || c->state->closing || impl_->channels.size() >= 24 ||
        impl_->dials.size() >= 24 || impl_->AdmissionBlocked()) {
      c->state->error = "Busy";
      impl_->Finish(c);
      return;
    }
    if (target.kind == DialEndpoint::DnsName)
      impl_->Resolve(c, target, expected_key);
    else
      impl_->OpenClient(c, target.host, target.port, {});
  });
  return c;
}
bool Transport::Send(const ChannelPtr& c, std::string message,
                     bool discard_on_close) {
  if (!c || impl_->stopping || c->state->closing || message.empty() ||
      message.size() > 256 * 1024)
    return false;
  {
    std::lock_guard lock(c->state->budget_mutex);
    if (c->state->sent_count >= 256 ||
        c->state->sent_bytes + message.size() > 1024 * 1024)
      return false;
    ++c->state->sent_count;
    c->state->sent_bytes += message.size();
  }
  asio::post(impl_->io, [c, message = std::move(message), discard_on_close] {
    if (!c->state->terminal && c->state->send &&
        !(discard_on_close && c->state->closing))
      c->state->send(message);
  });
  return true;
}
void Transport::Authenticated(const ChannelPtr& c) {
  asio::post(impl_->io, [this, c] {
    if (c->incoming && !c->state->terminal && !c->state->authenticated) {
      c->state->authenticated = true;
      --impl_->preauth;
    }
  });
}
void Transport::Close(const ChannelPtr& c) {
  if (!c) return;
  c->state->closing = true;
  asio::post(impl_->io, [this, c] {
    if (c->state->opened && !c->state->terminal && c->state->graceful_close)
      c->state->graceful_close();
    else
      impl_->Abort(c);
  });
}
std::vector<Transport::Event> Transport::Poll() {
  std::lock_guard lock(impl_->event_mutex);
  std::vector<Event> result;
  result.swap(impl_->events);
  return result;
}
void Transport::Shutdown() {
  if (!impl_ || impl_->stopping.exchange(true)) return;
  asio::post(impl_->io, [this] {
    Error e;
    impl_->timer.cancel();
    while (!impl_->dials.empty()) impl_->EndDial(impl_->dials.begin()->second);
    for (auto& [_, server] : impl_->servers) server->stop_listening(e);
    for (auto& [_, c] : impl_->channels) impl_->Abort(c);
    impl_->work.reset();
  });
  if (impl_->thread.joinable()) impl_->thread.join();
}
}  // namespace crossdesk::lan
