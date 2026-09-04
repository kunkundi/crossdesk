#include "runtime/gui_runtime.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include <display_stream_id.h>
#include "localization.h"
#include "platform/video_renderer.h"
#include "rd_log.h"

namespace crossdesk {

GuiRuntime::GuiRuntime()
    : clipboard_(*this), devices_(*this), transfers_(*this), settings_(*this),
      keyboard_(*this), peer_events_(*this) {}

GuiRuntime::~GuiRuntime() = default;

int GuiRuntime::CreateConnectionPeer() {
  params_.use_cfg_file = false;

  std::string signal_server_ip;
  int signal_server_port;
  int coturn_server_port;

  if (config_center_->IsSelfHosted()) {
    signal_server_ip = config_center_->GetSignalServerHost();
    signal_server_port = config_center_->GetSignalServerPort();
    coturn_server_port = config_center_->GetCoturnServerPort();

    std::string current_self_hosted_ip = config_center_->GetSignalServerHost();
    const bool use_cached_id = settings_.LoadCachedSelfHostedIdentity();
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
    coturn_server_port = config_center_->GetDefaultCoturnServerPort();
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
      credential_recovery_retry_active_ = false;
      credential_recovery_promote_pending_ = false;
      credential_recovery_clear_pending_ = false;
    } else if (!credential_recovery_in_progress_) {
      credential_recovery_in_progress_ = true;
      credential_recovery_attempt_pending_ = true;
      LOG_INFO("Recovering an interrupted password change for [{}]",
               client_id_);
    }
    try_pending_identity = credential_recovery_in_progress_ &&
                           credential_recovery_attempt_pending_ &&
                           !pending_identity.empty();
  }

  const char *active_identity =
      self_hosted ? self_hosted_user_id_ : client_id_with_password_;
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
  int coturn_port = config_center_->GetCoturnServerPort();
  if (coturn_port > 0) {
    strncpy(coturn_server_port_self_, std::to_string(coturn_port).c_str(),
            sizeof(coturn_server_port_self_) - 1);
    coturn_server_port_self_[sizeof(coturn_server_port_self_) - 1] = '\0';
  } else {
    coturn_server_port_self_[0] = '\0';
  }

  // peer config
  strncpy((char *)params_.signal_server_ip, signal_server_ip.c_str(),
          sizeof(params_.signal_server_ip) - 1);
  params_.signal_server_ip[sizeof(params_.signal_server_ip) - 1] = '\0';
  params_.signal_server_port = signal_server_port;
  strncpy((char *)params_.stun_server_ip, signal_server_ip.c_str(),
          sizeof(params_.stun_server_ip) - 1);
  params_.stun_server_ip[sizeof(params_.stun_server_ip) - 1] = '\0';
  params_.stun_server_port = coturn_server_port;
  strncpy((char *)params_.turn_server_ip, signal_server_ip.c_str(),
          sizeof(params_.turn_server_ip) - 1);
  params_.turn_server_ip[sizeof(params_.turn_server_ip) - 1] = '\0';
  params_.turn_server_port = coturn_server_port;
  // TURN credentials are issued by the signaling server after login. Keep the
  // initial values empty so a reusable static password is never embedded in
  // the client binary.
  params_.turn_server_username[0] = '\0';
  params_.turn_server_password[0] = '\0';

  strncpy(params_.log_path, dll_log_path_.c_str(),
          sizeof(params_.log_path) - 1);
  params_.log_path[sizeof(params_.log_path) - 1] = '\0';
  params_.hardware_acceleration = config_center_->IsHardwareVideoCodec();
#if defined(_WIN32) || defined(__APPLE__)
  // Windows renderers retain pooled CPU NV12 frames or CUDA device frames;
  // macOS retains VideoToolbox CVPixelBuffers for direct Metal sampling. Both
  // platforms fall back to a packed CPU copy when native upload is unavailable.
  params_.native_video_output = true;
#else
  params_.native_video_output = false;
#endif
  params_.av1_encoding = config_center_->GetVideoEncodeFormat() ==
                                 ConfigCenter::VIDEO_ENCODE_FORMAT::AV1
                             ? true
                             : false;
  params_.turn_mode = static_cast<TurnMode>(config_center_->GetTurnMode());
  params_.enable_srtp = config_center_->IsEnableSrtp();
  params_.video_content_type = VideoContentType::ScreenContent;
  params_.video_quality =
      static_cast<VideoQuality>(config_center_->GetVideoQuality());
  params_.video_frame_rate =
      config_center_->GetVideoFrameRate() ==
              ConfigCenter::VIDEO_FRAME_RATE::FPS_30
          ? 30
          : 60;
  switch (config_center_->GetVideoAdaptationPolicy()) {
    case ConfigCenter::VIDEO_ADAPTATION_POLICY::FRAME_RATE_PRIORITY:
      params_.video_degradation_preference =
          VideoDegradationPreference::MaintainFrameRate;
      break;
    case ConfigCenter::VIDEO_ADAPTATION_POLICY::BALANCED:
      params_.video_degradation_preference =
          VideoDegradationPreference::Balanced;
      break;
    case ConfigCenter::VIDEO_ADAPTATION_POLICY::QUALITY_PRIORITY:
    default:
      params_.video_degradation_preference =
          VideoDegradationPreference::MaintainResolution;
      break;
  }
  params_.on_receive_video_buffer = nullptr;
  params_.on_receive_audio_buffer = PeerEventHandler::OnReceiveAudioBuffer;
  params_.on_receive_data_buffer = PeerEventHandler::OnReceiveDataBuffer;

  params_.on_receive_video_frame = PeerEventHandler::OnReceiveVideoBuffer;

  params_.on_signal_status = PeerEventHandler::OnSignalStatus;
  params_.on_signal_message = PeerEventHandler::OnSignalMessage;
  params_.on_connection_status = PeerEventHandler::OnConnectionStatus;
  params_.on_net_status_report = PeerEventHandler::OnNetStatusReport;

  params_.user_data = &peer_events_;

  // The previous peer may have left a terminal status behind. Reset it before
  // Init() starts emitting callbacks for the newly selected server.
  signal_connected_ = false;
  signal_status_ = SignalStatus::SignalConnecting;

  peer_ = CreatePeer(&params_);
  if (peer_) {
    LOG_INFO("Create peer instance [{}] successful", client_id_);
    Init(peer_);
    LOG_INFO("Peer [{}] init finish", client_id_);
  } else {
    LOG_INFO("Create peer [{}] instance failed", client_id_);
  }

  if (0 == devices_.InitializeScreenCapturer()) {
    const auto &displays = devices_.display_info_list();
    for (size_t index = 0; index < displays.size(); ++index) {
      const std::string stream_id = MakeDisplayStreamId(index);
      AddVideoStream(peer_, stream_id.c_str());
    }

    AddAudioStream(peer_, audio_label_.c_str());
    AddDataStream(peer_, data_label_.c_str(), false);
    AddDataStream(peer_, mouse_label_.c_str(), false);
    AddDataStream(peer_, keyboard_label_.c_str(), true);
    AddDataStream(peer_, control_data_label_.c_str(), true);
    AddDataStream(peer_, file_label_.c_str(), true);
    AddDataStream(peer_, file_feedback_label_.c_str(), true);
    AddDataStream(peer_, clipboard_label_.c_str(), true);
    return 0;
  } else {
    return -1;
  }
}

void GuiRuntime::UpdateLabels() {
  if (!label_inited_ ||
      localization_language_index_last_ != localization_language_index_) {
    connect_button_label_ =
        connect_button_pressed_
            ? localization::disconnect[localization_language_index_]
            : localization::connect[localization_language_index_];
    label_inited_ = true;
    localization_language_index_last_ = localization_language_index_;
  }
}


void GuiRuntime::HandleRecentConnections() {
  if (reload_recent_connections_ && thumbnail_) {
    uint32_t now_time = SDL_GetTicks();
    if (now_time - recent_connection_image_save_time_ >= 50) {
      int ret = thumbnail_->LoadThumbnail(recent_connections_,
                                          &recent_connection_image_width_,
                                          &recent_connection_image_height_);
      if (!ret) {
        LOG_INFO("Load recent connection thumbnails");
      }
      reload_recent_connections_ = false;

      recent_connection_ids_.clear();
      for (const auto &conn : recent_connections_) {
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
      if (props->connection_status_.load() == ConnectionStatus::Connected) {
        if (props->peer_) {
          if (captured_timestamp == 0) {
            const int64_t timestamp_us = GetSystemTimeMicros(props->peer_);
            if (timestamp_us > 0) {
              captured_timestamp = static_cast<uint64_t>(timestamp_us);
            }
          }
          MiniRtcAudioFrame frame{};
          frame.data = reinterpret_cast<const char *>(stream);
          frame.size = static_cast<size_t>(len);
          frame.captured_timestamp = captured_timestamp;
          SendAudioFrame(props->peer_, &frame,
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
