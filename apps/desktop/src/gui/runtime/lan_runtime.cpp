#include <display_stream_id.h>

#include "rd_log.h"
#include "runtime/gui_runtime.h"
#if defined(__linux__) && !defined(__OHOS__)
#include "platform/linux/headless/headless_console.h"
#endif

namespace crossdesk {
void GuiRuntime::RegisterStreams(MiniRtcPeerPtr* peer, size_t displays) {
  for (size_t i = 0; i < displays; ++i) {
    const auto id = MakeDisplayStreamId(i);
    MiniRtcAddVideoStream(peer, id.c_str());
  }
  MiniRtcAddAudioStream(peer, audio_label_.c_str());
  MiniRtcAddDataStream(peer, data_label_.c_str(), false);
  for (const auto& label :
       {mouse_label_, keyboard_label_, control_data_label_, file_label_,
        file_feedback_label_, clipboard_label_})
    MiniRtcAddDataStream(peer, label.c_str(), true);
}
void GuiRuntime::InitializeLan() {
  if (unattended_host_ || stopping_ || lan_ ||
      !config_center_->IsIpDirectEnabled()) return;
#if defined(__linux__) && !defined(__OHOS__)
  if (HeadlessConsole::Instance().active()) return;
#endif
  lan_error_.clear();
  lan::SessionManager::Hooks hooks;
  hooks.error = [this](const std::string& error) {
    if (!lan_stopping_ && !stopping_) lan_error_ = error;
  };
  hooks.prepare = [this](const std::shared_ptr<lan::Session>& session) {
    if (stopping_ || lan_stopping_ || devices_.EnsureCaptureReady() != 0)
      throw std::runtime_error("Capture initialization failed");
    const size_t displays = devices_.display_info_list().size();
    auto events = std::make_shared<PeerEventHandler>(*this);
    events->BindDirect(session);
    // Direct initialization must not depend on cloud login/configuration having
    // successfully populated the managed peer's MiniRtcParams snapshot.
    MiniRtcParams params{};
    std::snprintf(params.log_path, sizeof(params.log_path), "%s",
                  dll_log_path_.c_str());
    params.hardware_acceleration = true;
    params.native_video_output = true;
    params.av1_encoding = config_center_->GetVideoEncodeFormat() ==
                          ConfigCenter::VIDEO_ENCODE_FORMAT::AV1;
    params.enable_srtp = true;
    params.turn_mode = MiniRtcTurnMode::MiniRtcTurnDisabled;
    params.direct_start_bitrate_bps = config_center_->GetDirectStartBitrateBps();
    params.relay_start_bitrate_bps = config_center_->GetRelayStartBitrateBps();
    params.disable_receiver_padding_probing =
        config_center_->IsReceiverPaddingProbingDisabled();
    params.video_content_type = MiniRtcVideoContentType::ScreenContent;
    params.video_quality = MiniRtcVideoQuality::MiniRtcVideoQualityHigh;
    params.video_frame_rate = 60;
    params.video_degradation_preference =
        MiniRtcVideoDegradationPreference::MaintainResolution;
    params.on_receive_video_frame = PeerEventHandler::OnReceiveVideoBuffer;
    params.on_receive_audio_buffer = PeerEventHandler::OnReceiveAudioBuffer;
    params.on_receive_data_buffer = PeerEventHandler::OnReceiveDataBuffer;
    params.on_connection_status = PeerEventHandler::OnConnectionStatus;
    params.on_net_status_report = PeerEventHandler::OnNetStatusReport;
    params.user_id = session->local_id.c_str();
    params.user_data = events.get();
    if (!session->incoming) {
      auto props = FindRemoteSession(session->key);
      if (!props) throw std::runtime_error("LAN session cancelled");
      props->peer_events_ = events;
      props->local_id_ = session->local_id;
    }
    return [this, session, events, params, displays] {
      lan::SessionManager::Prepared result;
      result.callback_owner = events;
      result.drain_callbacks = [events] { events->Deactivate(); };
      result.peer = MiniRtcCreatePeer(&params);
      events->SetDirectPeer(result.peer);
      if (result.peer) RegisterStreams(result.peer, displays);
      return result;
    };
  };
  hooks.attached = [this](const std::shared_ptr<lan::Session>& session) {
    auto events =
        std::static_pointer_cast<PeerEventHandler>(session->callback_owner);
    std::unique_lock lock(remote_sessions_mutex_);
    if (session->incoming)
      lan_controllers_[session->key] = {session, events};
    else if (auto it = remote_sessions_.find(session->key);
             it != remote_sessions_.end())
      it->second->peer_ = session->peer;
  };
  hooks.connected = [this](const std::shared_ptr<lan::Session>& session) {
    auto events =
        std::static_pointer_cast<PeerEventHandler>(session->callback_owner);
    events->ApplyConnectionStatus(MiniRtcConnectionStatus::MiniRtcConnectionConnected, session->key);
    events->FlushEarlyData();
  };
  hooks.closed = [this](const std::shared_ptr<lan::Session>& session) {
    auto events =
        std::static_pointer_cast<PeerEventHandler>(session->callback_owner);
    bool attached = !session->incoming;
    if (session->incoming) {
      std::shared_lock lock(remote_sessions_mutex_);
      attached = lan_controllers_.count(session->key) != 0;
    }
    if (events) {
      if (attached)
        events->ApplyConnectionStatus(MiniRtcConnectionStatus::MiniRtcConnectionClosed, session->key);
      events->StopAccepting();
      transfers_.Cancel(events.get());
    }
    if (session->incoming) {
      std::unique_lock lock(remote_sessions_mutex_);
      lan_controllers_.erase(session->key);
    } else if (auto props = FindRemoteSession(session->key))
      CloseRemoteSession(props);
  };
  try {
    lan_ = std::make_unique<lan::SessionManager>(
        path_manager_->GetConfigPath() / "lan", std::move(hooks));
  } catch (const std::exception& error) {
    lan_error_ = error.what();
    LOG_ERROR("LAN initialization failed: {}", error.what());
  }
}
void GuiRuntime::ApplyLanSettings() {
  if (config_center_->IsIpDirectEnabled()) {
    InitializeLan();
  } else {
    lan_connecting_.reset();
    lan_error_.clear();
    if (lan_ && !lan_stopping_) {
      lan_stopping_ = true;
      lan_->Stop();
    }
  }
}
bool GuiRuntime::LanAvailable() const {
  return lan_ && !lan_stopping_ && !stopping_;
}
void GuiRuntime::TickLan() {
  if (lan_) lan_->Tick();
  if (lan_ && lan_stopping_ && lan_->Quiescent()) {
    // Drain async authentication, media preparation and callbacks before
    // releasing the manager. A quick off/on waits here before rebinding.
    lan_.reset();
    lan_stopping_ = false;
    InitializeLan();
  }
}
void GuiRuntime::ConnectLan(const std::string& address, unsigned port,
                            std::string password, bool remember_password) {
  if (!LanAvailable() || (lan_connecting_ && !lan_connecting_->closing))
    return;
  try {
    auto props = std::make_shared<RemoteSession>();
    props->remember_password_ = remember_password;
    if (remember_password && lan::ValidPassword(password))
      std::memcpy(props->remote_password_, password.data(), password.size());
    auto session = lan_->Connect(address, port, std::move(password));
    lan_connecting_ = session;
    props->lan_session_ = session;
    props->remote_id_ = session->key;
    props->connection_status_ = MiniRtcConnectionStatus::MiniRtcConnectionConnecting;
    props->connection_auth_.Complete();
    {
      std::unique_lock lock(remote_sessions_mutex_);
      remote_sessions_[session->key] = props;
    }
    focused_remote_id_ = session->key;
    show_connection_status_window_ = false;
    lan_error_.clear();
  } catch (const std::exception& error) {
    lan_error_ = error.what();
  }
}
std::shared_ptr<PeerEventHandler> GuiRuntime::ControllerEvents(
    const std::string& id) {
  std::shared_lock lock(remote_sessions_mutex_);
  auto it = lan_controllers_.find(id);
  return it == lan_controllers_.end()
             ? (id.rfind("lan/", 0) == 0 ? nullptr : peer_events_)
             : it->second.events;
}
int GuiRuntime::WithController(
    const std::string& id,
    const std::function<int(MiniRtcPeerPtr*, const std::string&)>& operation,
    bool require_trusted) {
  if (require_trusted && !TrustedControllerAllowed(id)) return -1;
  std::shared_lock lock(remote_sessions_mutex_);
  auto it = lan_controllers_.find(id);
  auto events = it == lan_controllers_.end() ? peer_events_ : it->second.events;
  if (it == lan_controllers_.end() && id.rfind("lan/", 0) == 0) return -1;
  MiniRtcPeerPtr* peer =
      it == lan_controllers_.end() ? peer_ : it->second.session->peer;
  if (!peer || !events || !events->DataAllowed()) return -1;
  auto callback = events->EnterCallback();
  if (!callback) return -1;
  const auto wire_id = events->WireId(id);
  lock.unlock();
  return operation(peer, wire_id);
}
int GuiRuntime::SendControllerData(const std::string& id, const char* data,
                                   size_t size, const char* label) {
  return WithController(id, [&](MiniRtcPeerPtr* peer, const std::string& wire) {
    return PeerSendReliableDataFrameToPeer(peer, data, size, label, wire.data(),
                                       wire.size());
  });
}
std::vector<std::string> GuiRuntime::ConnectedControllers() {
  std::vector<std::string> ids;
  std::shared_lock lock(connection_status_mutex_);
  ids.reserve(connection_status_.size());
  for (const auto& [id, status] : connection_status_)
    if (status == MiniRtcConnectionStatus::MiniRtcConnectionConnected) ids.push_back(id);
  return ids;
}
void GuiRuntime::BeginShutdown() {
  if (stopping_) return;
  stopping_ = true;
  shutdown_report_at_ =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  if (lan_) lan_->Stop();
  CloseAllRemoteSessions();
  RestoreHostDisplayResolutions(true);
}
bool GuiRuntime::ShutdownDrained() {
  TickLan();
  HandleSessionCleanup();
  const bool drained =
      (!lan_ || lan_->Quiescent()) && !connection_peer_cleanup_.valid() &&
      session_cleanup_tasks_.empty() && controller_cleanup_tasks_.empty();
  if (drained) RestoreHostDisplayResolutions(true);
  if (!drained && std::chrono::steady_clock::now() >= shutdown_report_at_) {
    LOG_WARN(
        "Shutdown waiting: LAN [{}], cloud peer={}, viewers={}, controllers={}",
        lan_ ? lan_->PendingWork() : "none", connection_peer_cleanup_.valid(),
        session_cleanup_tasks_.size(), controller_cleanup_tasks_.size());
    shutdown_report_at_ =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
  }
  return drained;
}
}  // namespace crossdesk
