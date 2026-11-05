#include <SDL3/SDL_timer.h>

#include "runtime/peer_event_handler.h"

#include <app_version.h>
#include <remote_action.h>
#include <stream_names.h>
#include <nlohmann/json.hpp>

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
#include "web_clipboard_protocol.h"
#ifdef __OHOS__
#include "platform/harmony/input/system_shortcuts.h"
#endif

#if _WIN32
#include "desktop_transition_policy.h"
#include "interactive_desktop.h"
#include "service_host.h"
#include "unattended_config.h"
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

bool PeerEventHandler::QueueEarlyData(const char* data, size_t size,
                                      const char* label, size_t label_size) {
  if (!direct_bound_) return false;
  auto session = direct_.lock();
  if (!session || session->closing) return true;
  std::lock_guard lock(early_data_mutex_);
  if (session->authorized) return false;
  // Keep bounded reliable metadata until the manager commits Connected. Mouse,
  // keyboard, clipboard and files never enter this pre-authorization queue.
  if (std::string_view(label, label_size) != owner_.control_data_label_)
    return true;
  if (size > 8192 || early_data_.size() >= 16 ||
      early_data_bytes_ + size > 65536) {
    session->failed = true;
    return true;
  }
  early_data_bytes_ += size;
  early_data_.emplace_back(data, size);
  return true;
}
void PeerEventHandler::FlushEarlyData() {
  std::vector<std::string> queued;
  {
    std::lock_guard lock(early_data_mutex_);
    queued.swap(early_data_);
    early_data_bytes_ = 0;
  }
  const auto session = direct_.lock();
  if (!session || !session->authorized || session->closing) return;
  const auto& label = owner_.control_data_label_;
  for (const auto& data : queued)
    OnReceiveDataBuffer(data.data(), data.size(), session->remote_id.data(),
                        session->remote_id.size(), label.data(), label.size(),
                        this);
}

void PeerEventHandler::OnReceiveDataBuffer(
    const char *data, size_t size, const char *user_id, size_t user_id_size,
    const char *src_id, size_t src_id_size, void *user_data) {
  auto *handler = static_cast<PeerEventHandler *>(user_data);
  if (!handler) return;
  auto callback = handler->EnterCallback();
  if (!callback || handler->QueueEarlyData(data, size, src_id, src_id_size) ||
      !handler->DataAllowed())
    return;
  GuiRuntime* runtime = &handler->owner_;

  std::string source_id = std::string(src_id, src_id_size);
  const auto remote_id = handler->ConnectionKey(user_id, user_id_size);
  if (source_id == runtime->control_data_label_ &&
      runtime->HandleDeviceTrustMessage(remote_id, handler, data, size)) return;
  const auto viewer = runtime->FindRemoteSession(remote_id);
  if (viewer && viewer->peer_events_.get() != handler) return;
  if (!viewer && !runtime->TrustedControllerAllowed(remote_id)) return;
  // The service-owned host runs as SYSTEM. Do not expose filesystem or
  // clipboard operations in that security context.
  if (runtime->unattended_host_ &&
      (source_id == runtime->file_label_ ||
       source_id == runtime->file_feedback_label_ ||
       source_id == runtime->clipboard_label_ ||
       source_id == web_clipboard::kStream))
    return;
  if (source_id == runtime->control_data_label_ &&
      runtime->transfers_.HandleBrowser(data, size, handler, remote_id)) return;
  if (source_id == web_clipboard::kStream) {
    runtime->clipboard_.ReceiveWeb(
        data, size, remote_id, handler);
    return;
  }
  if (source_id == runtime->file_label_) {
    runtime->transfers_.Receive(data, size, handler, remote_id);
    return;
  } else if (source_id == runtime->clipboard_label_) {
    if (size > 0) {
      if (viewer && !viewer->clipboard_allowed_.load()) {
        return;
      }

      runtime->clipboard_.QueueRemoteText(
          data, size, viewer ? remote_id : std::string{}, {},
          viewer ? std::function<bool()>{} : [runtime, remote_id] {
            return runtime->IsAuthorizedController(remote_id);
          });
    }
    return;
  } else if (source_id == runtime->file_feedback_label_) {
    runtime->transfers_.HandleAck(data, size, handler, remote_id);
    return;
  }

  const std::string wire_id = handler->WireId(remote_id);
  PeerPtr* bound_peer =
      handler->IsDirect() ? handler->DirectPeer() : runtime->peer_;
  std::string json_str(data, size);
  const auto preview_json =
      json_str.find("\"deskhub_preview") != std::string::npos
          ? nlohmann::json::parse(json_str, nullptr, false)
          : nlohmann::json{};
  if (preview_json.is_object() &&
      preview_json.value("type", nlohmann::json{}) == "deskhub_preview") {
    if (source_id != runtime->control_data_label_ ||
        !runtime->IsAuthorizedController(remote_id))
      return;
    const auto subscription = PreviewSubscription::Parse(preview_json);
    if (!subscription) return;
    const auto previous = runtime->preview_subscriptions_.Get(remote_id);
    bool accepted = true;
    // Apply encoder quality to this controller only; other controllers retain
    // their own demand. MiniRTC owns per-peer video/audio pause state.
    if (!previous || subscription->IsNewerThan(*previous)) {
      if (subscription->fps > 0)
        accepted = UpdateVideoSettings(
                       bound_peer, wire_id.data(), wire_id.size(),
                       static_cast<VideoQuality>(subscription->quality),
                       subscription->frame_rate,
                       static_cast<VideoDegradationPreference>(
                           subscription->EncoderPreference())) == 0;
      if (accepted && (!previous || previous->max_bitrate_bps.value_or(0) !=
                                       subscription->max_bitrate_bps.value_or(0)))
        accepted = UpdateVideoBitrateLimit(
                       bound_peer, wire_id.data(), wire_id.size(),
                       subscription->max_bitrate_bps.value_or(0)) == 0;
      // Rebalancing bandwidth alone must not force every device to send a
      // keyframe. The pause API requests its own keyframes on resume.
      if (accepted && (!previous || previous->width != subscription->width ||
                       previous->height != subscription->height))
        accepted = UpdateVideoResolution(bound_peer, wire_id.data(),
                                         wire_id.size(), subscription->width,
                                         subscription->height) == 0;
      if (accepted)
        accepted =
            SetVideoSendPaused(bound_peer, wire_id.data(), wire_id.size(),
                               subscription->fps == 0) == 0;
      if (accepted)
        accepted =
            SetAudioSendPaused(bound_peer, wire_id.data(), wire_id.size(),
                               !subscription->audio) == 0;
      if (accepted && subscription->audio)
        runtime->devices_.StartSpeakerCapturer();
    }
    if (accepted) runtime->preview_subscriptions_.Set(remote_id, *subscription);
    const auto reply = nlohmann::json{
        {"type", "deskhub_preview_status"},
        {"version", subscription->version},
        {"request_id", subscription->request_id},
        {"accepted",
         accepted}}.dump();
    SendReliableDataFrameToPeer(bound_peer, reply.data(), reply.size(),
                                runtime->control_data_label_.c_str(),
                                wire_id.data(), wire_id.size());
    return;
  }
  if (preview_json.is_object() &&
      preview_json.value("type", nlohmann::json{}) ==
          "deskhub_preview_status") {
    if (auto props = runtime->FindRemoteSession(remote_id);
        props && source_id == props->control_data_label_) {
      try {
        if ((preview_json.at("version") == 1 ||
             preview_json.at("version") == 2) &&
            preview_json.at("request_id").is_number_unsigned() &&
            preview_json.at("request_id").get<uint64_t>() > 0 &&
            preview_json.at("request_id").get<uint64_t>() <= UINT32_MAX &&
            preview_json.at("accepted").is_boolean()) {
          props->preview_accepted_ = preview_json.at("accepted").get<bool>();
          props->preview_ack_ = preview_json.at("request_id").get<uint32_t>();
        }
      } catch (...) {
      }
    }
    return;
  }
  RemoteAction remote_action{};
  if (!remote_action.from_json(json_str)) {
    LOG_ERROR("Failed to parse RemoteAction JSON payload");
    return;
  }

  if (remote_action.type == ControlType::video_codec_status) {
    if (auto props = runtime->FindRemoteSession(remote_id);
        props && source_id == props->control_data_label_ &&
        !props->closing_ &&
        props->connection_status_ == ConnectionStatus::Connected) {
      props->net_traffic_stats_.StoreRemoteVideoEncoder(remote_action.vcs.encoder);
    }
    return;
  }

  if (remote_action.type == ControlType::video_settings_status) {
    if (auto props = runtime->FindRemoteSession(remote_id)) {
      if (source_id != props->control_data_label_) return;
      std::lock_guard lock(props->video_settings_mutex_);
      if (!props->video_settings_supported_) return;
      if (remote_action.vs.accepted &&
          static_cast<int32_t>(remote_action.vs.request_id -
                               props->applied_video_settings_.request_id) >= 0) {
        props->applied_video_settings_ = remote_action.vs;
      }
      if (remote_action.vs.request_id != props->video_settings_.request_id) return;
      props->video_settings_pending_ = false;
      props->video_settings_failed_ = !remote_action.vs.accepted;
      if (remote_action.vs.accepted) {
        props->video_settings_ = remote_action.vs;
        props->applied_video_settings_ = remote_action.vs;
      } else {
        const auto request_id = props->video_settings_.request_id;
        props->video_settings_ = props->applied_video_settings_;
        props->video_settings_.request_id = request_id;
      }
    }
    return;
  }
  if (remote_action.type == ControlType::video_settings) {
    // Web multiplexes its reliable control actions on a single DataChannel.
    // Accept that transport's source alias while retaining controller admission.
    if ((source_id != runtime->control_data_label_ &&
         source_id != kWebControlStream) ||
        !runtime->IsAuthorizedController(remote_id))
      return;
    const auto& settings = remote_action.vs;
    remote_action.vs.accepted =
        UpdateVideoSettings(
            bound_peer, wire_id.data(), wire_id.size(),
            static_cast<VideoQuality>(settings.quality), settings.frame_rate,
            static_cast<VideoDegradationPreference>(settings.preference)) == 0;
    LOG_INFO("Video settings: peer={} source={} request={} quality={} fps={} "
             "preference={} accepted={}",
             remote_id, source_id, settings.request_id, settings.quality,
             settings.frame_rate, settings.preference, remote_action.vs.accepted);
    remote_action.type = ControlType::video_settings_status;
    const auto message = remote_action.to_json();
    SendReliableDataFrameToPeer(bound_peer, message.data(), message.size(),
                                runtime->control_data_label_.c_str(),
                                wire_id.data(), wire_id.size());
    return;
  }
  if (remote_action.type == ControlType::privacy_status) {
    if (auto props = runtime->FindRemoteSession(remote_id)) {
      if (source_id != props->control_data_label_) return;
      std::lock_guard lock(props->privacy_status_mutex_);
      if (!props->privacy_status_received_ ||
          static_cast<int32_t>(remote_action.ps.revision - props->privacy_status_.revision) >= 0) {
        if (props->privacy_status_received_ &&
            static_cast<int32_t>(remote_action.ps.local_unlock_revision -
                                 props->privacy_status_.local_unlock_revision) > 0)
          props->privacy_unlock_notice_ = true;
        if (remote_action.ps.state == PrivacyState::on)
          props->privacy_unlock_notice_ = false;
        props->privacy_status_ = remote_action.ps;
        props->privacy_status_received_ = true;
        props->privacy_status_tick_ = SDL_GetTicks();
        if (static_cast<int32_t>(remote_action.ps.revision - props->privacy_request_revision_) > 0 &&
            remote_action.ps.state != PrivacyState::starting &&
            remote_action.ps.state != PrivacyState::stopping)
          props->privacy_command_pending_ = false;
      }
    }
    return;
  }
  if (remote_action.type == ControlType::privacy_command) {
    if (runtime->unattended_host_) return;
    if (source_id == runtime->control_data_label_)
      runtime->QueuePrivacyCommand(remote_id, remote_action.pc);
    return;
  }
  if (RemoteInputDispatcher::Accepts(remote_action)) {
    if (!runtime->IsAuthorizedController(remote_id)) return;
    if (remote_action.type == ControlType::text_input &&
        source_id != runtime->keyboard_label_) return;
    if (runtime->remote_input_.Push(remote_id, remote_action) ==
        RemoteInputDispatcher::Result::overflow)
      LOG_WARN("Remote input queue overflow: peer={}, clearing stale input and "
               "releasing keys/buttons",
               remote_id);
    return;
  }
  if (remote_action.type == ControlType::audio_capture) {
    if (!runtime->IsAuthorizedController(remote_id)) return;
#ifdef _WIN32
    if (runtime->privacy_.Engaged() && !IsWindowsPrivacyDesktopAvailable())
      runtime->privacy_.SuspendForDesktop();
#endif
  }
  if (remote_action.type == ControlType::service_status) {
    if (auto props = runtime->FindRemoteSession(remote_id)) {
      runtime->ApplyRemoteServiceStatus(*props, remote_action.ss);
    }
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
          props->remote_cursor_state_.shape != remote_action.cs.shape ||
          props->remote_cursor_state_.render_mode !=
              remote_action.cs.render_mode ||
          props->remote_cursor_state_.hidden_reason !=
              remote_action.cs.hidden_reason;
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
      props->cursor_presentation_.Observe(merged, SDL_GetTicks());
      if (changed) {
        LOG_DEBUG("Received cursor state: seq={}, visible={}, shape={}, "
                 "mode={}, reason={}",
                 remote_action.cs.seq, remote_action.cs.visible,
                 static_cast<int>(remote_action.cs.shape),
                 static_cast<int>(remote_action.cs.render_mode),
                 static_cast<int>(remote_action.cs.hidden_reason));
      }
    }
    return;
  }

  if (remote_action.type == ControlType::host_infomation) {
    if (source_id == runtime->control_data_label_) {
      runtime->transfers_.SetBrowserSupport(handler, remote_id,
          remote_action.i.supports_file_browser);
    }
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
      if (props && source_id == props->control_data_label_) {
        std::lock_guard lock(props->remote_version_mutex_);
        props->remote_host_info_received_ = true;
        props->remote_platform_ = remote_action.i.platform;
        props->recent_platform_ = remote_action.i.platform;
        props->remote_app_version_ = IsValidAppVersion(remote_action.i.app_version)
                                        ? remote_action.i.app_version : "";
      }
      // Negotiate live settings only on the reliable control stream. Old
      // hosts omit this capability and retain their existing video behavior.
      if (props && source_id == props->control_data_label_) {
        std::lock_guard lock(props->video_settings_mutex_);
        const bool first_advertisement = !props->video_settings_supported_;
        props->preview_subscription_supported_ =
            remote_action.i.supports_deskhub_preview;
        props->preview_resolution_supported_ =
            remote_action.i.supports_deskhub_resolution;
        props->preview_fps15_supported_ = remote_action.i.supports_deskhub_fps15;
        props->preview_bandwidth_supported_ = remote_action.i.supports_deskhub_bandwidth;
        props->video_settings_supported_ =
            remote_action.i.supports_video_settings;
        if (first_advertisement && props->video_settings_supported_) {
          RemoteAction settings{};
          settings.type = ControlType::video_settings;
          ++props->video_settings_.request_id;
          props->video_settings_pending_ = true;
          props->video_settings_request_tick_ = SDL_GetTicks();
          settings.vs = props->video_settings_;
          const auto message = settings.to_json();
          SendReliableDataFrame(
              handler->IsDirect() ? handler->DirectPeer() : props->peer_,
              message.data(), message.size(),
              props->control_data_label_.c_str());
        }
      }
      // client mode
      if (props) {
        std::lock_guard lock(props->remote_version_mutex_);
        if (props->remote_host_name_.empty()) {
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
        props->recent_connection_dirty_ = true;
      }
      FreeRemoteAction(remote_action);
    } else {
      // server mode
      std::string host_name(remote_action.i.host_name,
                            remote_action.i.host_name_size);
      {
        std::unique_lock lock(runtime->connection_status_mutex_);
        runtime->connection_host_names_[remote_id] = host_name;
        if (source_id == runtime->control_data_label_) {
          if (remote_action.i.supports_video_codec_status)
            runtime->codec_status_controllers_.insert(remote_id);
          else
            runtime->codec_status_controllers_.erase(remote_id);
        }
      }
      LOG_INFO("Remote hostname: [{}]", host_name);
      if (source_id == runtime->control_data_label_) {
        runtime->SetControllerPrivacySupport(
            remote_id, remote_action.i.supports_privacy_screen);
      }
      FreeRemoteAction(remote_action);
    }
  } else {
    if (remote_action.type == ControlType::audio_capture) {
      // A negotiated subscription remains authoritative if a legacy capture
      // command arrives after it (e.g. when unmuting an inactive window).
      const auto subscription = runtime->preview_subscriptions_.Get(remote_id);
      const bool paused = subscription ? !subscription->audio : !remote_action.a;
      const int ret = SetAudioSendPaused(bound_peer, wire_id.data(),
                                         wire_id.size(), paused);
      if (remote_action.a)
        runtime->devices_.StartSpeakerCapturer();
      else if (ret != 0)
        // Legacy/web transports without the per-peer API retain their fallback.
        runtime->devices_.StopSpeakerCapturer();
    }
  }
}

InputOutcome GuiRuntime::ApplyRemoteInput(const std::string& remote_id,
                                  const RemoteAction& remote_action) {
  if (!IsAuthorizedController(remote_id)) return InputOutcome::ignored;
  if (remote_action.type == ControlType::text_input) {
    return keyboard_.ApplyRemoteText(remote_id, remote_action.text);
  }
#ifdef _WIN32
  const bool keyboard_input = remote_action.type == ControlType::keyboard ||
                              remote_action.type == ControlType::keyboard_state;
  if (!keyboard_input && privacy_.Engaged() &&
      !IsWindowsPrivacyDesktopAvailable())
    privacy_.SuspendForDesktop();
#endif
  if (remote_action.type == ControlType::service_command) {
    if (privacy_.Engaged()) {
      privacy_.Fail(
          "Security shortcut requested; privacy screen will turn off");
    }
#if _WIN32
    if (remote_action.c.flag == ServiceCommandFlag::send_sas) {
      pending_windows_service_sas_.store(true, std::memory_order_relaxed);
      return InputOutcome::applied;
    } else if (remote_action.c.flag == ServiceCommandFlag::lock_workstation) {
      if (!LockWorkStation()) {
        LOG_WARN("Remote lock workstation request failed, error={}",
                 GetLastError());
        return InputOutcome::failed;
      }
      return InputOutcome::applied;
    }
#elif defined(__OHOS__)
    if (!SendHarmonySystemShortcut(remote_action.c.flag)) {
      LOG_WARN(
          "Harmony system shortcut unsupported or unauthorized, command={}",
          static_cast<int>(remote_action.c.flag));
      return InputOutcome::failed;
    }
    return InputOutcome::applied;
#endif
    return InputOutcome::ignored;
  }

  const int display = selected_display_.load();
  if (is_server_mode_ && remote_action.type == ControlType::mouse) {
    std::lock_guard lock(remote_pointer_input_mutex_);
    last_remote_pointer_input_time_[remote_id] =
        std::chrono::steady_clock::now();
  }

  if (remote_action.type == ControlType::mouse) {
    return devices_.ApplyRemoteMouse(remote_id, remote_action.m.flag,
        [this, &remote_action, display] {
          return InjectRemoteMouse(remote_action, display);
        });
  } else if (remote_action.type == ControlType::keyboard) {
    return keyboard_.ApplyRemoteEvent(remote_id, remote_action);
  } else if (remote_action.type == ControlType::keyboard_state) {
    return keyboard_.ApplyRemoteState(remote_id, remote_action);
  } else if (remote_action.type == ControlType::display_id) {
    const int ret = devices_.SwitchDisplay(remote_action.d);
    if (ret == 0) {
      selected_display_ = remote_action.d;
      return InputOutcome::applied;
    } else {
      LOG_WARN("Display switch skipped, invalid display_id={}",
               remote_action.d);
      return InputOutcome::failed;
    }
  }
  return InputOutcome::ignored;
}

InputOutcome GuiRuntime::InjectRemoteMouse(const RemoteAction& remote_action,
                                          int display) {
#if _WIN32
  if (is_server_mode_) {
    static const bool process_elevated = IsAdministratorProcess();
    std::string response;
    const bool delivered = DispatchDesktopInput(
        [&] {
          return PreferUserDesktopInput(
              IsCurrentSessionUserDesktopActive(), windows_consent_ui_.load(),
              local_service_available_.load(), process_elevated);
        },
        [&] {
          SetLastError(ERROR_SUCCESS);
          const bool sent = devices_.SendMouseCommand(remote_action, display);
          const DWORD error = GetLastError();
          const bool blocked =
              error == ERROR_ACCESS_DENIED || error == ERROR_GEN_FAILURE;
          return DesktopInputResult{
              sent, IsDesktopTransitionInputError(error) || blocked, blocked};
        },
        [&]() -> DesktopInputResult {
          int absolute_x = 0;
          int absolute_y = 0;
          if (!BuildAbsoluteMousePosition(devices_.display_info_list(), display,
                                          remote_action.m.x, remote_action.m.y,
                                          &absolute_x, &absolute_y)) {
            LOG_WARN(
                "Secure desktop mouse injection skipped, invalid display "
                "mapping: display_index={}, x={}, y={}",
                display, remote_action.m.x, remote_action.m.y);
            return {};
          }

          response = devices_.SendServiceMouseCommand(
              absolute_x, absolute_y, remote_action.m.s,
              static_cast<int>(remote_action.m.flag));
          auto json = nlohmann::json::parse(response, nullptr, false);
          const bool sent = json.is_object() && json.value("ok", false);
          const std::string error =
              json.is_object() ? json.value("error", "") : "";
          return {sent, IsDesktopInputSetupPending(error)};
        });
    if (!delivered && !response.empty()) {
      LogSecureDesktopInputBlocked(&last_local_secure_input_block_log_tick_,
                                   "local", WindowsInputStage().c_str());
      LOG_WARN(
          "Secure desktop mouse injection failed, x={}, y={}, wheel={}, "
          "flag={}, response={}",
          remote_action.m.x, remote_action.m.y, remote_action.m.s,
          static_cast<int>(remote_action.m.flag), response);
    }
    return delivered ? InputOutcome::applied : InputOutcome::failed;
  }
#endif
  return devices_.SendMouseCommand(remote_action, display)
      ? InputOutcome::applied : InputOutcome::failed;
}

}  // namespace crossdesk
