#include "controller_session.h"
#include "controller_diagnostics.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <display_stream_id.h>
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
    if (data && n && n <= 256 * 1024 && stream && sn < 64) {
      const std::string_view source(stream, sn);
      if (source == kFileStream || source == kFileFeedbackStream) {
        session.Enqueue({source == kFileStream ? 4 : 5, 0, std::string(data, n)});
        return;
      }
    }
    if (data && n && n <= web_clipboard::kHeaderBytes + web_clipboard::kChunkBytes &&
        stream && sn == std::strlen(web_clipboard::kStream) &&
        std::string_view(stream, sn) == web_clipboard::kStream) {
      session.Enqueue({3, 0, std::string(data, n)});
      return;
    }
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
  p.hardware_acceleration = options_.hardware_acceleration;
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
  p.on_receive_encoded_video = EncodedVideo;
  p.on_receive_audio_buffer = Audio;
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
  remote_capabilities_ = json::object();
  remote_results_.clear();
  input_requests_.clear();
  capability_request_ = 0;
  clipboard_ready_ = false;
  clipboard_assembler_ = {};
  displays_.clear();
  host_platform_ = HostPlatform::Unknown;
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
  WriteControllerDiagnostic("Session registering");
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
  AddDataStream(peer_, web_clipboard::kStream, true);
  AddDataStream(peer_, kFileStream, true);
  AddDataStream(peer_, kFileFeedbackStream, true);
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
  if (!Connected() && std::chrono::steady_clock::now() >= deadline_) {
    Fail(state_ == "registering" ? "signaling_timeout" : "connection_timeout");
    return;
  }
  for (auto &e : pending) {
    if (Finished())
      break;
    if (e.kind == 0) {
      WriteControllerDiagnostic("Signal status: " + std::to_string(e.code));
      if (e.code == SignalTlsCertError) {
        Fail("tls_certificate_error");
        break;
      }
      if (e.code == SignalFailed) {
        Fail("signaling_login_failed");
        break;
      }
      // The initial signaling transport can recover before a remote join is
      // sent. Keep its backoff worker alive within the original deadline.
      if (e.code == SignalReconnecting && state_ == "registering") {
        WriteControllerDiagnostic("Initial signaling retry within connection deadline");
        continue;
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
      WriteControllerDiagnostic("Connection status: " + std::to_string(e.code) + " (session " + state_ + ")");
      if (e.code == ::Connected && !Connected()) {
        state_ = "connected";
        auto hello = MakeHostInformation("CrossDesk CLI", {}, false);
        Send(kControlStream, hello.to_json());
        FreeRemoteAction(hello);
        RemoteAction mute{};
        mute.type = audio_capture;
        mute.a = false;
        Send(kControlStream, mute.to_json());
        capability_request_ = RequestRemote("capabilities");
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
    } else if ((e.kind == 4 || e.kind == 5) && Connected()) {
      if (files_) files_->Receive(e.data.data(), e.data.size(), e.kind == 5);
    } else if (e.kind == 3 && Connected()) {
      namespace clip = web_clipboard;
      if (e.data.size() < clip::kHeaderBytes) continue;
      const auto kind = clip::Read32(e.data.data());
      const auto id = clip::Read32(e.data.data() + 4);
      if (kind == clip::kReady && !id) { clipboard_ready_ = true; continue; }
      const auto found = remote_results_.find(id);
      if (!id || found == remote_results_.end()) continue;
      if (kind == clip::kApplied || kind == clip::kRejected) {
        found->second = {{"ok", kind == clip::kApplied},
            {"result", {{"applied", kind == clip::kApplied}}}, {"error", "CLIPBOARD_REJECTED"}};
      } else if (kind == clip::kEmpty) {
        found->second = {{"ok", true}, {"result", {{"text", ""}}}};
      } else if (kind == clip::kText) {
        const auto text = clipboard_assembler_.Receive(e.data);
        if (!text.empty()) found->second = {{"ok", true},
            {"result", {{"text", text.substr(clip::kHeaderBytes)}}}};
      }
    } else if (e.kind == 2 && Connected()) {
      auto j = ParseBoundedJson(e.data);
      if (!j.is_object())
        continue;
      if (j.value("type", json{}) == "file_browser") {
        if (files_) files_->Message(std::move(j));
        continue;
      }
      if (j.value("type", json{}) == "automation_result" &&
          j.value("version", json{}) == 1 && j.contains("id") &&
          j["id"].is_number_unsigned()) {
        const auto id = j["id"].get<uint64_t>();
        const auto found = remote_results_.find(id);
        if (found != remote_results_.end()) {
          found->second = j;
          if (id == capability_request_) {
            if (j.value("ok", json{}) == true && j.value("result", json{}).is_object())
              remote_capabilities_ = j["result"];
            if (remote_capabilities_.value("clipboard", json{}) == true)
              Send(web_clipboard::kStream, web_clipboard::Frame(
                  web_clipboard::kHello, 0, web_clipboard::kVersion));
            if (remote_capabilities_.value("files", json{}) == true)
              files_ = std::make_unique<ControllerFiles>([this](const char *stream, const std::string &data) {
                if (!peer_ || !Connected()) return false;
                return SendReliableDataFrame(peer_, data.data(), data.size(), stream) == 0;
              });
            remote_results_.erase(found);
          }
        }
        continue;
      }
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
        host_platform_ = a.i.platform;
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
  if (files_) files_->Tick();
}
bool ControllerSession::Send(const char *stream, const std::string &payload) {
  return peer_ && Connected() && !payload.empty() &&
         SendReliableDataFrame(peer_, payload.data(), payload.size(), stream) ==
             0;
}
void ControllerSession::ClosePeers() {
  files_.reset();
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    audio_enabled_ = false;
    audio_frames_.clear();
    audio_bytes_ = 0;
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
  WriteControllerDiagnostic("Session failed: " + std::string(reason) + " (stage " + state_ + ")");
  ReleaseInput();
  error_ = reason;
  state_ = "failed";
  control_ = false;
  ClosePeers();
  keys_.clear();
  buttons_.clear();
}
void ControllerSession::Disconnect() {
  if (state_ == "closed" && !control_)
    return;
  ReleaseInput();
  control_ = false;
  ClosePeers();
  keys_.clear();
  buttons_.clear();
  state_ = "closed";
}
bool ControllerSession::SelectDisplay(int index, bool preserve_input) {
  if (!Connected() || index < 0 || size_t(index) >= displays_.size())
    return false;
  if (!preserve_input) ReleaseInput();
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
  if (fps < 0 || fps > 60 || w < 2 || h < 2 || (w & 1) || (h & 1) || w > 3840 || h > 2160 ||
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
  s.frame_rate = options_.fps > 30 ? 60 : 30;
  // Preserve desktop text detail at every cadence, including 30 FPS previews.
  s.preference =
      static_cast<int>(VideoDegradationPreference::MaintainResolution);
  s.audio = audio_enabled_;
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
    WriteControllerDiagnostic(std::string("Remote control: ") + (enabled ? "true" : "false"));
  if (!enabled)
    ReleaseInput();
  control_ = enabled;
}
void ControllerSession::ReleaseInput() {
  // Release buttons before modifiers, so interrupted modified drags do not
  // turn into an unmodified drop. Failed releases remain tracked for cleanup.
  auto buttons = buttons_;
  for (int b : buttons)
    Pointer(pointer_x_, pointer_y_, b + 1);
  auto keys = keys_;
  for (auto it = keys.rbegin(); it != keys.rend(); ++it)
    Key(*it, false);
}
bool ControllerSession::Key(int key, bool down) {
  if ((!control_ && (down || !keys_.count(key))) || !Connected() || key < 0 ||
      key > 255)
    return false;
  RemoteAction a{};
  a.type = keyboard;
  a.k = {size_t(key), 0, false, down ? key_down : key_up};
  if (down)
    keys_.insert(key);
  const bool sent = SendInput(kKeyboardStream, a);
  if (sent && !down)
    keys_.erase(key);
  return sent;
}
void ControllerSession::ReleaseActionInput() {
  const auto buttons = buttons_, keys = keys_;
  for (int button : buttons)
    if (!action_buttons_.count(button)) Pointer(pointer_x_, pointer_y_, button + 1);
  for (auto it = keys.rbegin(); it != keys.rend(); ++it)
    if (!action_keys_.count(*it)) Key(*it, false);
}
bool ControllerSession::Pointer(float x, float y, int flag, int wheel) {
  const bool releasing =
      flag >= 2 && flag <= 6 && !(flag & 1) && buttons_.count(flag - 1);
  if ((!control_ && !releasing) || !Connected() || !std::isfinite(x) ||
      !std::isfinite(y) || x < 0 || y < 0 || x > 1 || y > 1 || flag < 0 || flag > 8 ||
      std::abs(int64_t(wheel)) > 1200)
    return false;
  RemoteAction a{};
  a.type = mouse;
  a.m = {x, y, wheel, static_cast<MouseFlag>(flag)};
  pointer_x_ = x;
  pointer_y_ = y;
  if (flag >= 1 && flag <= 6 && (flag & 1))
    buttons_.insert(flag);
  const bool sent = SendInput(kMouseStream, a);
  if (sent && flag >= 2 && flag <= 6 && !(flag & 1))
    buttons_.erase(flag - 1);
  return sent;
}
bool ControllerSession::Text(const std::string &text) {
  if (!control_ || !Connected() || !text_supported_ || text.empty() ||
      text.size() > kMaxTextInputBytes)
    return false;
  RemoteAction a{};
  a.type = text_input;
  a.text = text;
  try {
    return SendInput(kKeyboardStream, a);
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
    ds.push_back({{"index", i}, {"name", displays_[i].name},
                  {"left", displays_[i].left}, {"top", displays_[i].top},
                  {"right", displays_[i].right}, {"bottom", displays_[i].bottom}});
  std::lock_guard lock(mutex_);
  return {{"state", state_},
          {"client_id", client_id_},
          {"error", error_},
          {"generation", generation_},
          {"control", control_},
          {"held_key_codes", keys_},
          {"held_mouse_flags", buttons_},
          {"displays", ds},
          {"platform", HostPlatformName(host_platform_)},
          {"display", options_.display},
          {"text_input", text_supported_},
          {"audio", {{"enabled", audio_enabled_}, {"sample_rate", 48000}, {"channels", 1},
                     {"format", "s16le"}, {"received", audio_sequence_}, {"dropped", audio_dropped_}}},
          {"capabilities", remote_capabilities_},
          {"clipboard_ready", clipboard_ready_},
          {"preview_supported", preview_supported_},
          {"preview_resolution_supported", resolution_},
          {"preview_bitrate_supported", bandwidth_},
          {"preview_fps15_supported", fps15_},
          {"preview_request", preview_request_},
          {"preview_ack", preview_ack_},
          {"preview_accepted", preview_accepted_},
          {"preview_width", options_.preview_width},
          {"preview_fps", options_.fps},
          {"preview_height", options_.preview_height},
          {"preview_quality", options_.quality},
          {"preview_bitrate_bps", options_.max_bitrate_bps},
          {"native_video_decoder", SafeText(std::string(
              network_.video_decoder,
              std::find(network_.video_decoder,
                        network_.video_decoder + sizeof(network_.video_decoder), '\0')))},
          {"frames", frames_.Stats()},
          {"video_output",
           {{"enabled", video_output_},
            {"received", video_sequence_},
            {"queued", video_frames_.size()},
            {"queued_bytes", video_bytes_},
            {"waiting_keyframe", video_wait_key_},
            {"has_sps", !video_sps_.empty()},
            {"has_pps", !video_pps_.empty()}}},
          {"network",
           {{"video_bps", network_.video_inbound_stats.bitrate},
            {"rtt_ms", network_.rtt_ms},
            {"srtp", network_.srtp_active},
            {"traversal", int(traversal_)}}}};
}
} // namespace crossdesk
