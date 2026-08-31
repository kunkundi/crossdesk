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

#include "device_controller.h"
#include "file_transfer.h"
#include "localization.h"
#include "filesystem_utf8.h"
#include "platform.h"
#include "rd_log.h"
#include "runtime/gui_runtime.h"
#include "runtime/remote_action_codec.h"

#if _WIN32
#include "interactive_state.h"
#include "service_host.h"
#endif

namespace crossdesk {

namespace {
#if _WIN32
constexpr uint32_t kSecureDesktopInputLogIntervalMs = 2000;

bool BuildAbsoluteMousePosition(const std::vector<DisplayInfo> &displays,
                                int display_index, float normalized_x,
                                float normalized_y, int *absolute_x_out,
                                int *absolute_y_out) {
  if (absolute_x_out == nullptr || absolute_y_out == nullptr ||
      display_index < 0 || display_index >= static_cast<int>(displays.size())) {
    return false;
  }

  const DisplayInfo &display = displays[display_index];
  if (display.width <= 0 || display.height <= 0) {
    return false;
  }

  const float clamped_x = std::clamp(normalized_x, 0.0f, 1.0f);
  const float clamped_y = std::clamp(normalized_y, 0.0f, 1.0f);
  *absolute_x_out = static_cast<int>(clamped_x * display.width) + display.left;
  *absolute_y_out = static_cast<int>(clamped_y * display.height) + display.top;
  return true;
}

void LogSecureDesktopInputBlocked(uint32_t *last_tick, const char *side,
                                  const char *stage) {
  if (last_tick == nullptr) {
    return;
  }

  const uint32_t now = static_cast<uint32_t>(SDL_GetTicks());
  if (*last_tick != 0 && now - *last_tick < kSecureDesktopInputLogIntervalMs) {
    return;
  }

  *last_tick = now;
  LOG_WARN("{} secure-desktop input blocked, stage={}, normal SendInput path "
           "cannot drive the Windows password UI",
           side != nullptr ? side : "unknown", stage != nullptr ? stage : "");
}

#endif

} // namespace

void PeerEventHandler::OnReceiveDataBuffer(
    const char *data, size_t size, const char *user_id, size_t user_id_size,
    const char *src_id, size_t src_id_size, void *user_data) {
  auto *handler = static_cast<PeerEventHandler *>(user_data);
  GuiRuntime *runtime = handler ? &handler->owner_ : nullptr;
  if (!runtime) {
    return;
  }

  std::string source_id = std::string(src_id, src_id_size);
  if (source_id == runtime->file_label_) {
    std::string remote_user_id = std::string(user_id, user_id_size);

    static FileReceiver receiver;
    // Update output directory from config
    std::string configured_path =
        runtime->config_center_->GetFileTransferSavePath();
    if (!configured_path.empty()) {
      receiver.SetOutputDir(PathFromUtf8(configured_path));
    } else if (receiver.OutputDir().empty()) {
      receiver = FileReceiver(); // re-init with default desktop path
    }
    receiver.SetOnSendAck([runtime,
                           remote_user_id](const FileTransferAck &ack) -> int {
      const auto encoded_ack = EncodeFileTransferAck(ack);
      bool is_server_sending = remote_user_id.rfind("C-", 0) != 0;
      if (is_server_sending) {
        auto props =
            runtime->FindRemoteSession(remote_user_id);
        if (props) {
          PeerPtr *peer = props->peer_;
          return SendReliableDataFrame(
              peer, encoded_ack.data(), encoded_ack.size(),
              runtime->file_feedback_label_.c_str());
        }
      }

      return SendReliableDataFrame(
          runtime->peer_, encoded_ack.data(), encoded_ack.size(),
          runtime->file_feedback_label_.c_str());
    });

    receiver.OnData(data, size);
    return;
  } else if (source_id == runtime->clipboard_label_) {
    if (size > 0) {
      std::string remote_user_id(user_id, user_id_size);
      auto props =
          runtime->FindRemoteSession(remote_user_id);
      if (props && !props->enable_mouse_control_) {
        return;
      }

      runtime->clipboard_.QueueRemoteText(data, size);
    }
    return;
  } else if (source_id == runtime->file_feedback_label_) {
    runtime->transfers_.HandleAck(data, size);
    return;
  }

  std::string json_str(data, size);
  RemoteAction remote_action{};
  if (!remote_action.from_json(json_str)) {
    LOG_ERROR("Failed to parse RemoteAction JSON payload");
    return;
  }

  std::string remote_id(user_id, user_id_size);
  if (remote_action.type == ControlType::service_status) {
    auto props_it = runtime->remote_sessions_.find(remote_id);
    if (props_it != runtime->remote_sessions_.end()) {
      runtime->ApplyRemoteServiceStatus(*props_it->second, remote_action.ss);
    }
    return;
  }

  if (remote_action.type == ControlType::service_command) {
#if _WIN32
    if (remote_action.c.flag == ServiceCommandFlag::send_sas) {
      runtime->pending_windows_service_sas_.store(true,
                                                 std::memory_order_relaxed);
    } else if (remote_action.c.flag == ServiceCommandFlag::lock_workstation) {
      if (!LockWorkStation()) {
        LOG_WARN("Remote lock workstation request failed, error={}",
                 GetLastError());
      }
    }
#endif
    return;
  }

  if (remote_action.type == ControlType::cursor_state) {
    std::shared_ptr<GuiRuntime::RemoteSession> props;
    {
      std::shared_lock lock(runtime->remote_sessions_mutex_);
      auto props_it = runtime->remote_sessions_.find(remote_id);
      if (props_it != runtime->remote_sessions_.end()) {
        props = props_it->second;
      }
    }
    if (!props) {
      return;
    }

    std::lock_guard lock(props->remote_cursor_state_mutex_);
    const uint32_t previous_seq = props->remote_cursor_state_.seq;
    const bool is_newer =
        !props->remote_cursor_state_received_ ||
        static_cast<int32_t>(remote_action.cs.seq - previous_seq) > 0;
    if (is_newer) {
      const bool changed =
          !props->remote_cursor_state_received_ ||
          props->remote_cursor_state_.visible != remote_action.cs.visible ||
          props->remote_cursor_state_.shape != remote_action.cs.shape;
      CursorState merged = remote_action.cs;
      if (!remote_action.cs.position_update &&
          props->remote_cursor_state_received_) {
        merged.position_valid = props->remote_cursor_state_.position_valid;
        merged.x = props->remote_cursor_state_.x;
        merged.y = props->remote_cursor_state_.y;
        merged.display_id = props->remote_cursor_state_.display_id;
      }
      props->remote_cursor_state_ = merged;
      props->remote_cursor_state_received_ = true;
      if (changed) {
        LOG_INFO("Received cursor state: seq={}, visible={}, shape={}",
                 remote_action.cs.seq, remote_action.cs.visible,
                 static_cast<int>(remote_action.cs.shape));
      }
    }
    return;
  }

  if (remote_action.type == ControlType::host_infomation) {
    bool is_client_mode = false;
    std::shared_ptr<GuiRuntime::RemoteSession> props;
    {
      std::shared_lock lock(runtime->remote_sessions_mutex_);
      auto props_it = runtime->remote_sessions_.find(remote_id);
      if (props_it != runtime->remote_sessions_.end()) {
        is_client_mode = true;
        props = props_it->second;
      }
    }

    if (is_client_mode) {
      // client mode
      if (props && props->remote_host_name_.empty()) {
        props->remote_host_name_ = std::string(remote_action.i.host_name,
                                               remote_action.i.host_name_size);
        LOG_INFO("Remote hostname: [{}]", props->remote_host_name_);

        for (int i = 0; i < remote_action.i.display_num; i++) {
          props->display_info_list_.push_back(
              DisplayInfo(remote_action.i.display_list[i],
                          remote_action.i.left[i], remote_action.i.top[i],
                          remote_action.i.right[i], remote_action.i.bottom[i]));
        }
      }
      remote_action_codec::Free(remote_action);
    } else {
      // server mode
      std::string host_name(remote_action.i.host_name,
                            remote_action.i.host_name_size);
      {
        std::unique_lock lock(runtime->connection_status_mutex_);
        runtime->connection_host_names_[remote_id] = host_name;
      }
      LOG_INFO("Remote hostname: [{}]", host_name);
      remote_action_codec::Free(remote_action);
    }
  } else {
    // remote
    if (runtime->is_server_mode_ &&
        remote_action.type == ControlType::mouse) {
      std::lock_guard lock(runtime->remote_pointer_input_mutex_);
      runtime->last_remote_pointer_input_time_[remote_id] =
          std::chrono::steady_clock::now();
    }
#if _WIN32
    if (runtime->local_service_status_received_ &&
        IsSecureDesktopInteractionRequired(runtime->local_interactive_stage_) &&
        remote_action.type != ControlType::keyboard &&
        remote_action.type != ControlType::keyboard_state) {
      if (remote_action.type == ControlType::mouse) {
        int absolute_x = 0;
        int absolute_y = 0;
        if (!BuildAbsoluteMousePosition(runtime->devices_.display_info_list(),
                                        runtime->selected_display_,
                                        remote_action.m.x, remote_action.m.y,
                                        &absolute_x, &absolute_y)) {
          LOG_WARN("Secure desktop mouse injection skipped, invalid display "
                   "mapping: display_index={}, x={}, y={}",
                   runtime->selected_display_, remote_action.m.x,
                   remote_action.m.y);
          return;
        }

        const std::string response = SendCrossDeskSecureDesktopMouseInput(
            absolute_x, absolute_y, remote_action.m.s,
            static_cast<int>(remote_action.m.flag), 1000);
        auto json = nlohmann::json::parse(response, nullptr, false);
        if (json.is_discarded() || !json.value("ok", false)) {
          LogSecureDesktopInputBlocked(
              &runtime->last_local_secure_input_block_log_tick_, "local",
              runtime->local_interactive_stage_.c_str());
          LOG_WARN(
              "Secure desktop mouse injection failed, x={}, y={}, wheel={}, "
              "flag={}, response={}",
              absolute_x, absolute_y, remote_action.m.s,
              static_cast<int>(remote_action.m.flag), response);
        }
        return;
      }
    }
#endif
    if (remote_action.type == ControlType::mouse) {
      runtime->devices_.SendMouseCommand(remote_action,
                                        runtime->selected_display_);
    } else if (remote_action.type == ControlType::audio_capture) {
      if (remote_action.a && !runtime->start_speaker_capturer_)
        runtime->devices_.StartSpeakerCapturer();
      else if (!remote_action.a && runtime->start_speaker_capturer_)
        runtime->devices_.StopSpeakerCapturer();
    } else if (remote_action.type == ControlType::keyboard) {
      runtime->keyboard_.ApplyRemoteEvent(remote_id, remote_action);
    } else if (remote_action.type == ControlType::keyboard_state) {
      runtime->keyboard_.ApplyRemoteState(remote_id, remote_action);
    } else if (remote_action.type == ControlType::display_id) {
      const int ret = runtime->devices_.SwitchDisplay(remote_action.d);
      if (ret == 0) {
        runtime->selected_display_ = remote_action.d;
      } else {
        LOG_WARN("Display switch skipped, invalid display_id={}",
                 remote_action.d);
      }
    }
  }
}

} // namespace crossdesk
