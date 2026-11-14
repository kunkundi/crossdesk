#include "runtime/gui_runtime.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <display_stream_id.h>
#include <web_clipboard_protocol.h>
#include "localization.h"
#include "platform/video_renderer.h"
#include "rd_log.h"
#ifdef _WIN32
#include "platform/windows/input/windows_input_injector.h"
#include "platform/windows/service/autostart_control.h"
#include "service_host.h"
#endif
#if defined(__linux__) && !defined(__OHOS__)
#include "platform/linux/headless/headless_console.h"
#endif

namespace crossdesk {

GuiRuntime::GuiRuntime()
    : clipboard_(*this), devices_(*this), transfers_(*this), settings_(*this),
      keyboard_(*this),
      remote_input_(
          [this](const std::string& id, const RemoteAction& action) {
#ifdef _WIN32
            ScopedUserDesktopInputThread input_thread;
#endif
            return ApplyRemoteInput(id, action);
          },
          [this](const std::string& id) {
#ifdef _WIN32
            ScopedUserDesktopInputThread input_thread;
#endif
            const bool keys_released =
                keyboard_.ReleaseRemotePressedKeys(id, "input_queue_reset");
            const bool mouse_released = devices_.ReleaseRemoteMouseButtons(id);
            std::lock_guard lock(remote_pointer_input_mutex_);
            last_remote_pointer_input_time_.erase(id);
            return keys_released && mouse_released ? InputOutcome::applied
                                                   : InputOutcome::failed;
          },
          [](const std::string& id, int64_t queued_ms, int64_t execution_ms,
             InputOutcome outcome) {
            LOG_DEBUG("Remote input result for peer={}", id);
            LOG_WARN(
                "Remote input result: queue_ms={} execution_ms={} result={}",
                queued_ms, execution_ms, InputOutcomeName(outcome));
          },
          [this] { return devices_.ResetToInitialDisplay(); },
          [this](const std::string& id, unsigned mask) {
#ifdef _WIN32
            ScopedUserDesktopInputThread input_thread;
#endif
            return devices_.ReleaseRemoteMouseButtons(id, mask)
                       ? InputOutcome::applied : InputOutcome::failed;
          }) {}

GuiRuntime::~GuiRuntime() {
  {
    std::lock_guard lock(automation_mutex_);
    automation_closing_ = true;
    automation_tasks_.clear();
  }
  remote_input_.Stop();
#if _WIN32
  windows_rdp_handoff_allowed_->store(false, std::memory_order_relaxed);
  windows_rdp_handoff_connected_->store(false, std::memory_order_relaxed);
  if (windows_service_worker_) windows_service_worker_->Reset();
#endif
  lan_.reset();
  transfers_.Shutdown();
  WaitForSessionCleanup();
  RestoreHostDisplayResolutions(true);
}

void GuiRuntime::SendRemoteInput(const RemoteAction& action) {
  const bool mouse = action.type == ControlType::mouse;
  const bool mouse_move = mouse && action.m.flag == MouseFlag::move;
  if (!mouse && action.type != ControlType::keyboard &&
      action.type != ControlType::keyboard_state)
    return;
  const std::string& target_id = controlled_remote_id_.empty()
      ? focused_remote_id_ : controlled_remote_id_;
  if (target_id.empty()) return;
  const bool group = gallery_group_control_id_ == target_id;
  const bool releasing = mouse
      ? action.m.flag == MouseFlag::left_up ||
        action.m.flag == MouseFlag::right_up ||
        action.m.flag == MouseFlag::middle_up
      : action.type == ControlType::keyboard
          ? action.k.flag == KeyFlag::key_up : action.ks.pressed_count == 0;
  const auto available = [](const auto& props) {
    return props && props->peer_ && !props->closing_ &&
           props->connection_status_ == MiniRtcConnectionStatus::MiniRtcConnectionConnected;
  };
  std::shared_lock lock(remote_sessions_mutex_);
  const auto source = remote_sessions_.find(target_id);
  // A lost leader must stop new input, but its followers still need key/button
  // releases before group control is cleared by the UI.
  if (!releasing &&
      (source == remote_sessions_.end() || !available(source->second)))
    return;
  const auto message = action.to_json();
  for (const auto& [id, props] : remote_sessions_) {
    if ((!group && id != target_id) || !available(props)) continue;
    if (mouse) {
      std::lock_guard cursor_lock(props->remote_cursor_state_mutex_);
      props->cursor_presentation_.NoteInput(SDL_GetTicks());
    }
    // Reuse exactly the leader's mapped coordinates and key metadata. Neither
    // gallery filters nor the follower's preview dimensions affect input.
    // Move updates supersede old positions and must not wait for KCP retries.
    // Use the negotiated unreliable stream; clicks and wheel events remain
    // on the reliable mouse stream, with their own absolute coordinates.
    const int result = mouse_move
        ? MiniRtcSendDataFrame(props->peer_, message.data(), message.size(),
                        props->data_label_.c_str())
        : MiniRtcSendReliableDataFrame(
              props->peer_, message.data(), message.size(),
              mouse ? props->mouse_label_.c_str() : props->keyboard_label_.c_str());
    if (result != 0) LOG_WARN("Send remote input failed, ret={}", result);
  }
}

bool GuiRuntime::PrepareSharedGuiIdentity(bool import_existing) {
#ifdef _WIN32
  if (!SupportsServiceAutostart() || shared_identity_) return true;
  if (!import_existing && settings_.Save() != 0) return false;
  if (peer_) {
    CloseConnectionPeer();
    WaitForSessionCleanup();
  }
  std::string error;
  if (!import_existing && !SharedHostIdentity::Prepare(
      path_manager_->GetCachePath(), path_manager_->GetConfigPath() / "trusted-devices",
      config_center_->ExportSharedHostConfig(), error)) {
    LOG_ERROR("Cannot prepare persistent host: {}", error);
    return false;
  }
  auto identity = SharedHostIdentity::AttachGui(path_manager_->GetCachePath(),
      path_manager_->GetConfigPath() / "trusted-devices", import_existing, error);
  if (!identity ||
      (import_existing && config_center_->ApplySharedHostConfig(identity->HostConfig()) != 0)) {
    LOG_ERROR("Cannot load the machine identity: {}", error);
    return false;
  }
  shared_identity_ = std::move(identity);
  managed_host_frontend_ = true;
  StartCrossDeskService();
#else
  (void)import_existing;
#endif
  return true;
}

bool GuiRuntime::PublishSharedIdentity(bool include_config) {
#ifdef _WIN32
  if (managed_host_frontend_) {
    if (!include_config) return true;
    pending_local_config_ =
        {{"op", "config"}, {"config", config_center_->ExportSharedHostConfig()},
         {"privacy_on_connect", config_center_->IsEnablePrivacyScreen()},
         {"request", ++next_local_config_request_}};
    local_config_sent_ = false;
    return true;  // Tick sends after startup/reattachment; errors return to the UI.
  }
  if (shared_identity_) {
    local_host_profile_dirty_ = true;
    std::string error;
    if (!shared_identity_->Publish(include_config ? config_center_->ExportSharedHostConfig()
                                                 : std::string{}, error)) {
      LOG_ERROR("Cannot synchronize the shared device identity: {}", error);
      return false;
    }
  }
#else
  (void)include_config;
#endif
  return true;
}

int GuiRuntime::CreateConnectionPeer() {
  // Tick retries after the old peer's leave notification and socket shutdown.
  if (connection_peer_cleanup_.valid()) return 0;
  {
    std::unique_lock lock(remote_sessions_mutex_);
    peer_events_ = std::make_shared<PeerEventHandler>(*this);
  }
  params_.use_cfg_file = false;

  std::string signal_server_ip;
  int signal_server_port;

  if (config_center_->IsSelfHosted()) {
    signal_server_ip = config_center_->GetSignalServerHost();
    signal_server_port = config_center_->GetSignalServerPort();

    std::string current_self_hosted_ip = config_center_->GetSignalServerHost();
    bool use_cached_id = false;
#if defined(__linux__) && !defined(__OHOS__)
    try {
      use_cached_id = settings_.LoadCachedSelfHostedIdentity();
    } catch (const std::system_error&) {
      // Keep the cached identity and avoid requesting a replacement identity
      // when its file is temporarily inaccessible. SettingsManager logs why.
      return -1;
    }
#else
    use_cached_id = settings_.LoadCachedSelfHostedIdentity();
#endif
    if (!use_cached_id) {
      LOG_INFO(
          "secure_cache_v2.enc not found, will use empty id to get new id from "
          "server");
    }

    if (use_cached_id && strlen(self_hosted_id_) > 0) {
      memset(&self_hosted_user_id_, 0, sizeof(self_hosted_user_id_));
      strncpy(self_hosted_user_id_, self_hosted_id_,
              sizeof(self_hosted_user_id_) - 1);
      self_hosted_user_id_[sizeof(self_hosted_user_id_) - 1] = '\0';
      params_.user_id = self_hosted_user_id_;
    } else {
      memset(&self_hosted_user_id_, 0, sizeof(self_hosted_user_id_));
      params_.user_id = self_hosted_user_id_;
      LOG_INFO(
          "Using empty id for self-hosted server, server will assign new id");
    }
  } else {
    signal_server_ip = config_center_->GetDefaultServerHost();
    signal_server_port = config_center_->GetDefaultSignalServerPort();
    settings_.ActivateCachedPublicIdentity();
    params_.user_id = client_id_with_password_;
  }

  const bool self_hosted = config_center_->IsSelfHosted();
  const std::string pending_identity =
      settings_.PendingPasswordChangeIdentity(
          self_hosted, signal_server_ip, signal_server_port);
  bool try_pending_identity = false;
  {
    std::lock_guard<std::mutex> lock(password_change_mutex_);
    if (pending_identity.empty()) {
      credential_recovery_in_progress_ = false;
      credential_recovery_attempt_pending_ = false;
      credential_recovery_action_ = CredentialRecoveryAction::Idle;
    } else if (!credential_recovery_in_progress_) {
      credential_recovery_in_progress_ = true;
      credential_recovery_attempt_pending_ = true;
      LOG_INFO("Recovering an interrupted password change");
    }
    try_pending_identity = credential_recovery_in_progress_ &&
                           credential_recovery_attempt_pending_ &&
                           !pending_identity.empty();
  }

  const char *active_identity =
      self_hosted ? self_hosted_user_id_ : client_id_with_password_;
#if defined(__linux__) && !defined(__OHOS__)
  // Publish the committed identity; an unconfirmed recovery credential must
  // not be advertised as the current connection password.
  HeadlessConsole::Instance().SetIdentity(active_identity);
#endif
  const std::string login_identity =
      try_pending_identity ? pending_identity : std::string(active_identity);
  std::memset(connection_login_identity_, 0,
              sizeof(connection_login_identity_));
  std::strncpy(connection_login_identity_, login_identity.c_str(),
               sizeof(connection_login_identity_) - 1);
  params_.user_id = connection_login_identity_;

  // self hosted server config
  strncpy(signal_server_ip_self_, config_center_->GetSignalServerHost().c_str(),
          sizeof(signal_server_ip_self_) - 1);
  signal_server_ip_self_[sizeof(signal_server_ip_self_) - 1] = '\0';
  int signal_port = config_center_->GetSignalServerPort();
  if (signal_port > 0) {
    strncpy(signal_server_port_self_, std::to_string(signal_port).c_str(),
            sizeof(signal_server_port_self_) - 1);
    signal_server_port_self_[sizeof(signal_server_port_self_) - 1] = '\0';
  } else {
    signal_server_port_self_[0] = '\0';
  }
  // peer config
  strncpy((char *)params_.signal_server_ip, signal_server_ip.c_str(),
          sizeof(params_.signal_server_ip) - 1);
  params_.signal_server_ip[sizeof(params_.signal_server_ip) - 1] = '\0';
  params_.signal_server_port = signal_server_port;
  strncpy(params_.log_path, dll_log_path_.c_str(),
          sizeof(params_.log_path) - 1);
  params_.log_path[sizeof(params_.log_path) - 1] = '\0';
  // MiniRTC selects the available hardware backend and falls back to software.
  params_.hardware_acceleration = true;
#if defined(_WIN32) || (defined(__linux__) && !defined(__OHOS__)) || defined(__APPLE__)
  // Windows and Linux retain pooled CPU NV12 frames or CUDA device frames;
  // macOS retains VideoToolbox CVPixelBuffers for direct Metal sampling. All
  // platforms fall back to a packed CPU copy when native upload is unavailable.
  params_.native_video_output = true;
#else
  params_.native_video_output = false;
#endif
  params_.av1_encoding = config_center_->GetVideoEncodeFormat() ==
                                 ConfigCenter::VIDEO_ENCODE_FORMAT::AV1
                             ? true
                             : false;
  params_.turn_mode = static_cast<MiniRtcTurnMode>(config_center_->GetTurnMode());
  params_.direct_start_bitrate_bps = config_center_->GetDirectStartBitrateBps();
  params_.relay_start_bitrate_bps = config_center_->GetRelayStartBitrateBps();
  params_.disable_receiver_padding_probing =
      config_center_->IsReceiverPaddingProbingDisabled();
  params_.enable_srtp = config_center_->IsEnableSrtp();
  params_.video_content_type = MiniRtcVideoContentType::ScreenContent;
  // Bootstrap outgoing media with fixed defaults. Live overrides come only
  // from the authorized controller and apply to its connection on this host.
  params_.video_quality = MiniRtcVideoQuality::MiniRtcVideoQualityHigh;
  params_.video_frame_rate = 60;
  params_.video_degradation_preference =
      MiniRtcVideoDegradationPreference::MaintainResolution;
  params_.on_receive_video_buffer = nullptr;
  params_.on_receive_audio_buffer = PeerEventHandler::OnReceiveAudioBuffer;
  params_.on_receive_data_buffer = PeerEventHandler::OnReceiveDataBuffer;

  params_.on_receive_video_frame = PeerEventHandler::OnReceiveVideoBuffer;

  params_.on_signal_status = PeerEventHandler::OnSignalStatus;
  params_.on_signal_message = PeerEventHandler::OnSignalMessage;
  params_.on_signal_exchange = PeerEventHandler::OnSignalExchange;
  params_.on_connection_status = PeerEventHandler::OnConnectionStatus;
  params_.on_net_status_report = PeerEventHandler::OnNetStatusReport;

  params_.user_data = peer_events_.get();

  // The previous peer may have left a terminal status behind. Reset it before
  // Init() starts emitting callbacks for the newly selected server.
  signal_connected_ = false;
  device_presence_cache_.SetSignalConnected(false);
  need_to_send_recent_connections_ = true;
  signal_status_ = MiniRtcSignalStatus::MiniRtcSignalConnecting;

  // Complete slow headless display setup before connecting to signaling.
  if (unattended_host_ && devices_.EnsureCaptureReady() != 0) return -1;
  {
    std::unique_lock lock(remote_sessions_mutex_);
    peer_ = MiniRtcCreatePeer(&params_);
  }
  if (peer_) {
    LOG_INFO("Create peer instance successful");
    LOG_DEBUG("Local client id: [{}]", client_id_);
#ifdef _WIN32
    if (managed_host_frontend_) {
      if (!local_host_) local_host_ = std::make_shared<LocalHostChannel>(false);
      std::weak_ptr<LocalHostChannel> channel = local_host_;
      BindLocalPeer(peer_, [this, channel](nlohmann::json message) {
        if (message.contains("id")) {
          std::lock_guard lock(local_host_state_mutex_);
          const auto found = mirrored_controllers_.find(message.at("id").get<std::string>());
          if (found == mirrored_controllers_.end()) return false;
          message["epoch"] = found->second;
        }
        const auto connection = channel.lock();
        return connection && connection->Send(message);
      });
      local_host_->Send({{"op", "hello"},
          {"privacy_on_connect", config_center_->IsEnablePrivacyScreen()},
          {"privacy_requested", privacy_.Requested() || config_center_->IsEnablePrivacyScreen()}});
      // Outgoing viewer/direct sessions still register their native streams
      // from the user's local display metadata; this does not start capture.
      devices_.EnsureCaptureReady();
      LOG_INFO("GUI attached to persistent host; no second signaling login");
      return 0;
    }
#endif
    MiniRtcInit(peer_);
    LOG_INFO("Peer initialization finished");
  } else {
    LOG_INFO("Create peer instance failed");
  }

  if (0 == devices_.EnsureCaptureReady()) {
    const auto &displays = devices_.display_info_list();
    for (size_t index = 0; index < displays.size(); ++index) {
      const std::string stream_id = MakeDisplayStreamId(index);
      MiniRtcAddVideoStream(peer_, stream_id.c_str());
    }

    MiniRtcAddAudioStream(peer_, audio_label_.c_str());
    MiniRtcAddDataStream(peer_, data_label_.c_str(), false);
    MiniRtcAddDataStream(peer_, mouse_label_.c_str(), true);
    MiniRtcAddDataStream(peer_, keyboard_label_.c_str(), true);
    MiniRtcAddDataStream(peer_, control_data_label_.c_str(), true);
    MiniRtcAddDataStream(peer_, file_label_.c_str(), true);
    MiniRtcAddDataStream(peer_, file_feedback_label_.c_str(), true);
    MiniRtcAddDataStream(peer_, clipboard_label_.c_str(), true);
    MiniRtcAddDataStream(peer_, web_clipboard::kStream, true);
    return 0;
  } else {
    return -1;
  }
}

void GuiRuntime::HandleRecentConnections() {
  if (thumbnail_ && session_cleanup_tasks_.empty()) {
    std::vector<std::shared_ptr<RemoteSession>> sessions;
    {
      std::shared_lock lock(remote_sessions_mutex_);
      for (const auto& [_, props] : remote_sessions_)
        if (props && !props->closing_ && props->was_connected_ &&
            props->recent_connection_dirty_.exchange(false))
          sessions.push_back(props);
    }
    for (const auto& props : sessions) {
      std::string host_name;
      HostPlatform platform;
      {
        std::lock_guard lock(props->remote_version_mutex_);
        host_name = props->remote_host_name_;
        platform = props->recent_platform_;
      }
      // Record a successful connection before a preview or host info arrives.
      // The close path still updates its final thumbnail. Remember password
      // controls only credential storage, not whether this device is listed.
      const auto direct = props->lan_session_;
      const Thumbnail::LanTarget target =
          direct ? Thumbnail::LanTarget{direct->target.address,
                                        direct->target.device_id,
                                        direct->target.port}
                 : Thumbnail::LanTarget{};
      const auto record_id =
          direct ? Thumbnail::LanRecordId(target.device_id) : props->remote_id_;
      if (thumbnail_->SaveToThumbnail(
              nullptr, 0, 0, record_id, host_name,
              props->remember_password_ ? props->remote_password_ : "",
              platform, direct ? &target : nullptr) == 0)
        reload_recent_connections_ = true;
      else
        LOG_ERROR("Failed to save recent connection");
    }
  }
  if (session_cleanup_tasks_.empty() && reload_recent_connections_ && thumbnail_) {
    uint32_t now_time = SDL_GetTicks();
    if (now_time - recent_connection_image_save_time_ >= 50) {
      int ret = thumbnail_->LoadThumbnail(recent_connections_);
      if (!ret) {
        LOG_INFO("Load recent connection thumbnails");
      }
      reload_recent_connections_ = false;

      recent_connection_ids_.clear();
      for (const auto &conn : recent_connections_) {
        if (conn.second.lan.device_id.empty())
          recent_connection_ids_.push_back(conn.first);
      }
      need_to_send_recent_connections_ = true;
    }
  }
}


void GuiRuntime::SdlCaptureAudioIn(void *userdata, Uint8 *stream, int len) {
  GuiRuntime *runtime = static_cast<GuiRuntime *>(userdata);
  if (!runtime) {
    return;
  }

  if (1) {
    std::shared_lock lock(runtime->remote_sessions_mutex_);
    uint64_t captured_timestamp = 0;
    for (const auto &it : runtime->remote_sessions_) {
      auto props = it.second;
      if (props->connection_status_.load() == MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
        if (props->peer_) {
          if (captured_timestamp == 0) {
            const int64_t timestamp_us = MiniRtcGetSystemTimeMicros(props->peer_);
            if (timestamp_us > 0) {
              captured_timestamp = static_cast<uint64_t>(timestamp_us);
            }
          }
          MiniRtcAudioFrame frame{};
          frame.data = reinterpret_cast<const char *>(stream);
          frame.size = static_cast<size_t>(len);
          frame.captured_timestamp = captured_timestamp;
          MiniRtcSendAudioFrame(props->peer_, &frame,
                         runtime->audio_label_.c_str());
        }
      }
    }

  } else {
    memcpy(runtime->audio_buffer_, stream, len);
    runtime->audio_len_ = len;
    SDL_Delay(10);
    runtime->audio_buffer_fresh_ = true;
  }
}

void GuiRuntime::SdlCaptureAudioOut([[maybe_unused]] void *userdata,
                                    [[maybe_unused]] Uint8 *stream,
                                    [[maybe_unused]] int len) {
}

} // namespace crossdesk
