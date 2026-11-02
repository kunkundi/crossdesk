#include "video_server.h"

#include <atomic>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

namespace crossdesk::cli {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using json = nlohmann::json;
using Handle = websocketpp::connection_hdl;
using Server = websocketpp::server<websocketpp::config::asio>;
constexpr size_t kMaxEncoded = 4 * 1024 * 1024;
constexpr size_t kMaxQueued = 8 * 1024 * 1024;

bool Token(const std::string &s) {
  return s.size() == 32 &&
         s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvw"
                             "xyz0123456789-_") == std::string::npos;
}
bool AppOrigin(const std::string &s) {
  return s == "null" ||
         (s.size() > 8 && s.size() <= 256 && s.compare(0, 8, "https://") == 0 &&
          s.find_first_of("/?#@\\ \t\r\n", 8) == std::string::npos);
}
bool BrowserOrigin(const std::string &s) {
  const std::string prefix = "http://127.0.0.1:";
  if (s.compare(0, prefix.size(), prefix) || s.size() > prefix.size() + 5 ||
      s.size() == prefix.size() ||
      s.find_first_not_of("0123456789", prefix.size()) != std::string::npos)
    return false;
  const auto port = std::stoul(s.substr(prefix.size()));
  return port > 0 && port <= 65535;
}
void Number(std::string &out, uint64_t value, int bytes) {
  for (int n = bytes - 1; n >= 0; --n)
    out.push_back(char(value >> (n * 8)));
}
} // namespace

struct VideoServer::State {
  struct Ticket {
    VideoGrant grant;
    Clock::time_point expires;
  };
  struct Receipt {
    uint32_t generation, sequence;
    size_t bytes;
    Clock::time_point sent;
  };
  struct Client {
    std::string origin, view, session;
    uint64_t view_generation = 0;
    uint64_t cursor_revision = 0;
    bool authorized = false, waiting_key = true;
    size_t bytes = 0;
    Clock::time_point opened = Clock::now(), pinged = Clock::now();
    std::deque<Receipt> receipts;
  };
  struct Packet {
    std::shared_ptr<const DecodedFrame> raw;
    EncodedVideoFrame encoded;
    size_t Size() const {
      return raw ? 32 + raw->nv12.size() : 24 + encoded.data.size();
    }
    uint64_t Generation() const {
      return raw ? raw->generation : encoded.generation;
    }
    uint64_t Sequence() const { return raw ? raw->frame_id : encoded.sequence; }
    bool Key() const { return raw || encoded.key; }
  };

  Server server;
  Server::timer_ptr timer;
  std::thread thread;
  std::string authority;
  std::map<Handle, Client, std::owner_less<Handle>> clients;
  std::map<std::string, Ticket> tickets;
  std::atomic<size_t> viewers{0}, outstanding{0}, dropped{0};
  std::atomic<bool> key_requested{false};
  uint64_t generation = 0;
  bool connected = false, decoded = false;
  Clock::time_point last_key_request{};

  // One posted drain, not one posted handler per picture. Raw frames replace
  // each other; the compressed handoff is bounded and recovers at an IDR.
  mutable std::mutex mutex;
  std::deque<Packet> queue;
  size_t queued_bytes = 0;
  bool scheduled = false, wait_key = true;
  json cursor;
  uint64_t cursor_revision = 0;

  State() {
    server.clear_access_channels(websocketpp::log::alevel::all);
    server.clear_error_channels(websocketpp::log::elevel::all);
    server.init_asio();
    server.set_open_handshake_timeout(3000);
    server.set_close_handshake_timeout(1000);
    server.set_pong_timeout(10000);
    server.set_max_message_size(1024);
    server.set_max_http_body_size(0);
    server.set_socket_init_handler([](Handle, asio::ip::tcp::socket &socket) {
      asio::error_code ec;
      socket.set_option(asio::ip::tcp::no_delay(true), ec);
      socket.set_option(asio::socket_base::send_buffer_size(256 * 1024), ec);
    });
    server.set_validate_handler([this](Handle h) {
      auto c = server.get_con_from_hdl(h);
      if (clients.size() >= 16 || c->get_resource() != "/api/video" ||
          c->get_request_header("Host") != authority ||
          c->get_request_header("Sec-WebSocket-Version") != "13" ||
          c->get_origin().empty() || c->get_origin().size() > 256) {
        c->set_status(websocketpp::http::status_code::forbidden);
        return false;
      }
      Client client;
      client.origin = c->get_origin();
      clients.emplace(h, std::move(client));
      return true;
    });
    server.set_message_handler([this](Handle h, Server::message_ptr message) {
      try {
        Message(h, message);
      } catch (...) {
        Drop(h);
      }
    });
    server.set_close_handler([this](Handle h) { Remove(h); });
    server.set_fail_handler([this](Handle h) { Remove(h); });
    server.set_pong_timeout_handler(
        [this](Handle h, const std::string &) { Drop(h); });
    server.listen(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));
    asio::error_code ec;
    authority =
        "127.0.0.1:" + std::to_string(server.get_local_endpoint(ec).port());
    if (ec)
      throw std::runtime_error("VIDEO_CHANNEL_UNAVAILABLE");
    server.start_accept();
    Maintain();
    thread = std::thread([this] { server.run(); });
  }
  ~State() {
    Call([this] {
      websocketpp::lib::error_code ec;
      server.stop_listening(ec);
      if (timer)
        timer->cancel();
      Revoke("");
    });
    server.stop();
    thread.join();
  }
  template <class F> void Call(F f) {
    auto task = std::make_shared<std::packaged_task<void()>>(std::move(f));
    auto result = task->get_future();
    server.get_io_service().post([task] { (*task)(); });
    result.get();
  }
  void Remove(Handle h) {
    auto it = clients.find(h);
    if (it == clients.end())
      return;
    if (it->second.authorized)
      --viewers;
    outstanding -= it->second.bytes;
    clients.erase(it);
  }
  void Drop(Handle h) {
    // Cancel even a partially sent raw picture on revocation. Waiting for a
    // WebSocket close frame behind megabytes of video would keep it alive.
    Remove(h);
    websocketpp::lib::error_code ec;
    auto c = server.get_con_from_hdl(h, ec);
    if (!ec) {
      asio::error_code ignored;
      c->get_raw_socket().cancel(ignored);
      c->get_raw_socket().close(ignored);
    }
  }
  void Revoke(const std::string &view) {
    for (auto it = tickets.begin(); it != tickets.end();) {
      if (view.empty() || it->second.grant.view == view)
        it = tickets.erase(it);
      else
        ++it;
    }
    std::vector<Handle> close;
    for (const auto &[h, c] : clients)
      if (view.empty() || c.view == view)
        close.push_back(h);
    for (auto h : close)
      Drop(h);
  }
  void RequestKey() {
    const auto now = Clock::now();
    if (now - last_key_request < 1s)
      return;
    last_key_request = now;
    key_requested = true;
  }
  void Message(Handle h, const Server::message_ptr &message) {
    auto it = clients.find(h);
    if (it == clients.end())
      return;
    auto &c = it->second;
    auto j = json::parse(message->get_payload(), nullptr, false);
    if (message->get_opcode() != websocketpp::frame::opcode::TEXT ||
        !j.is_object())
      return Drop(h);
    if (!c.authorized) {
      if (j.size() != 1 || !j.contains("ticket") || !j["ticket"].is_string())
        return Drop(h);
      auto ticket = tickets.find(j["ticket"].get<std::string>());
      if (ticket == tickets.end())
        return Drop(h);
      const auto grant = std::move(ticket->second);
      tickets.erase(ticket);
      if (!connected || grant.expires <= Clock::now() || viewers >= 8 ||
          (c.origin != grant.grant.origin &&
           !(grant.grant.apps && AppOrigin(c.origin))))
        return Drop(h);
      c.authorized = true;
      c.view = grant.grant.view;
      c.view_generation = grant.grant.view_generation;
      c.session = grant.grant.session;
      ++viewers;
      // Also prompts a raw viewer to receive the current fresh picture.
      key_requested = true;
      return;
    }
    if (j.value("type", "") == "keyframe" && j.size() == 1 && !decoded) {
      c.waiting_key = true;
      RequestKey();
      return;
    }
    if (j.value("type", "") != "ack" || j.size() != 3 ||
        !j.contains("generation") || !j["generation"].is_number_unsigned() ||
        !j.contains("sequence") || !j["sequence"].is_number_unsigned() ||
        c.receipts.empty() ||
        j["generation"] != c.receipts.front().generation ||
        j["sequence"] != c.receipts.front().sequence)
      return Drop(h);
    c.bytes -= c.receipts.front().bytes;
    outstanding -= c.receipts.front().bytes;
    c.receipts.pop_front();
  }
  void Maintain() {
    timer = server.set_timer(
        33, [this](const websocketpp::lib::error_code &error) {
          if (error)
            return;
          const auto now = Clock::now();
          std::string pointer;
          uint64_t revision;
          {
            std::lock_guard lock(mutex);
            revision = cursor_revision;
            if (!cursor.is_null()) pointer = cursor.dump();
          }
          for (auto it = tickets.begin(); it != tickets.end();)
            if (it->second.expires <= now)
              it = tickets.erase(it);
            else
              ++it;
          std::vector<Handle> close;
          for (auto &[h, c] : clients) {
            if (c.authorized && c.cursor_revision != revision && !pointer.empty()) {
              websocketpp::lib::error_code ec;
              auto connection = server.get_con_from_hdl(h, ec);
              // Coalesce pointer changes while a viewer is backpressured.
              if (!ec && connection->get_buffered_amount() < 256 * 1024) {
                server.send(h, pointer, websocketpp::frame::opcode::TEXT, ec);
                if (!ec) c.cursor_revision = revision;
              }
              if (ec) close.push_back(h);
            }
            if ((!c.authorized && now - c.opened >= 3s) ||
                (!c.receipts.empty() && now - c.receipts.front().sent >= 10s)) {
              close.push_back(h);
            } else if (c.authorized && now - c.pinged >= 10s) {
              c.pinged = now;
              websocketpp::lib::error_code ec;
              server.ping(h, "", ec);
              if (ec)
                close.push_back(h);
            }
          }
          for (auto h : close)
            Drop(h);
          Maintain();
        });
  }
  void Enqueue(Packet p) {
    std::lock_guard lock(mutex);
    if (p.raw) {
      dropped += queue.size();
      queue.clear();
      queued_bytes = 0;
    } else {
      if (queue.size() >= 60 || queued_bytes + p.Size() > kMaxQueued) {
        dropped += queue.size();
        queue.clear();
        queued_bytes = 0;
        wait_key = true;
        key_requested = true;
      }
      if (wait_key && !p.Key()) {
        ++dropped;
        return;
      }
      wait_key = false;
    }
    queued_bytes += p.Size();
    queue.push_back(std::move(p));
    if (!scheduled) {
      scheduled = true;
      server.get_io_service().post([this] { Drain(); });
    }
  }
  void Drain() {
    Packet p;
    {
      std::lock_guard lock(mutex);
      if (queue.empty()) {
        scheduled = false;
        return;
      }
      p = std::move(queue.front());
      queued_bytes -= p.Size();
      queue.pop_front();
    }
    if (connected && p.Generation() == generation && bool(p.raw) == decoded)
      Publish(p);
    // Let authorization, reads, and timers run between large pictures.
    server.get_io_service().post([this] { Drain(); });
  }
  void Publish(const Packet &p) {
    std::vector<Handle> ready;
    for (auto &[h, c] : clients) {
      if (!c.authorized)
        continue;
      // ACKs are flow-control hints, not proof that a client read the socket.
      // Also bound the actual send queue against forged early receipts.
      const auto buffered = server.get_con_from_hdl(h)->get_buffered_amount();
      if ((decoded && (!c.receipts.empty() || buffered > 0)) ||
          (!decoded && (c.receipts.size() >= 8 || c.bytes >= 1024 * 1024 ||
                        buffered >= 1024 * 1024))) {
        ++dropped;
        c.waiting_key = true;
        if (!decoded)
          RequestKey();
        continue;
      }
      if (!decoded && c.waiting_key && !p.Key()) {
        RequestKey();
        continue;
      }
      ready.push_back(h);
    }
    if (ready.empty())
      return;
    // Prepare once and share the immutable WebSocket message across viewers.
    // No length-prefixed pipe, Node Buffer, or per-viewer payload copy.
    auto message =
        server.get_con_from_hdl(ready.front())
            ->get_message(websocketpp::frame::opcode::BINARY, p.Size());
    auto &bytes = message->get_raw_payload();
    Number(bytes, decoded ? 0x43444e31 : 0x43445631, 4);
    Number(bytes, p.Generation(), 4);
    Number(bytes, p.Sequence(), 4);
    const auto time =
        p.raw ? std::chrono::duration_cast<std::chrono::microseconds>(
                    p.raw->queued.time_since_epoch())
                    .count()
              : p.encoded.timestamp_us;
    Number(bytes, time, 8);
    Number(bytes, p.Key() ? 1 : 0, 4);
    if (p.raw) {
      Number(bytes, p.raw->width, 4);
      Number(bytes, p.raw->height, 4);
      message->append_payload(p.raw->nv12.data(), p.raw->nv12.size());
    } else
      message->append_payload(p.encoded.data.data(), p.encoded.data.size());
    message->set_header(websocketpp::frame::prepare_header(
        websocketpp::frame::basic_header(websocketpp::frame::opcode::BINARY,
                                         bytes.size(), true, false),
        websocketpp::frame::extended_header(bytes.size())));
    message->set_prepared(true);
    for (auto h : ready) {
      auto &c = clients.at(h);
      websocketpp::lib::error_code ec;
      server.send(h, message, ec);
      if (ec) {
        Drop(h);
        continue;
      }
      c.waiting_key = false;
      c.bytes += bytes.size();
      outstanding += bytes.size();
      c.receipts.push_back({uint32_t(p.Generation()), uint32_t(p.Sequence()),
                            bytes.size(), Clock::now()});
    }
  }
};

VideoServer::VideoServer() : state_(std::make_unique<State>()) {}
VideoServer::~VideoServer() = default;
std::string VideoServer::Url() const {
  return "ws://" + state_->authority + "/api/video";
}
void VideoServer::Bind(uint64_t generation, bool decoded, bool connected) {
  auto &s = *state_;
  // Only the automation owner calls Bind; the I/O thread reads these values.
  s.Call([&s, generation, decoded, connected] {
    if (s.generation == generation && s.decoded == decoded &&
        s.connected == connected)
      return;
    s.Revoke("");
    s.generation = generation;
    s.decoded = decoded;
    s.connected = connected;
    s.key_requested = false;
    std::lock_guard lock(s.mutex);
    s.queue.clear();
    s.queued_bytes = 0;
    s.wait_key = true;
  });
}
void VideoServer::Grant(VideoGrant grant) {
  if (!Token(grant.ticket) || !Token(grant.view) || !Token(grant.session) ||
      !grant.view_generation || !BrowserOrigin(grant.origin))
    throw std::invalid_argument("INVALID_VIDEO_GRANT");
  auto &s = *state_;
  s.Call([&s, grant = std::move(grant)] {
    if (!s.connected)
      throw std::invalid_argument("NOT_READY");
    for (auto it = s.tickets.begin(); it != s.tickets.end();)
      if (it->second.expires <= Clock::now())
        it = s.tickets.erase(it);
      else
        ++it;
    if (s.tickets.size() >= 32)
      throw std::invalid_argument("VIDEO_LIMIT");
    // A replaced view cannot retain a socket or consume an old grant.
    bool changed = false;
    for (const auto &[h, c] : s.clients)
      changed |=
          c.view == grant.view && (c.view_generation != grant.view_generation ||
                                   c.session != grant.session);
    for (const auto &[ticket, t] : s.tickets)
      changed |= t.grant.view == grant.view &&
                 (t.grant.view_generation != grant.view_generation ||
                  t.grant.session != grant.session);
    if (changed)
      s.Revoke(grant.view);
    if (!s.tickets
             .emplace(grant.ticket, State::Ticket{grant, Clock::now() + 15s})
             .second)
      throw std::invalid_argument("INVALID_VIDEO_GRANT");
  });
}
void VideoServer::Revoke(const std::string &view) {
  state_->Call([&] { state_->Revoke(view); });
}
bool VideoServer::Enabled() const { return state_->viewers.load() > 0; }
bool VideoServer::TakeKeyframeRequest() {
  return state_->key_requested.exchange(false);
}
void VideoServer::Submit(std::shared_ptr<const DecodedFrame> frame) {
  if (!frame || !frame->width || !frame->height || (frame->width & 1) ||
      (frame->height & 1) || frame->width > 8192 || frame->height > 8192 ||
      uint64_t(frame->width) * frame->height > 16777216 ||
      frame->nv12.size() != size_t(frame->width) * frame->height * 3 / 2)
    throw std::invalid_argument("INVALID_VIDEO_FRAME");
  if (Enabled())
    state_->Enqueue({std::move(frame), {}});
}
void VideoServer::Submit(EncodedVideoFrame frame) {
  if (frame.data.empty() || frame.data.size() > kMaxEncoded)
    throw std::invalid_argument("INVALID_VIDEO_FRAME");
  if (Enabled())
    state_->Enqueue({{}, std::move(frame)});
}
void VideoServer::Cursor(json cursor) {
  auto &s = *state_;
  std::lock_guard lock(s.mutex);
  s.cursor = std::move(cursor);
  ++s.cursor_revision;
}
json VideoServer::Status() const {
  const auto &s = *state_;
  std::lock_guard lock(s.mutex);
  return {{"transport", "loopback-websocket/1"},
          {"viewers", s.viewers.load()},
          {"queued", s.queue.size()},
          {"queued_bytes", s.queued_bytes},
          {"unacknowledged_bytes", s.outstanding.load()},
          {"dropped", s.dropped.load()}};
}
} // namespace crossdesk::cli
