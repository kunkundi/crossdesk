#include "controller_session.h"
#include "controller_diagnostics.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <display_stream_id.h>
#include <log.h>
#include <preview_subscription.h>
#include <stdexcept>
#include <stream_names.h>

namespace crossdesk {
using nlohmann::json;
ControllerSession::~ControllerSession() { Disconnect(); }
void ControllerSession::Enqueue(Event e) noexcept {
  try {
    std::lock_guard lock(mutex_);
    if (stopping_)
      return;
    if (events_.size() == 64) {
      overflow_ = true;
      return;
    }
    events_.push_back(std::move(e));
  } catch (...) { /* Allocation failure cannot escape a C callback. */
  }
}
void ControllerSession::Signal(SignalStatus s, const char *id, size_t n,
                               void *p) {
  try {
    static_cast<ControllerSession *>(p)->Enqueue(
        {0, s, id && n < 256 ? std::string(id, n) : ""});
  } catch (...) {
  }
}
void ControllerSession::Connection(ConnectionStatus s, const char *, size_t,
                                   void *p) {
  auto &session = *static_cast<ControllerSession *>(p);
  session.Enqueue({1, s, {}});
}
void ControllerSession::Data(const char *data, size_t n, const char *, size_t,
                             const char *stream, size_t sn, void *p) {
  auto &session = *static_cast<ControllerSession *>(p);
  try {
    if (data && n && n <= 65536 && stream && sn < 64 &&
        (std::string(stream, sn) == kControlStream ||
         std::string(stream, sn) == kWebControlStream))
      session.Enqueue({2, 0, std::string(data, n)});
  } catch (...) {
  }
}
void ControllerSession::Video(const MiniRtcVideoFrame *f, const char *, size_t,
                              const char *stream, size_t n, void *p) {
  auto &s = *static_cast<ControllerSession *>(p);
  try {
    std::lock_guard lock(s.mutex_);
    if (!s.stopping_ && f && stream && n < 64 &&
        std::string(stream, n) == MakeDisplayStreamId(s.callback_display_))
      s.frames_.Submit(*f, s.callback_generation_);
  } catch (...) {
  }
}
void ControllerSession::Network(const char *, size_t, TraversalMode mode,
                                const MiniRtcNetTrafficStats *stats,
                                const char *, size_t, void *p) {
  auto &session = *static_cast<ControllerSession *>(p);
  if (stats) {
    std::lock_guard lock(session.mutex_);
    session.network_ = *stats;
    session.traversal_ = mode;
  }
}
Params ControllerSession::Parameters() {
  Params p{};
  std::snprintf(p.signal_server_ip, sizeof(p.signal_server_ip), "%s",
                options_.host.c_str());
  std::snprintf(p.log_path, sizeof(p.log_path), "%s",
                options_.log_directory.c_str());
  p.signal_server_port = options_.port;
  p.hardware_acceleration = false;
  p.native_video_output = true;
  p.av1_encoding = false;
  p.turn_mode = TurnAutoUdpTcp;
  p.enable_srtp = true;
  p.video_content_type = VideoContentType::ScreenContent;
  p.video_quality = QualityLow;
  p.video_frame_rate = 30;
  p.video_degradation_preference =
      VideoDegradationPreference::MaintainResolution;
  p.on_signal_status = Signal;
  p.on_connection_status = Connection;
  p.on_net_status_report = Network;
  p.on_receive_data_buffer = Data;
  p.on_receive_video_frame = Video;
  // No audio callback, clipboard receiver, file writer or incoming input
  // handler.
  p.user_id = "cli";
  p.user_data = this;
  return p;
}
void ControllerSession::Connect(SessionOptions options, std::string device,
                                std::string password) {
  Disconnect();
  if (device.size() != 9 ||
      device.find_first_not_of("0123456789") != std::string::npos)
    throw std::invalid_argument("device ID must contain 9 digits");
  if (password.size() > 256 || password.find('\0') != std::string::npos)
    throw std::invalid_argument("invalid password length or encoding");
  if (options.host.empty() || options.host.size() >= 256 || options.port < 1 ||
      options.port > 65535 || options.log_directory.empty() ||
      options.log_directory.size() >= 256 || options.display < 0 ||
      options.display > 63)
    throw std::invalid_argument(
        "invalid server, log directory or display index");
  options_ = std::move(options);
  device_ = std::move(device);
  password_ = std::move(password);
  state_ = "registering";
  error_.clear();
  displays_.clear();
  preview_supported_ = resolution_ = fps15_ = bandwidth_ = text_supported_ =
      false;
  preview_request_ = preview_ack_ = 0;
  preview_accepted_ = false;
  deadline_ = std::chrono::steady_clock::now() + options_.timeout;
  {
    std::lock_guard lock(mutex_);
    stopping_ = false;
    overflow_ = false;
    events_.clear();
    network_ = {};
    traversal_ = UnknownMode;
    callback_generation_ = ++generation_;
    callback_display_ = options_.display;
    frames_.Reset(generation_);
  }
  // The server assigns a temporary controller identity on this connection.
  // Never register a desktop identity or persist local credentials.
  client_id_.clear();
  InitializeControllerDiagnostics(options_.log_directory);
  SPDLOG_LOGGER_INFO(minirtc::get_logger(), "Session registering");
  auto p = Parameters();
  peer_ = CreatePeer(&p);
  if (!peer_) {
    Fail("media_initialization_failed");
    return;
  }
  AddAudioStream(peer_, kAudioStream);
  AddDataStream(peer_, kDataStream, false);
  for (auto stream : {kMouseStream, kKeyboardStream, kControlStream})
    AddDataStream(peer_, stream, true);
  if (Init(peer_) != 0)
    Fail("media_initialization_failed");
}
void ControllerSession::Tick() {
  std::deque<Event> pending;
  bool overflow;
  {
    std::lock_guard lock(mutex_);
    pending.swap(events_);
    overflow = overflow_;
  }
  if (Finished() || state_ == "idle")
    return;
  if (overflow) {
    Fail("control_event_overflow");
    return;
  }
  for (auto &e : pending) {
    if (Finished())
      break;
    if (e.kind == 0) {
      SPDLOG_LOGGER_INFO(minirtc::get_logger(), "Signal status: {}", e.code);
      if (e.code == SignalTlsCertError) {
        Fail("tls_certificate_error");
        break;
      }
      if (e.code == SignalFailed) {
        Fail("signaling_login_failed");
        break;
      }
      if (e.code == SignalClosed || e.code == SignalServerClosed ||
          e.code == SignalReconnecting) {
        Fail("signaling_disconnected");
        break;
      }
      if (e.code != SignalConnected)
        continue;
      if (state_ == "registering") {
        if (e.data.size() != 13 || e.data.rfind("cli-", 0) != 0 ||
            e.data.find_first_not_of("0123456789", 4) != std::string::npos) {
          Fail("cli_identity_not_supported");
          break;
        }
        client_id_ = e.data;
        state_ = "connecting";
        const auto join = device_ + "@" + password_;
        if (JoinConnection(peer_, join.c_str()) != 0)
          Fail("join_request_failed");
        std::fill(password_.begin(), password_.end(), '\0');
        password_.clear();
      }
    } else if (e.kind == 1) {
      SPDLOG_LOGGER_INFO(minirtc::get_logger(), "Connection status: {} (session {})",
                         e.code, state_);
      if (e.code == ::Connected && !Connected()) {
        state_ = "connected";
        auto hello = MakeHostInformation("CrossDesk CLI", {}, false);
        Send(kControlStream, hello.to_json());
        FreeRemoteAction(hello);
        RemoteAction mute{};
        mute.type = audio_capture;
        mute.a = false;
        Send(kControlStream, mute.to_json());
      } else if (e.code == IncorrectPassword)
        Fail("incorrect_password");
      else if (e.code == NoSuchTransmissionId)
        Fail("device_not_found");
      else if (e.code == RemoteUnavailable)
        Fail("device_offline_or_unavailable");
      else if (e.code == Failed)
        Fail(Connected() ? "connection_lost" : "connection_failed");
      else if (e.code == Closed || e.code == Disconnected)
        Fail("remote_disconnected");
    } else if (e.kind == 2 && Connected()) {
      auto j = ParseBoundedJson(e.data);
      if (!j.is_object())
        continue;
      if (j.value("type", json{}) == "deskhub_preview_status") {
        if (j.value("request_id", json{}) == preview_request_ &&
            j.contains("accepted") && j["accepted"].is_boolean()) {
          preview_ack_ = preview_request_;
          preview_accepted_ = j["accepted"];
        }
        continue;
      }
      RemoteAction a{};
      if (!a.from_json(e.data))
        continue;
      if (a.type == host_infomation) {
        displays_.clear();
        for (size_t i = 0; i < std::min(a.i.display_num, size_t(64)); ++i)
          displays_.push_back({SafeText(a.i.display_list[i]), a.i.left[i],
                               a.i.top[i], a.i.right[i], a.i.bottom[i]});
        preview_supported_ = a.i.supports_deskhub_preview;
        resolution_ = a.i.supports_deskhub_resolution;
        fps15_ = a.i.supports_deskhub_fps15;
        bandwidth_ = a.i.supports_deskhub_bandwidth;
        text_supported_ = a.i.supports_text_input;
        if (!SelectDisplay(options_.display))
          Fail("display_not_available");
        else
          Preview();
      }
      FreeRemoteAction(a);
    }
  }
  if (!Connected() && !Finished() &&
      std::chrono::steady_clock::now() > deadline_)
    Fail("connection_timeout");
}
bool ControllerSession::Send(const char *stream, const std::string &payload) {
  return peer_ && Connected() && !payload.empty() &&
         SendReliableDataFrame(peer_, payload.data(), payload.size(), stream) ==
             0;
}
void ControllerSession::ClosePeers() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    callback_generation_ = ++generation_;
    frames_.Reset(generation_);
  }
  if (peer_) {
    LeaveConnection(peer_, device_.c_str());
    DestroyPeer(&peer_);
  }
  std::fill(password_.begin(), password_.end(), '\0');
  password_.clear();
}
void ControllerSession::Fail(const char *reason) {
  SPDLOG_LOGGER_INFO(minirtc::get_logger(), "Session failed: {}", reason);
  ReleaseInput();
  error_ = reason;
  state_ = "failed";
  control_ = false;
  ClosePeers();
}
void ControllerSession::Disconnect() {
  if (state_ == "closed" && !control_)
    return;
  ReleaseInput();
  control_ = false;
  ClosePeers();
  state_ = "closed";
}
bool ControllerSession::SelectDisplay(int index) {
  if (!Connected() || index < 0 || size_t(index) >= displays_.size())
    return false;
  ReleaseInput();
  options_.display = index;
  {
    std::lock_guard lock(mutex_);
    callback_display_ = index;
    callback_generation_ = ++generation_;
    frames_.Reset(generation_);
  }
  RemoteAction a{};
  a.type = display_id;
  a.d = index;
  return Send(kControlStream, a.to_json());
}
void ControllerSession::SetPreview(int fps, int w, int h, int quality,
                                   int bitrate) {
  if (fps < 0 || fps > 30 || w < 2 || h < 2 || w > 3840 || h > 2160 ||
      quality < -1 || quality > 2 || bitrate < -1 || bitrate > 100000000)
    throw std::invalid_argument("preview out of range");
  quality = quality < 0 ? options_.quality : quality;
  bitrate = bitrate < 0 ? options_.max_bitrate_bps : bitrate;
  if (options_.fps == fps && options_.preview_width == w &&
      options_.preview_height == h && options_.quality == quality &&
      options_.max_bitrate_bps == bitrate)
    return;
  options_.fps = fps;
  options_.preview_width = w;
  options_.preview_height = h;
  options_.quality = quality;
  options_.max_bitrate_bps = bitrate;
  Preview();
}
void ControllerSession::Preview() {
  if (!Connected() || !preview_supported_)
    return;
  PreviewSubscription s;
  s.request_id = ++preview_request_;
  preview_accepted_ = false;
  s.fps = options_.fps == 0              ? 0
          : options_.fps > 15            ? 60
          : options_.fps <= 1            ? 1
          : options_.fps <= 8 || !fps15_ ? 8
                                         : 15;
  // 60 is the existing interactive-mode sentinel, not a 60 FPS request.
  // The encoder cadence is independently specified by frame_rate.
  s.frame_rate = 30;
  s.audio = false;
  s.quality = options_.quality;
  if (resolution_) {
    s.version = 2;
    s.width = options_.preview_width;
    s.height = options_.preview_height;
  }
  if (bandwidth_)
    s.max_bitrate_bps = options_.max_bitrate_bps;
  Send(kControlStream, s.ToJson().dump());
}
void ControllerSession::SetControl(bool enabled) {
  if (control_ != enabled)
    SPDLOG_LOGGER_INFO(minirtc::get_logger(), "Remote control: {}", enabled);
  if (!enabled)
    ReleaseInput();
  control_ = enabled;
}
void ControllerSession::ReleaseInput() {
  auto keys = keys_;
  for (int k : keys)
    Key(k, false);
  auto buttons = buttons_;
  for (int b : buttons)
    Pointer(pointer_x_, pointer_y_, b + 1);
  keys_.clear();
  buttons_.clear();
}
bool ControllerSession::Key(int key, bool down) {
  if (!control_ || !Connected() || key < 0 || key > 255)
    return false;
  RemoteAction a{};
  a.type = keyboard;
  a.k = {size_t(key), 0, false, down ? key_down : key_up};
  if (down)
    keys_.insert(key);
  else
    keys_.erase(key);
  return Send(kKeyboardStream, a.to_json());
}
bool ControllerSession::Pointer(float x, float y, int flag, int wheel) {
  if (!control_ || !Connected() || !std::isfinite(x) || !std::isfinite(y) ||
      x < 0 || y < 0 || x > 1 || y > 1 || flag < 0 || flag > 8 ||
      std::abs(int64_t(wheel)) > 1200)
    return false;
  RemoteAction a{};
  a.type = mouse;
  a.m = {x, y, wheel, static_cast<MouseFlag>(flag)};
  pointer_x_ = x;
  pointer_y_ = y;
  if (flag >= 1 && flag <= 6) {
    if (flag & 1)
      buttons_.insert(flag);
    else
      buttons_.erase(flag - 1);
  }
  return Send(kMouseStream, a.to_json());
}
bool ControllerSession::Text(const std::string &text) {
  if (!control_ || !Connected() || !text_supported_ || text.empty() ||
      text.size() > kMaxTextInputBytes)
    return false;
  RemoteAction a{};
  a.type = text_input;
  a.text = text;
  try {
    return Send(kKeyboardStream, a.to_json());
  } catch (...) {
    return false;
  }
}
std::optional<DecodedFrame> ControllerSession::TakeFrame(DecodedFrame *recycle) {
  return frames_.Take(recycle);
}
json ControllerSession::Status() const {
  json ds = json::array();
  for (size_t i = 0; i < displays_.size(); ++i)
    ds.push_back({{"index", i}, {"name", displays_[i].name}});
  std::lock_guard lock(mutex_);
  return {{"state", state_},
          {"client_id", client_id_},
          {"error", error_},
          {"generation", generation_},
          {"control", control_},
          {"displays", ds},
          {"display", options_.display},
          {"text_input", text_supported_},
          {"preview_supported", preview_supported_},
          {"preview_request", preview_request_},
          {"preview_ack", preview_ack_},
          {"preview_accepted", preview_accepted_},
          {"preview_width", options_.preview_width},
          {"preview_height", options_.preview_height},
          {"preview_quality", options_.quality},
          {"preview_bitrate_bps", options_.max_bitrate_bps},
          {"frames", frames_.Stats()},
          {"network",
           {{"video_bps", network_.video_inbound_stats.bitrate},
            {"rtt_ms", network_.rtt_ms},
            {"srtp", network_.srtp_active},
            {"traversal", int(traversal_)}}}};
}
} // namespace crossdesk
