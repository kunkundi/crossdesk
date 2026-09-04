#include "runtime/peer_event_handler.h"

#include <remote_action.h>

#include <algorithm>
#include <chrono>
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
#include "runtime/remote_action_codec.h"

#if _WIN32
#include "interactive_state.h"
#include "service_host.h"
#endif

namespace crossdesk {

void PeerEventHandler::SendClientInfo(PeerPtr* peer,
                                      const std::string& client_id) {
  if (!peer) {
    return;
  }

#if defined(_WIN32)
  constexpr const char* kClientPlatform = "windows";
#elif defined(__APPLE__)
  constexpr const char* kClientPlatform = "macos";
#elif defined(__linux__)
  constexpr const char* kClientPlatform = "linux";
#else
  constexpr const char* kClientPlatform = "unknown";
#endif

  const nlohmann::json message = {{"type", "client_info"},
                                  {"version", CROSSDESK_VERSION},
                                  {"platform", kClientPlatform}};
  const std::string payload = message.dump();
  if (SendSignalMessage(peer, payload.data(), payload.size()) != 0) {
    LOG_WARN("[{}] failed to report client information", client_id);
  }
}

PeerEventHandler::PeerEventHandler(GuiRuntime& owner) : owner_(owner) {}

void PeerEventHandler::OnSignalMessage(const char* message, size_t size,
                                       void* user_data) {
  auto* handler = static_cast<PeerEventHandler*>(user_data);
  GuiRuntime* runtime = handler ? &handler->owner_ : nullptr;
  if (!runtime || !message || size == 0) {
    return;
  }
  std::string s(message, size);
  auto j = nlohmann::json::parse(s, nullptr, false);
  if (j.is_discarded() || !j.contains("type") || !j["type"].is_string()) {
    return;
  }
  std::string type = j["type"].get<std::string>();
  if (type == "presence") {
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
    runtime->password_change_result_uncertain_ = false;
    runtime->password_change_result_ready_ = true;
  }
}

void PeerEventHandler::OnSignalStatus(SignalStatus status, const char* user_id,
                                      size_t user_id_size, void* user_data) {
  auto* handler = static_cast<PeerEventHandler*>(user_data);
  GuiRuntime* runtime = handler ? &handler->owner_ : nullptr;
  if (!runtime) {
    return;
  }

  std::string client_id(user_id, user_id_size);
  if (client_id == runtime->client_id_) {
    runtime->signal_status_ = status;
    if (SignalStatus::SignalConnecting == status) {
      runtime->signal_connected_ = false;
    } else if (SignalStatus::SignalConnected == status) {
      runtime->signal_connected_ = true;
      runtime->need_to_send_recent_connections_ = true;
      LOG_INFO("[{}] connected to signal server", client_id);
      SendClientInfo(runtime->peer_, client_id);
      std::lock_guard<std::mutex> lock(runtime->password_change_mutex_);
      if (runtime->credential_recovery_in_progress_) {
        if (runtime->credential_recovery_attempt_pending_) {
          runtime->credential_recovery_promote_pending_ = true;
        } else {
          runtime->credential_recovery_clear_pending_ = true;
        }
      }
    } else if (SignalStatus::SignalFailed == status) {
      runtime->signal_connected_ = false;
      std::lock_guard<std::mutex> lock(runtime->password_change_mutex_);
      if (runtime->credential_recovery_in_progress_ &&
          runtime->credential_recovery_attempt_pending_) {
        runtime->credential_recovery_retry_active_ = true;
      }
    } else if (SignalStatus::SignalClosed == status) {
      runtime->signal_connected_ = false;
    } else if (SignalStatus::SignalReconnecting == status) {
      runtime->signal_connected_ = false;
    } else if (SignalStatus::SignalServerClosed == status) {
      runtime->signal_connected_ = false;
    } else if (SignalStatus::SignalTlsCertError == status) {
      runtime->signal_connected_ = false;
    }
  } else {
    if (client_id.rfind("C-", 0) != 0) {
      return;
    }

    std::string remote_id(client_id.begin() + 2, client_id.end());
    // std::shared_lock lock(runtime->remote_sessions_mutex_);
    if (runtime->remote_sessions_.find(remote_id) ==
        runtime->remote_sessions_.end()) {
      return;
    }
    auto props = runtime->remote_sessions_.find(remote_id)->second;
    props->signal_status_ = status;
    if (SignalStatus::SignalConnecting == status) {
      props->signal_connected_ = false;
    } else if (SignalStatus::SignalConnected == status) {
      props->signal_connected_ = true;
      LOG_INFO("[{}] connected to signal server", remote_id);
      SendClientInfo(props->peer_, client_id);
    } else if (SignalStatus::SignalFailed == status) {
      props->signal_connected_ = false;
    } else if (SignalStatus::SignalClosed == status) {
      props->signal_connected_ = false;
    } else if (SignalStatus::SignalReconnecting == status) {
      props->signal_connected_ = false;
    } else if (SignalStatus::SignalServerClosed == status) {
      props->signal_connected_ = false;
    } else if (SignalStatus::SignalTlsCertError == status) {
      props->signal_connected_ = false;
    }
  }
}

void PeerEventHandler::OnConnectionStatus(ConnectionStatus status,
                                          const char* user_id,
                                          const size_t user_id_size,
                                          void* user_data) {
  auto* handler = static_cast<PeerEventHandler*>(user_data);
  GuiRuntime* runtime = handler ? &handler->owner_ : nullptr;
  if (!runtime) return;

  std::string remote_id(user_id, user_id_size);
  std::shared_ptr<GuiRuntime::RemoteSession> props;
  {
    std::shared_lock lock(runtime->remote_sessions_mutex_);
    auto it = runtime->remote_sessions_.find(remote_id);
    if (it != runtime->remote_sessions_.end()) {
      props = it->second;
    }
  }

  if (props) {
    runtime->is_client_mode_ = true;
    runtime->show_connection_status_window_ = true;
    props->connection_status_.store(status);

    switch (status) {
      case ConnectionStatus::Connected: {
        runtime->ResetRemoteServiceStatus(*props);
        {
          std::lock_guard lock(props->remote_cursor_state_mutex_);
          props->remote_cursor_state_ = {};
          props->remote_cursor_state_received_ = false;
        }
        {
          RemoteAction remote_action;
          remote_action.i.display_num =
              runtime->devices_.display_info_list().size();
          remote_action.i.display_list =
              (char**)malloc(remote_action.i.display_num * sizeof(char*));
          remote_action.i.left =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.top =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.right =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.bottom =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          for (int i = 0; i < remote_action.i.display_num; i++) {
            LOG_INFO("Local display [{}:{}]", i + 1,
                     runtime->devices_.display_info_list()[i].name);
            remote_action.i.display_list[i] = (char*)malloc(
                runtime->devices_.display_info_list()[i].name.length() + 1);
            strncpy(remote_action.i.display_list[i],
                    runtime->devices_.display_info_list()[i].name.c_str(),
                    runtime->devices_.display_info_list()[i].name.length());
            remote_action.i
                .display_list[i][runtime->devices_.display_info_list()[i]
                                     .name.length()] = '\0';
            remote_action.i.left[i] =
                runtime->devices_.display_info_list()[i].left;
            remote_action.i.top[i] =
                runtime->devices_.display_info_list()[i].top;
            remote_action.i.right[i] =
                runtime->devices_.display_info_list()[i].right;
            remote_action.i.bottom[i] =
                runtime->devices_.display_info_list()[i].bottom;
          }

          std::string host_name = GetHostName();
          remote_action.type = ControlType::host_infomation;
          memcpy(&remote_action.i.host_name, host_name.data(),
                 host_name.size());
          remote_action.i.host_name[host_name.size()] = '\0';
          remote_action.i.host_name_size = host_name.size();

          std::string msg = remote_action.to_json();
          int ret = SendReliableDataFrame(props->peer_, msg.data(), msg.size(),
                                          runtime->control_data_label_.c_str());
          remote_action_codec::Free(remote_action);
        }

        if (!runtime->need_to_create_stream_window_ &&
            !runtime->remote_sessions_.empty()) {
          runtime->need_to_create_stream_window_ = true;
        }
        props->connection_established_ = true;
        runtime->start_keyboard_capturer_ = true;
        break;
      }
      case ConnectionStatus::Disconnected:
      case ConnectionStatus::Failed:
      case ConnectionStatus::Closed: {
        runtime->keyboard_.ReleaseRemotePressedKeys(remote_id,
                                                    "connection_closed");
        props->connection_established_ = false;
        props->enable_mouse_control_ = false;
        runtime->ResetRemoteServiceStatus(*props);
        {
          std::lock_guard lock(props->remote_cursor_state_mutex_);
          props->remote_cursor_state_ = {};
          props->remote_cursor_state_received_ = false;
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
        if (needs_native_snapshot) {
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

        break;
      }
      case ConnectionStatus::IncorrectPassword: {
        runtime->password_validating_ = false;
        runtime->password_validating_time_++;
        if (runtime->connect_button_pressed_) {
          runtime->connect_button_pressed_ = false;
          props->connection_established_ = false;
          runtime->connect_button_label_ =
              localization::connect[runtime->localization_language_index_];
        }
        break;
      }
      case ConnectionStatus::NoSuchTransmissionId:
      case ConnectionStatus::RemoteUnavailable: {
        if (runtime->connect_button_pressed_) {
          props->connection_established_ = false;
          runtime->connect_button_label_ =
              localization::connect[runtime->localization_language_index_];
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
      runtime->connection_status_[remote_id] = status;
    }

    switch (status) {
      case ConnectionStatus::Connected: {
#if _WIN32
        runtime->last_windows_service_status_tick_ = 0;
#endif
        {
          RemoteAction remote_action;
          remote_action.i.display_num =
              runtime->devices_.display_info_list().size();
          remote_action.i.display_list =
              (char**)malloc(remote_action.i.display_num * sizeof(char*));
          remote_action.i.left =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.top =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.right =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.bottom =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          for (int i = 0; i < remote_action.i.display_num; i++) {
            LOG_INFO("Local display [{}:{}]", i + 1,
                     runtime->devices_.display_info_list()[i].name);
            remote_action.i.display_list[i] = (char*)malloc(
                runtime->devices_.display_info_list()[i].name.length() + 1);
            strncpy(remote_action.i.display_list[i],
                    runtime->devices_.display_info_list()[i].name.c_str(),
                    runtime->devices_.display_info_list()[i].name.length());
            remote_action.i
                .display_list[i][runtime->devices_.display_info_list()[i]
                                     .name.length()] = '\0';
            remote_action.i.left[i] =
                runtime->devices_.display_info_list()[i].left;
            remote_action.i.top[i] =
                runtime->devices_.display_info_list()[i].top;
            remote_action.i.right[i] =
                runtime->devices_.display_info_list()[i].right;
            remote_action.i.bottom[i] =
                runtime->devices_.display_info_list()[i].bottom;
          }

          std::string host_name = GetHostName();
          remote_action.type = ControlType::host_infomation;
          memcpy(&remote_action.i.host_name, host_name.data(),
                 host_name.size());
          remote_action.i.host_name[host_name.size()] = '\0';
          remote_action.i.host_name_size = host_name.size();

          std::string msg = remote_action.to_json();
          int ret =
              SendReliableDataFrame(runtime->peer_, msg.data(), msg.size(),
                                    runtime->control_data_label_.c_str());
          remote_action_codec::Free(remote_action);
        }

        runtime->need_to_destroy_server_window_.store(
            false, std::memory_order_release);
        runtime->need_to_create_server_window_.store(true,
                                                     std::memory_order_release);
        runtime->is_server_mode_ = true;
        runtime->start_screen_capturer_ = true;
        runtime->start_speaker_capturer_ = true;
        runtime->remote_client_id_ = remote_id;
        runtime->start_mouse_controller_ = true;
        {
          std::shared_lock lock(runtime->connection_status_mutex_);
          if (std::all_of(runtime->connection_status_.begin(),
                          runtime->connection_status_.end(),
                          [](const auto& kv) {
                            return kv.first.find("web") != std::string::npos;
                          })) {
            runtime->show_cursor_ = true;
          }
        }

        break;
      }
      case ConnectionStatus::Disconnected:
      case ConnectionStatus::Failed:
      case ConnectionStatus::Closed: {
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
    const char* client_id, size_t client_id_size, TraversalMode mode,
    const MiniRtcNetTrafficStats* net_traffic_stats, const char* user_id,
    const size_t user_id_size, void* user_data) {
  auto* handler = static_cast<PeerEventHandler*>(user_data);
  GuiRuntime* runtime = handler ? &handler->owner_ : nullptr;
  if (!runtime) {
    return;
  }

  if (strchr(client_id, '@') != nullptr && strchr(user_id, '-') == nullptr) {
    std::string id, password;
    const char* at_pos = strchr(client_id, '@');
    if (at_pos == nullptr) {
      id = client_id;
      password.clear();
    } else {
      id.assign(client_id, at_pos - client_id);
      password = at_pos + 1;
    }

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
  }

  std::string remote_id(user_id, user_id_size);
  // std::shared_lock lock(runtime->remote_sessions_mutex_);
  if (runtime->remote_sessions_.find(remote_id) ==
      runtime->remote_sessions_.end()) {
    return;
  }
  auto props = runtime->remote_sessions_.find(remote_id)->second;
  if (props->traversal_mode_ != mode) {
    props->traversal_mode_ = mode;
    LOG_INFO("Net mode: [{}]", int(props->traversal_mode_));
  }

  if (!net_traffic_stats) {
    return;
  }

  // only display client side net status if connected to itself
  if (!(runtime->peer_reserved_ && !strstr(client_id, "C-"))) {
    props->net_traffic_stats_ = *net_traffic_stats;
  }
}
}  // namespace crossdesk
