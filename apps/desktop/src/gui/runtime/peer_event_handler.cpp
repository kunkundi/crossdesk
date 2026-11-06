#include "runtime/peer_event_handler.h"

#include <remote_action.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "device_controller.h"
#include "file_transfer.h"
#include "localization.h"
#include "platform.h"
#include "platform/video_renderer.h"
#include "rd_log.h"
#include "runtime/gui_runtime.h"
#if defined(__linux__) && !defined(__OHOS__)
#include "platform/linux/headless/headless_console.h"
#endif

#if _WIN32
#include "interactive_state.h"
#include "service_host.h"
#endif

namespace crossdesk {
namespace {

#if defined(_WIN32)
constexpr HostPlatform kClientPlatform = HostPlatform::Windows;
#elif defined(__APPLE__)
constexpr HostPlatform kClientPlatform = HostPlatform::MacOS;
#elif defined(__OHOS__)
constexpr HostPlatform kClientPlatform = HostPlatform::HarmonyOS;
#elif (defined(__linux__) && !defined(__OHOS__))
constexpr HostPlatform kClientPlatform = HostPlatform::Linux;
#else
constexpr HostPlatform kClientPlatform = HostPlatform::Unknown;
#endif

}  // namespace

std::string PeerEventHandler::ConnectionKey(const char* id, size_t size) const {
  if (auto session = direct_.lock()) return session->key;
  return direct_bound_ ? std::string{} : std::string(id, size);
}

std::string PeerEventHandler::WireId(const std::string& key) const {
  if (auto session = direct_.lock()) return session->remote_id;
  return key;
}

bool PeerEventHandler::DataAllowed() const {
  if (!direct_bound_) return true;
  auto session = direct_.lock();
  return session && session->authorized && !session->closing;
}

void PeerEventHandler::SendClientInfo(MiniRtcPeerPtr* peer,
                                      const std::string& client_id) {
  if (!peer) {
    return;
  }

  const nlohmann::json message = {{"type", "client_info"},
                                  {"version", CROSSDESK_VERSION},
                                  {"platform", HostPlatformName(kClientPlatform)}};
  const std::string payload = message.dump();
  if (MiniRtcSendSignalMessage(peer, payload.data(), payload.size()) != 0) {
    LOG_WARN("[{}] failed to report client information", client_id);
  }
}

PeerEventHandler::PeerEventHandler(GuiRuntime& owner) : owner_(owner) {
  owner_.transfers_.Connected(this, "");
}

int PeerEventHandler::OnSignalExchange(MiniRtcSignalExchangeStage stage, const char* message,
    size_t size, char* response, size_t capacity, void* user_data) {
  auto* handler = static_cast<PeerEventHandler*>(user_data);
  if (!handler || !message) return -1;
  auto callback = handler->EnterCallback();
  if (!callback) return -1;
  if (stage == MiniRtcSignalExchangeStage::IncomingMessage &&
      !handler->device_login_.Pending()) return 0;
  try {
    return handler->device_login_.Exchange(stage,
        nlohmann::json::parse(message, message + size),
        handler->owner_.trusted_devices_.get(), response, capacity);
  } catch (const std::exception&) {
    LOG_ERROR("Device login protocol or identity unavailable");
    return -1;
  }
}

void PeerEventHandler::OnSignalMessage(const char* message, size_t size,
                                       void* user_data) {
  auto* handler = static_cast<PeerEventHandler*>(user_data);
  if (!handler) return;
  auto callback = handler->EnterCallback();
  if (!callback) return;
  GuiRuntime* runtime = &handler->owner_;
  if (!message || size == 0) {
    return;
  }
  std::string s(message, size);
  auto j = nlohmann::json::parse(s, nullptr, false);
  if (j.is_discarded() || !j.contains("type") || !j["type"].is_string()) {
    return;
  }
  std::string type = j["type"].get<std::string>();
  if (type == "user_join_transmission" && j.contains("status") &&
      j["status"] == "failed" && j.contains("transmission_id") &&
      j["transmission_id"].is_string()) {
    const auto props = runtime->FindRemoteSession(j["transmission_id"].get<std::string>());
    if (props && !props->closing_) props->connection_auth_.Rejected(j);
  } else if (type == "announcements_changed") {
    runtime->announcements_.Refresh();
  } else if (type == "announcements") {
    runtime->announcements_.Receive(j);
  } else if (type == "presence") {
    if (j.contains("devices") && j["devices"].is_array()) {
      for (auto& dev : j["devices"]) {
        if (!dev.is_object()) {
          continue;
        }
        if (!dev.contains("id") || !dev["id"].is_string()) {
          continue;
        }
        if (!dev.contains("online") || !dev["online"].is_boolean()) {
          continue;
        }
        std::string id = dev["id"].get<std::string>();
        bool online = dev["online"].get<bool>();
        runtime->device_presence_cache_.SetOnline(id, online);
        {
          std::lock_guard<std::mutex> lock(
              runtime->pending_presence_probe_mutex_);
          if (runtime->pending_presence_probe_ &&
              runtime->pending_presence_remote_id_ == id) {
            runtime->pending_presence_result_ready_ = true;
            runtime->pending_presence_online_ = online;
          }
        }
      }
    }
  } else if (type == "presence_update") {
    if (j.contains("id") && j["id"].is_string() && j.contains("online") &&
        j["online"].is_boolean()) {
      std::string id = j["id"].get<std::string>();
      bool online = j["online"].get<bool>();
      if (!id.empty()) {
        runtime->device_presence_cache_.SetOnline(id, online);
        {
          std::lock_guard<std::mutex> lock(
              runtime->pending_presence_probe_mutex_);
          if (runtime->pending_presence_probe_ &&
              runtime->pending_presence_remote_id_ == id) {
            runtime->pending_presence_result_ready_ = true;
            runtime->pending_presence_online_ = online;
          }
        }
      }
    }
  } else if (type == "change_password") {
    std::lock_guard<std::mutex> lock(runtime->password_change_mutex_);
    if (!runtime->password_change_pending_) {
      LOG_WARN("Ignore unexpected password change response");
      return;
    }

    if (!j.contains("request_id") || !j["request_id"].is_string() ||
        j["request_id"].get<std::string>() !=
            runtime->pending_password_change_request_id_) {
      LOG_WARN("Ignore password change response with unexpected request id");
      return;
    }

    if (j.contains("user_id") && j["user_id"].is_string()) {
      const std::string response_user_id = j["user_id"].get<std::string>();
      if (!response_user_id.empty() &&
          response_user_id != runtime->client_id_) {
        LOG_WARN("Ignore password change response for unexpected id [{}]",
                 response_user_id);
        return;
      }
    }

    runtime->password_change_succeeded_ =
        j.contains("status") && j["status"].is_string() &&
        j["status"].get<std::string>() == "success";
    runtime->password_change_error_ =
        j.contains("reason") && j["reason"].is_string()
            ? j["reason"].get<std::string>()
            : "Password change failed";
    // A rejected replay cannot tell us whether the original request committed.
    runtime->password_change_result_uncertain_ = runtime->password_change_recovery_ &&
                                                !runtime->password_change_succeeded_;
    runtime->password_change_result_ready_ = true;
  }
}

void PeerEventHandler::OnSignalStatus(MiniRtcSignalStatus status, const char* user_id,
                                      size_t user_id_size, void* user_data) {
  using RecoveryAction = GuiRuntime::CredentialRecoveryAction;
  auto* handler = static_cast<PeerEventHandler*>(user_data);
  if (!handler) return;
  auto callback = handler->EnterCallback();
  if (!callback) return;
  GuiRuntime* runtime = &handler->owner_;

  std::string client_id(user_id, user_id_size);
  if (client_id == runtime->client_id_) {
    runtime->announcements_.SetConnected(status == MiniRtcSignalStatus::MiniRtcSignalConnected);
    runtime->signal_status_ = status;
    runtime->device_presence_cache_.SetSignalConnected(
        status == MiniRtcSignalStatus::MiniRtcSignalConnected);
    if (MiniRtcSignalStatus::MiniRtcSignalConnecting == status) {
      runtime->signal_connected_ = false;
    } else if (MiniRtcSignalStatus::MiniRtcSignalConnected == status) {
      runtime->signal_connected_ = true;
      runtime->need_to_send_recent_connections_ = true;
      LOG_INFO("[{}] connected to signal server", client_id);
      SendClientInfo(runtime->peer_, client_id);
      std::lock_guard<std::mutex> lock(runtime->password_change_mutex_);
      if (runtime->credential_recovery_in_progress_) {
        if (!handler->device_login_.UsedPassword()) {
          runtime->credential_recovery_action_ = RecoveryAction::ResumePending;
        } else if (runtime->credential_recovery_attempt_pending_) {
          runtime->credential_recovery_action_ = RecoveryAction::PromotePending;
        } else {
          runtime->credential_recovery_action_ = RecoveryAction::ClearPending;
        }
      }
    } else if (MiniRtcSignalStatus::MiniRtcSignalFailed == status) {
      runtime->signal_connected_ = false;
      std::lock_guard<std::mutex> lock(runtime->password_change_mutex_);
      if (runtime->credential_recovery_in_progress_ &&
          runtime->credential_recovery_attempt_pending_) {
        runtime->credential_recovery_action_ = RecoveryAction::RetryActive;
      }
    } else if (MiniRtcSignalStatus::MiniRtcSignalClosed == status) {
      runtime->signal_connected_ = false;
    } else if (MiniRtcSignalStatus::MiniRtcSignalReconnecting == status) {
      runtime->signal_connected_ = false;
    } else if (MiniRtcSignalStatus::MiniRtcSignalServerClosed == status) {
      runtime->signal_connected_ = false;
    } else if (MiniRtcSignalStatus::MiniRtcSignalTlsCertError == status) {
      runtime->signal_connected_ = false;
    }
  } else {
    if (client_id.rfind("C-", 0) != 0) {
      return;
    }

    std::string remote_id(client_id.begin() + 2, client_id.end());
    auto props = runtime->FindRemoteSession(remote_id);
    if (!props) return;
    props->signal_status_ = status;
    if (MiniRtcSignalStatus::MiniRtcSignalConnecting == status) {
      props->signal_connected_ = false;
    } else if (MiniRtcSignalStatus::MiniRtcSignalConnected == status) {
      props->signal_connected_ = true;
      LOG_INFO("[{}] connected to signal server", remote_id);
      SendClientInfo(props->peer_, client_id);
    } else if (MiniRtcSignalStatus::MiniRtcSignalFailed == status) {
      props->signal_connected_ = false;
    } else if (MiniRtcSignalStatus::MiniRtcSignalClosed == status) {
      props->signal_connected_ = false;
    } else if (MiniRtcSignalStatus::MiniRtcSignalReconnecting == status) {
      props->signal_connected_ = false;
    } else if (MiniRtcSignalStatus::MiniRtcSignalServerClosed == status) {
      props->signal_connected_ = false;
    } else if (MiniRtcSignalStatus::MiniRtcSignalTlsCertError == status) {
      props->signal_connected_ = false;
    }
  }
}

void PeerEventHandler::OnConnectionStatus(MiniRtcConnectionStatus status,
                                          const char* user_id, size_t size,
                                          void* context) {
  auto* handler = static_cast<PeerEventHandler*>(context);
  if (!handler) return;
  if (handler->direct_bound_) {
    auto callback = handler->EnterCallback();
    if (callback)
      if (auto session = handler->direct_.lock()) session->Status(status);
    return;
  }
  DispatchConnectionStatus(status, user_id, size, context);
}

void PeerEventHandler::DispatchConnectionStatus(MiniRtcConnectionStatus status,
                                                const char* user_id,
                                                const size_t user_id_size,
                                                void* user_data) {
  auto* handler = static_cast<PeerEventHandler*>(user_data);
  if (!handler) return;
  auto callback = handler->EnterCallback();
  if (!callback) return;
  GuiRuntime* runtime = &handler->owner_;

  std::string remote_id = handler->ConnectionKey(user_id, user_id_size);
  if (status == MiniRtcConnectionStatus::MiniRtcConnectionConnected) runtime->transfers_.Connected(handler, remote_id);
  else if (status == MiniRtcConnectionStatus::MiniRtcConnectionDisconnected || status == MiniRtcConnectionStatus::MiniRtcConnectionFailed ||
           status == MiniRtcConnectionStatus::MiniRtcConnectionClosed) runtime->transfers_.Cancel(handler, remote_id);
  std::shared_ptr<GuiRuntime::RemoteSession> props;
  {
    std::shared_lock lock(runtime->remote_sessions_mutex_);
    auto it = runtime->remote_sessions_.find(remote_id);
    if (it != runtime->remote_sessions_.end()) {
      props = it->second;
    }
  }

  if (!props) {
    if (status == MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
      if (!runtime->BeginControllerTrust(remote_id, handler)) return;
    } else {
      runtime->ForgetControllerTrust(remote_id, handler);
    }
  }

  if (props) {
    runtime->is_client_mode_ = true;
    if (!props->wall_managed_) runtime->show_connection_status_window_ = true;
    props->connection_status_.store(status);
    if (status != MiniRtcConnectionStatus::MiniRtcConnectionConnecting && status != MiniRtcConnectionStatus::MiniRtcConnectionGathering)
      props->connection_auth_.Complete();

    switch (status) {
      case MiniRtcConnectionStatus::MiniRtcConnectionConnected: {
        runtime->ResetRemoteServiceStatus(*props);
        {
          std::lock_guard lock(props->remote_cursor_state_mutex_);
          props->remote_cursor_state_ = {};
          props->remote_cursor_state_received_ = false;
          props->cursor_presentation_ = {};
        }
        {
          RemoteAction remote_action = MakeHostInformation(
              GetHostName(), runtime->devices_.host_display_list(),
              /*supports_privacy_screen=*/!runtime->unattended_host_,
              CROSSDESK_VERSION, kClientPlatform);
          remote_action.i.supports_video_settings = true;
          remote_action.i.supports_video_codec_status = true;
          remote_action.i.supports_text_input = runtime->devices_.SupportsTextInput();
          remote_action.i.supports_deskhub_preview = true;
          remote_action.i.supports_deskhub_resolution = true;
          remote_action.i.supports_deskhub_fps15 = true;
          remote_action.i.supports_deskhub_bandwidth = true;
          remote_action.i.supports_file_browser = !runtime->unattended_host_;
          for (std::size_t i = 0; i < remote_action.i.display_num; i++) {
            LOG_INFO("Local display [{}:{}]", i + 1,
                     remote_action.i.display_list[i]);
          }
          auto information = nlohmann::json::parse(remote_action.to_json());
          information["device_trust_version"] = 1;
          std::string msg = information.dump();
          const int ret =
              MiniRtcSendReliableDataFrame(props->peer_, msg.data(), msg.size(),
                                    runtime->control_data_label_.c_str());
          if (ret != 0) {
            LOG_WARN("[{}] host information send failed, ret={}", remote_id,
                     ret);
          }
          FreeRemoteAction(remote_action);
        }

        if (!props->wall_managed_ && !runtime->need_to_create_stream_window_) {
          runtime->need_to_create_stream_window_ = true;
        }
        props->connection_established_ = true;
        props->was_connected_ = true;
        props->recent_connection_dirty_ = true;
        if (!props->wall_managed_) runtime->start_keyboard_capturer_ = true;
        break;
      }
      case MiniRtcConnectionStatus::MiniRtcConnectionDisconnected:
      case MiniRtcConnectionStatus::MiniRtcConnectionFailed:
      case MiniRtcConnectionStatus::MiniRtcConnectionClosed: {
        runtime->keyboard_.ReleaseRemotePressedKeys(remote_id,
                                                    "connection_closed");
        props->connection_established_ = false;
        {
          std::lock_guard lock(props->remote_version_mutex_);
          props->remote_host_info_received_ = false;
          props->remote_app_version_.clear();
          props->remote_platform_ = HostPlatform::Unknown;
          props->remote_update_dismissed_ = false;
        }
        props->net_traffic_stats_.Reset();
        props->received_video_stats_.Reset();
        {
          std::lock_guard lock(props->gallery_frame_mutex_);
          props->gallery_video_latency_->Reset();
          props->gallery_frame_timing_ = {};
        }
        props->traversal_mode_ = MiniRtcTraversalMode::MiniRtcTraversalUnknown;
        props->enable_mouse_control_ = false;
        props->preview_subscription_supported_ = false;
        props->preview_resolution_supported_ = false;
        props->preview_fps15_supported_ = false;
        props->preview_bandwidth_supported_ = false;
        props->preview_needs_sync_ = true;
        props->preview_ack_ = 0;
        props->preview_accepted_ = false;
        props->audio_allowed_ = false;
        props->clipboard_allowed_ = false;
        runtime->ResetRemoteServiceStatus(*props);
        {
          std::lock_guard lock(props->video_settings_mutex_);
          props->video_settings_supported_ = false;
          props->video_settings_pending_ = false;
          props->video_settings_failed_ = false;
        }
        {
          std::lock_guard lock(props->remote_cursor_state_mutex_);
          props->remote_cursor_state_ = {};
          props->remote_cursor_state_received_ = false;
          props->cursor_presentation_ = {};
        }

        std::shared_ptr<std::vector<unsigned char>> native_snapshot;
        int native_snapshot_width = 0;
        int native_snapshot_height = 0;
        bool needs_native_snapshot = false;
        {
          std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
          needs_native_snapshot =
              !props->thumbnail_frame_ || props->thumbnail_frame_->empty();
        }
        if (needs_native_snapshot &&
            runtime->config_center_->IsSaveRemotePreviews()) {
          auto* native_renderer = runtime->video_renderer_.get();
          auto snapshot = std::make_shared<std::vector<unsigned char>>();
          if (native_renderer &&
              native_renderer->CopyLatestNv12(remote_id, snapshot.get(),
                                              &native_snapshot_width,
                                              &native_snapshot_height)) {
            native_snapshot = std::move(snapshot);
          }
        }
        {
          std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
          props->video_latency_->Reset();
          props->video_frame_timing_ = {};
          props->front_frame_.reset();
          props->back_frame_.reset();
          if (native_snapshot &&
              (!props->thumbnail_frame_ || props->thumbnail_frame_->empty())) {
            props->thumbnail_frame_ = std::move(native_snapshot);
            props->thumbnail_width_ = native_snapshot_width;
            props->thumbnail_height_ = native_snapshot_height;
          }
          props->video_width_ = 0;
          props->video_height_ = 0;
          props->video_size_ = 0;
          props->render_rect_dirty_ = true;
          props->stream_cleanup_pending_ = true;
        }
        auto* native_renderer = runtime->video_renderer_.get();
        if (native_renderer) {
          native_renderer->DiscardStream(remote_id);
          runtime->video_frame_dirty_.store(true, std::memory_order_release);
        }

        runtime->focus_on_stream_window_ = false;

        if (status == MiniRtcConnectionStatus::MiniRtcConnectionClosed) {
          props->remote_close_pending_.store(true, std::memory_order_release);
        }

        break;
      }
      default:
        break;
    }
  } else {
    runtime->is_client_mode_ = false;
    runtime->show_connection_status_window_ = true;
    {
      std::unique_lock lock(runtime->connection_status_mutex_);
      if (status == MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
        runtime->disconnect_lock_.Connected(remote_id);
        // Local privacy requires explicit desktop capability. Host info may
        // arrive before Connected; consume that saved capability here.
        const bool automatic_enable = runtime->privacy_sessions_.Connected(
            remote_id, runtime->config_center_->IsEnablePrivacyScreen());
        runtime->ApplyPrivacyAdmission(automatic_enable);
      }
      runtime->connection_status_[remote_id] = status;
      if (status == MiniRtcConnectionStatus::MiniRtcConnectionConnected)
        runtime->remote_input_.Connect(remote_id);
#if _WIN32
      runtime->windows_rdp_handoff_connected_->store(std::any_of(
          runtime->connection_status_.begin(), runtime->connection_status_.end(),
          [](const auto& entry) { return entry.second == MiniRtcConnectionStatus::MiniRtcConnectionConnected; }),
          std::memory_order_relaxed);
#endif
      runtime->devices_.OnControllerConnectionsChanged();
    }

    switch (status) {
      case MiniRtcConnectionStatus::MiniRtcConnectionConnected: {
        runtime->preview_subscriptions_.Remove(remote_id);
#if _WIN32
        runtime->last_windows_service_status_tick_ = 0;
#endif
        {
          RemoteAction remote_action = MakeHostInformation(
              GetHostName(), runtime->devices_.host_display_list(),
              /*supports_privacy_screen=*/!runtime->unattended_host_,
              CROSSDESK_VERSION, kClientPlatform);
          remote_action.i.supports_video_settings = true;
          remote_action.i.supports_video_codec_status = true;
          remote_action.i.supports_text_input = runtime->devices_.SupportsTextInput();
          remote_action.i.supports_deskhub_preview = true;
          remote_action.i.supports_deskhub_resolution = true;
          remote_action.i.supports_deskhub_fps15 = true;
          remote_action.i.supports_deskhub_bandwidth = true;
          remote_action.i.supports_file_browser = !runtime->unattended_host_;
          for (std::size_t i = 0; i < remote_action.i.display_num; i++) {
            LOG_INFO("Local display [{}:{}]", i + 1,
                     remote_action.i.display_list[i]);
          }
          std::string msg = remote_action.to_json();
          const int ret =
              runtime->SendControllerData(remote_id, msg.data(), msg.size(),
                                          runtime->control_data_label_.c_str());
          if (ret != 0) {
            LOG_WARN("[{}] host information send failed, ret={}", remote_id,
                     ret);
          }
          FreeRemoteAction(remote_action);
        }

        runtime->need_to_destroy_server_window_.store(
            false, std::memory_order_release);
        runtime->need_to_create_server_window_.store(true,
                                                     std::memory_order_release);
        runtime->is_server_mode_ = true;
        runtime->start_screen_capturer_ = true;
        runtime->devices_.StartSpeakerCapturer();
        runtime->remote_client_id_ = remote_id;
        runtime->start_mouse_controller_ = true;

        break;
      }
      case MiniRtcConnectionStatus::MiniRtcConnectionDisconnected:
      case MiniRtcConnectionStatus::MiniRtcConnectionFailed:
      case MiniRtcConnectionStatus::MiniRtcConnectionClosed: {
        runtime->HandleServerControllerDisconnected(remote_id,
                                                    "connection_closed");
        break;
      }
      default:
        break;
    }
  }
}

void PeerEventHandler::OnNetStatusReport(
    const char* client_id, size_t client_id_size, MiniRtcTraversalMode mode,
    const MiniRtcNetTrafficStats* net_traffic_stats, const char* user_id,
    const size_t user_id_size, void* user_data) {
  auto* handler = static_cast<PeerEventHandler*>(user_data);
  if (!handler) return;
  auto callback = handler->EnterCallback();
  if (!callback) return;
  GuiRuntime* runtime = &handler->owner_;

  if (const char* at_pos = handler->IsDirect() ? nullptr : strchr(client_id, '@');
      at_pos && strchr(user_id, '-') == nullptr) {
    const std::string id(client_id, at_pos - client_id);
    const std::string password(at_pos + 1);

    bool is_self_hosted = runtime->config_center_->IsSelfHosted();

    if (is_self_hosted) {
      memset(&runtime->client_id_, 0, sizeof(runtime->client_id_));
      strncpy(runtime->client_id_, id.c_str(), sizeof(runtime->client_id_) - 1);
      runtime->client_id_[sizeof(runtime->client_id_) - 1] = '\0';

      memset(&runtime->password_saved_, 0, sizeof(runtime->password_saved_));
      strncpy(runtime->password_saved_, password.c_str(),
              sizeof(runtime->password_saved_) - 1);
      runtime->password_saved_[sizeof(runtime->password_saved_) - 1] = '\0';

      memset(&runtime->self_hosted_id_, 0, sizeof(runtime->self_hosted_id_));
      strncpy(runtime->self_hosted_id_, client_id,
              sizeof(runtime->self_hosted_id_) - 1);
      runtime->self_hosted_id_[sizeof(runtime->self_hosted_id_) - 1] = '\0';

      LOG_INFO("Use self-hosted client id [{}] and save to cache file", id);

      runtime->settings_.PersistSelfHostedIdentity(client_id);
    } else {
      memset(&runtime->client_id_, 0, sizeof(runtime->client_id_));
      strncpy(runtime->client_id_, id.c_str(), sizeof(runtime->client_id_) - 1);
      runtime->client_id_[sizeof(runtime->client_id_) - 1] = '\0';

      memset(&runtime->password_saved_, 0, sizeof(runtime->password_saved_));
      strncpy(runtime->password_saved_, password.c_str(),
              sizeof(runtime->password_saved_) - 1);
      runtime->password_saved_[sizeof(runtime->password_saved_) - 1] = '\0';

      memset(&runtime->client_id_with_password_, 0,
             sizeof(runtime->client_id_with_password_));
      strncpy(runtime->client_id_with_password_, client_id,
              sizeof(runtime->client_id_with_password_) - 1);
      runtime
          ->client_id_with_password_[sizeof(runtime->client_id_with_password_) -
                                     1] = '\0';

      LOG_INFO("Use client id [{}] and save id into cache file", id);
      runtime->settings_.Save();
    }
#if defined(__linux__) && !defined(__OHOS__)
    HeadlessConsole::Instance().SetIdentity(id + "@" + password);
#endif
  }

  std::string remote_id = handler->ConnectionKey(user_id, user_id_size);
  auto props = runtime->FindRemoteSession(remote_id);
  if (!props) {
    bool report_codecs = false;
    {
      std::shared_lock lock(runtime->connection_status_mutex_);
      const auto status = runtime->connection_status_.find(remote_id);
      report_codecs = runtime->codec_status_controllers_.count(remote_id) != 0 &&
          status != runtime->connection_status_.end() &&
          status->second == MiniRtcConnectionStatus::MiniRtcConnectionConnected;
    }
    if (net_traffic_stats && report_codecs) {
      RemoteAction action{};
      action.type = ControlType::video_codec_status;
      std::snprintf(action.vcs.encoder, sizeof(action.vcs.encoder), "%.*s",
                    static_cast<int>(sizeof(net_traffic_stats->video_encoder)),
                    net_traffic_stats->video_encoder);
      const auto message = action.to_json();
      // Report only to this controller, after releasing the status lock.
      runtime->SendControllerData(remote_id, message.data(), message.size(),
                                  runtime->control_data_label_.c_str());
    }
    return;
  }
  if (props->traversal_mode_ != mode) {
    props->net_traffic_stats_.ResetLatency();
    props->traversal_mode_ = mode;
    LOG_INFO("Net mode: [{}]", int(props->traversal_mode_.load()));
  }

  if (!net_traffic_stats) {
    return;
  }

  // only display client side net status if connected to itself
  if (!(runtime->peer_reserved_ && !strstr(client_id, "C-"))) {
    props->net_traffic_stats_.Store(*net_traffic_stats);
  }
}
}  // namespace crossdesk
