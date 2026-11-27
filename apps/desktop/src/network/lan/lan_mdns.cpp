#include "lan_mdns.h"

#if defined(_WIN32) || \
    (defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__))
#include <asio/ip/multicast.hpp>
#include <asio/ip/udp.hpp>
#include <asio/steady_timer.hpp>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <utility>

#include "lan_identity.h"

namespace crossdesk::lan {
namespace {
using Address = asio::ip::address;
using Udp = asio::ip::udp;
using Bytes = std::vector<unsigned char>;
using Error = asio::error_code;
using namespace std::chrono_literals;

unsigned Word(const unsigned char* p) { return (unsigned(p[0]) << 8) | p[1]; }

// Match individual wire labels, including compressed names. A literal dot in
// a label must never compare equal to a separator in the requested name.
bool ReadName(const unsigned char* data, size_t size, size_t& pos,
              const std::string& host, bool& matches) {
  size_t cursor = pos, end = 0, matched = 0, expanded = 0;
  matches = true;
  for (unsigned steps = 0; cursor < size && steps < 150; ++steps) {
    const auto len = data[cursor++];
    if (!len) {
      pos = end ? end : cursor;
      matches = matches && matched == host.size();
      return true;
    }
    if ((len & 0xc0) == 0xc0) {
      if (cursor >= size) return false;
      const size_t offset = ((len & 63u) << 8) | data[cursor];
      if (offset < 12 || offset >= cursor - 1) return false;
      if (!end) end = cursor + 1;
      cursor = offset;
      continue;
    }
    if (len > 63 || len > size - cursor || (expanded += len + 1) > 254)
      return false;
    if (matched) {
      matches = matches && matched < host.size() && host[matched] == '.';
      ++matched;
    }
    for (unsigned n = 0; n < len; ++n) {
      unsigned char c = data[cursor++];
      if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
      matches =
          matches && matched < host.size() && c != '.' && c == host[matched];
      ++matched;
    }
  }
  return false;
}

Bytes Question(const std::string& host, unsigned id, unsigned char type) {
  Bytes wire(12);
  wire[0] = static_cast<unsigned char>(id >> 8);
  wire[1] = static_cast<unsigned char>(id);
  wire[5] = 1;  // One question.
  for (size_t start = 0; start < host.size();) {
    auto stop = host.find('.', start);
    if (stop == std::string::npos) stop = host.size();
    wire.push_back(static_cast<unsigned char>(stop - start));
    wire.insert(wire.end(), host.begin() + start, host.begin() + stop);
    start = stop + 1;
  }
  wire.insert(wire.end(), {0, 0, type, 0, 1});
  return wire;
}

std::vector<Address> Answers(const unsigned char* data, size_t size,
                             const std::string& host, unsigned id4,
                             unsigned scope) {
  std::vector<Address> result;
  if (size < 12 || (Word(data + 2) & 0xfa0f) != 0x8000 || Word(data + 4) != 1)
    return {};
  const auto id = Word(data);
  const unsigned type = id == id4 ? 1 : 28;
  if (id != id4 && id != (id4 ^ 1u)) return {};
  size_t pos = 12;
  bool match;
  if (!ReadName(data, size, pos, host, match) || !match || size - pos < 4 ||
      Word(data + pos) != type || Word(data + pos + 2) != 1)
    return {};
  pos += 4;
  for (unsigned n = 0; n < Word(data + 6); ++n) {
    if (!ReadName(data, size, pos, host, match) || size - pos < 10) return {};
    const unsigned length = Word(data + pos + 8);
    if (length > size - pos - 10) return {};
    const bool usable = match && Word(data + pos) == type &&
                        (Word(data + pos + 2) & 0x7fff) == 1 &&
                        (Word(data + pos + 4) || Word(data + pos + 6));
    pos += 10;
    if (usable && type == 1 && length == 4) {
      asio::ip::address_v4::bytes_type b;
      std::copy_n(data + pos, b.size(), b.begin());
      result.emplace_back(asio::ip::address_v4(b));
    } else if (usable && type == 28 && length == 16) {
      asio::ip::address_v6::bytes_type b;
      std::copy_n(data + pos, b.size(), b.begin());
      asio::ip::address_v6 ip(b);
      if (ip.is_link_local()) ip.scope_id(scope);
      if (!ip.is_link_local() || scope) result.emplace_back(ip);
    }
    pos += length;
  }
  return result;
}

template <typename BytesType>
bool SamePrefix(const BytesType& a, const BytesType& b, unsigned bits) {
  if (!bits || bits > a.size() * 8) return false;
  const auto whole = bits / 8, rest = bits % 8;
  return std::equal(a.begin(), a.begin() + whole, b.begin()) &&
         (!rest || ((a[whole] ^ b[whole]) >> (8 - rest)) == 0);
}

struct Link {
  Link(asio::io_context& io, Address address, const Interface& iface)
      : socket(io),
        local(address),
        scope(iface.ipv6_index),
        prefix(iface.prefix_length) {}
  Udp::socket socket;
  Address local;
  unsigned scope, prefix;
  Udp::endpoint group, sender;
  std::array<unsigned char, 9000> packet;
  bool SameSubnet(const Address& other) const {
    if (local.is_v4() != other.is_v4()) return false;
    if (local.is_v4())
      return SamePrefix(local.to_v4().to_bytes(), other.to_v4().to_bytes(),
                        prefix);
    return SamePrefix(local.to_v6().to_bytes(), other.to_v6().to_bytes(),
                      prefix);
  }
  bool OnLink() const {
    const auto remote = sender.address();
    if (sender.port() != 5353 || remote.is_v4() != local.is_v4()) return false;
    if (local.is_v4()) return SameSubnet(remote);
    const auto v6 = remote.to_v6();
    if (v6.is_link_local()) return v6.scope_id() == scope;
    return SameSubnet(remote);
  }
};

// RFC 6762 section 6.7. An ephemeral source port asks existing responders
// for unicast replies and coexists with a system mDNS daemon on port 5353.
struct Query : std::enable_shared_from_this<Query> {
  Query(asio::io_context& io, const std::string& host, LocalNameResult result)
      : io(io),
        finish(io),
        retry(io),
        batch(io),
        host(host),
        result(std::move(result)) {}
  asio::io_context& io;
  asio::steady_timer finish, retry, batch;
  std::string host;
  LocalNameResult result;
  unsigned id4 = 0;
  std::array<Bytes, 2> questions;
  std::vector<std::shared_ptr<Link>> links;
  std::vector<Address> found, pending;

  void Stop(bool deliver) {
    auto callback = std::exchange(result, {});
    if (!callback) return;
    finish.cancel();
    retry.cancel();
    batch.cancel();
    for (auto& link : links) {
      Error ignored;
      link->socket.close(ignored);
    }
    if (deliver) callback(std::move(pending), true);
  }
  void FinishAfter(std::chrono::milliseconds delay) {
    finish.expires_after(delay);
    finish.async_wait([q = shared_from_this()](Error e) {
      if (!e) q->Stop(true);
    });
  }
  void Send() {
    for (auto& link : links)
      for (const auto& question : questions)
        link->socket.async_send_to(asio::buffer(question), link->group,
                                   [q = shared_from_this()](Error, size_t) {});
  }
  void Receive(const std::shared_ptr<Link>& link) {
    link->socket.async_receive_from(
        asio::buffer(link->packet), link->sender,
        [q = shared_from_this(), link](Error e, size_t size) {
          if (e || !q->result) return;
          const bool was_empty = q->pending.empty();
          if (link->OnLink()) {
            for (const auto& ip : Answers(link->packet.data(), size, q->host,
                                          q->id4, link->scope)) {
              if (q->found.size() < 16 && !ip.is_unspecified() &&
                  !ip.is_multicast() && !ip.is_loopback() &&
                  !(ip.is_v6() && ip.to_v6().is_v4_mapped()) &&
                  std::find(q->found.begin(), q->found.end(), ip) ==
                      q->found.end()) {
                q->found.push_back(ip);
                q->pending.push_back(ip);
              }
            }
          }
          // Coalesce simultaneous A/AAAA replies but keep listening for a
          // slower family or another link until a connection wins.
          if (was_empty && !q->pending.empty()) {
            q->batch.expires_after(25ms);
            q->batch.async_wait([q](Error error) {
              if (error || !q->result) return;
              auto deliver = q->result;
              deliver(std::exchange(q->pending, {}), false);
            });
          }
          q->Receive(link);
        });
  }
  void Start() {
    unsigned char random[2];
    if (RAND_bytes(random, sizeof(random)) != 1) {
      FinishAfter(0ms);
      return;
    }
    id4 = Word(random);
    questions = {Question(host, id4, 1), Question(host, id4 ^ 1u, 28)};
    for (const auto& iface : LocalMulticastInterfaces()) {
      Error e;
      auto local = asio::ip::make_address(iface.address, e);
      if (e || !iface.prefix_length || (local.is_v6() && !iface.ipv6_index))
        continue;
      // Temporary IPv6 addresses on one link commonly share a prefix.
      if (iface.ipv6_index &&
          std::any_of(links.begin(), links.end(), [&](const auto& link) {
            return link->scope == iface.ipv6_index &&
                   link->prefix == iface.prefix_length &&
                   link->SameSubnet(local);
          }))
        continue;
      auto link = std::make_shared<Link>(io, local, iface);
      const Udp::endpoint bind(local, 0);
      link->socket.open(bind.protocol(), e);
      if (e) continue;
      link->socket.bind(bind, e);
      if (e) continue;
      if (local.is_v4()) {
        link->socket.set_option(
            asio::ip::multicast::outbound_interface(local.to_v4()), e);
        link->group = {asio::ip::make_address_v4("224.0.0.251"), 5353};
      } else {
        link->socket.set_option(
            asio::ip::multicast::outbound_interface(iface.ipv6_index), e);
        auto group = asio::ip::make_address_v6("ff02::fb");
        group.scope_id(iface.ipv6_index);
        link->group = {group, 5353};
      }
      if (e) continue;
      link->socket.set_option(asio::ip::multicast::hops(255), e);
      if (e) continue;
      links.push_back(link);
      Receive(link);
      if (links.size() == 32) break;
    }
    FinishAfter(links.empty() ? 0ms : 2300ms);
    if (links.empty()) return;
    Send();
    retry.expires_after(1s);
    retry.async_wait([q = shared_from_this()](Error e) {
      if (!e) q->Send();
    });
  }
};
}  // namespace

std::function<void()> QueryLocalName(asio::io_context& io,
                                     const std::string& host,
                                     LocalNameResult result) {
  auto query = std::make_shared<Query>(io, host, std::move(result));
  query->Start();
  return [query] { query->Stop(false); };
}
}  // namespace crossdesk::lan
#endif
