#include "application/gui_application.h"

#include <SDL3/SDL_timer.h>
#include <stream_names.h>
#include <web_clipboard_protocol.h>
#include <algorithm>
#include <tuple>

#include "interactive_desktop.h"
#include "localization.h"
#include "rd_log.h"
#include "platform/windows/screen_capturer/dxgi_cursor_state.h"

namespace crossdesk {
namespace {
using Json = nlohmann::json;
Json DeviceInfo(const TrustedDevices::Device& device) {
  return {{"key", device.public_key}, {"name", device.name}, {"device", device.device_id}};
}
TrustedDevices::Device ReadDevice(const Json& message) {
  return {message.at("key").get<std::string>(), message.at("name").get<std::string>(),
          message.at("device").get<std::string>()};
}
auto ServerSettings(const ConfigCenter& config) {
  return std::tuple(config.IsSelfHosted(), config.GetSignalServerHost(),
                    config.GetSignalServerPort());
}
}  // namespace

bool GuiRuntime::SendLocalHostCommand(Json message) {
  return local_host_ && local_host_->Send(message);
}

bool GuiRuntime::ForwardUserSessionData(const std::string& id,
    const std::string& stream, const char* data, size_t size) {
  const bool user_stream = stream == file_label_ || stream == file_feedback_label_ ||
      stream == clipboard_label_ || stream == web_clipboard::kStream;
  Json action;
  if ((stream == control_data_label_ || stream == kWebControlStream) && size <= 65536)
    action = Json::parse(data, data + size, nullptr, false);
  const auto type = action.is_object() ? action.value("type", Json{}) : Json{};
  const bool host_info = type == static_cast<int>(ControlType::host_infomation);
  const bool preview = type == "deskhub_preview";
  const bool user_control = type == "file_browser" ||
      type == static_cast<int>(ControlType::privacy_command);
  const bool audio = type == static_cast<int>(ControlType::audio_capture);
  uint64_t epoch = 0;
  {
    std::lock_guard lock(local_host_state_mutex_);
    const auto found = local_controller_info_.find(id);
    if (found == local_controller_info_.end()) return user_stream || user_control;
    epoch = found->second.value("epoch", uint64_t{});
    if (host_info) found->second["host_info"] = action;
    if (preview) found->second["preview"] = action;
  }
  if (local_gui_ready_ && local_host_ &&
      (user_stream || user_control || host_info || preview || audio)) {
    // IPC uses the native label; the Web alias belongs to the SCTP boundary.
    const auto& forwarded_stream = stream == kWebControlStream ? control_data_label_ : stream;
    if (!local_host_->Send({{"op", "data"}, {"id", id}, {"epoch", epoch}, {"stream", forwarded_stream},
        {"bytes", Json::binary(std::vector<uint8_t>(data, data + size))}})) {
      LOG_WARN(
          "Cannot forward user-session data on {}; reconnecting local frontend",
          stream);
      local_gui_ready_ = false;
      // Reset the mirrored sessions so an incomplete reliable stream fails
      // explicitly. Never fall through to file/clipboard handling as SYSTEM.
      local_host_->Reconnect();
    }
  }
  return user_stream || user_control;
}

void GuiRuntime::PublishLocalHostStatus() {
  if (!local_host_ || !local_gui_ready_) return;
  Json controllers = Json::object();
  for (const auto& id : ConnectedControllers()) {
    if (!IsAuthorizedController(id)) continue;
    Json info;
    {
      std::lock_guard lock(local_host_state_mutex_);
      const auto found = local_controller_info_.find(id);
      if (found == local_controller_info_.end()) continue;
      info = found->second;
    }
    if (auto device = VerifiedController(id)) info["verified"] = DeviceInfo(*device);
    controllers[id] = std::move(info);
  }
  bool password_pending = false;
  {
    std::lock_guard lock(password_change_mutex_);
    password_pending = password_change_pending_ || credential_recovery_in_progress_;
  }
  Json status{{"op", "status"}, {"online", signal_connected_.load()},
      {"identity", settings_.ActiveIdentity()}, {"self_hosted", config_center_->IsSelfHosted()},
      {"controllers", std::move(controllers)},
      {"capture", screen_capturer_is_started_ && host_capture_tick_ != 0 &&
                  SDL_GetTicks() - host_capture_tick_.load() < 2000},
      {"cursor_owned", devices_.OwnsNativeCursor()},
      {"password_pending", password_pending}};
  // Settings/trust mutations invalidate this profile; reattachment sends a
  // full snapshot. Frequent heartbeats carry only changed large fields.
  const bool refresh_profile = local_host_profile_dirty_.exchange(false);
  if (refresh_profile) {
    status["config"] = config_center_->ExportSharedHostConfig();
    auto& trusted = status["trusted"] = Json::array();
    if (trusted_devices_)
      for (const auto& device : trusted_devices_->List()) trusted.push_back(DeviceInfo(device));
  }
  for (const char* field : {"config", "trusted", "controllers"}) {
    if (status.contains(field) && local_host_status_.values.contains(field) &&
        status.at(field) == local_host_status_.values.at(field)) status.erase(field);
  }
  if (local_host_->Send(status)) local_host_status_.values.update(status);
  else if (refresh_profile) local_host_profile_dirty_ = true;
}

void GuiApplication::TickLocalHost() {
  if (!local_host_) return;
  const auto error = [this](const std::string& message) {
    if (unattended_host_) local_host_->Send({{"op", "error"}, {"message", message}});
    else {
      offline_warning_text_ = message;
      show_offline_warning_window_ = true;
    }
  };
  const auto deliver = [this](const std::string& id, const std::string& stream,
                              const char* bytes, size_t size) {
    PeerEventHandler::OnReceiveDataBuffer(bytes, size, id.data(), id.size(),
        stream.data(), stream.size(), peer_events_.get());
  };
  const auto remove_mirror = [this](const std::string& id) {
    if (peer_events_) peer_events_->ApplyConnectionStatus(MiniRtcConnectionClosed, id);
    std::lock_guard lock(local_host_state_mutex_);
    mirrored_controllers_.erase(id);
    local_controller_info_.erase(id);
  };
  const auto ensure_mirror = [this, &remove_mirror](const std::string& id, uint64_t epoch) {
    if (!peer_ || !peer_events_ || !epoch || id.empty() || id.size() > 128) return false;
    uint64_t existing = 0;
    {
      std::lock_guard lock(local_host_state_mutex_);
      const auto found = mirrored_controllers_.find(id);
      if (found != mirrored_controllers_.end()) existing = found->second;
    }
    if (existing == epoch) return true;
    local_host_status_.controllers_dirty = true;
    if (existing) remove_mirror(id);
    {
      std::lock_guard lock(local_host_state_mutex_);
      mirrored_controllers_[id] = epoch;
    }
    peer_events_->ApplyConnectionStatus(MiniRtcConnectionConnected, id);
    return true;
  };
  for (auto& message : local_host_->Receive()) {
    try {
      const auto op = message.at("op").get<std::string>();
      if (op == "attached" || op == "detached" || (unattended_host_ && op == "hello")) {
        local_host_status_.Reset();
        local_host_profile_dirty_ = true;
      }
      if (unattended_host_) {
        if (op == "attached" || op == "hello" || op == "detached") {
          local_gui_ready_ = op != "detached";
          if (op == "detached") {
            SharedDxgiCursorState().SetExternalCursorHidden(false);
            devices_.SetNativeCursorPolicy(true, true);
          }
          if (op == "hello") {
            devices_.SetNativeCursorPolicy(message.value("privacy_on_connect", true),
                message.value("privacy_requested", true));
          }
          PublishLocalHostStatus();
          for (const auto& id : ConnectedControllers()) AnnounceHostCapabilities(id);
          LOG_INFO("User-session frontend {}; RTC host remains running",
                   local_gui_ready_ ? "attached" : "detached");
          continue;
        }
        if (!local_gui_ready_) continue;
        if (op == "privacy_cursor") {
          if (message.contains("requested"))
            devices_.SetNativeCursorPolicy(host_cursor_default_, message.at("requested").get<bool>());
          const bool hidden = message.at("hidden").get<bool>();
          if (hidden != SharedDxgiCursorState().ExternalCursorHidden()) {
            SharedDxgiCursorState().SetExternalCursorHidden(hidden);
            LOG_INFO("Host cursor: user-session privacy cursor hidden={}", hidden);
          }
          continue;
        }
        if (op == "data" || op == "audio" || op == "disconnect") {
          const auto id = message.at("id").get<std::string>();
          if (!IsAuthorizedController(id)) continue;
          {
            std::lock_guard lock(local_host_state_mutex_);
            const auto found = local_controller_info_.find(id);
            if (found == local_controller_info_.end() ||
                message.at("epoch") != found->second.at("epoch")) continue;
          }
          if (op == "disconnect") { CloseServerController(id); continue; }
          const auto& bytes = message.at("bytes").get_binary();
          const auto stream = message.at("stream").get<std::string>();
          if (bytes.empty()) continue;
          if (op == "audio") {
            if (stream != audio_label_ || bytes.size() > 65536) continue;
            WithController(id, [&](MiniRtcPeerPtr* peer, const std::string& wire) {
              MiniRtcAudioFrame frame{};
              frame.data = reinterpret_cast<const char*>(bytes.data());
              frame.size = bytes.size();
              frame.captured_timestamp = MiniRtcGetSystemTimeMicros(peer);
              return MiniRtcSendAudioFrameToPeer(peer, &frame, audio_label_.c_str(),
                                               wire.data(), wire.size());
            });
          } else if (stream == file_label_ || stream == file_feedback_label_ ||
                     stream == clipboard_label_ || stream == web_clipboard::kStream ||
                     stream == control_data_label_) {
            SendControllerData(id, reinterpret_cast<const char*>(bytes.data()), bytes.size(), stream.c_str());
          }
        } else if (op == "signal") {
          const auto text = message.at("message").get<std::string>();
          const auto request = Json::parse(text, nullptr, false);
          const auto type = request.is_object() ? request.value("type", Json{}) : Json{};
          // Authentication, credential changes and connection ownership stay
          // exclusively in the persistent host's own signaling exchange.
          if (peer_ && (type == "recent_connections_presence" || type == "announcements_list"))
            MiniRtcSendSignalMessage(peer_, text.data(), text.size());
        } else if (op == "password") {
          if (!RequestPasswordChange(message.at("password").get<std::string>()))
            error("Invalid host password");
        } else if (op == "config") {
          const auto previous = config_center_->ExportSharedHostConfig();
          const auto server = ServerSettings(*config_center_);
          bool ok = config_center_->ApplySharedHostConfig(message.at("config")) == 0;
          const bool reconnect = server != ServerSettings(*config_center_);
          if (ok && reconnect && HasActiveSession()) {
            config_center_->ApplySharedHostConfig(previous);
            ok = false;
          }
          if (ok) {
            devices_.SetNativeCursorPolicy(message.value("privacy_on_connect", true),
                                           host_cursor_preference_);
            ok = PublishSharedIdentity(true);
            ApplySettingsFromConfig(reconnect);
            TickControllerTrust();
          }
          if (!local_host_->Send({{"op", "config_result"}, {"ok", ok},
                                  {"request", message.at("request")}})) {
            LOG_WARN("Cannot acknowledge host settings; reconnecting local frontend");
            local_host_->Reconnect();
          }
          if (!ok) error("Could not apply host settings while a connection is active");
        } else if (op == "connection_settings") {
          const auto turn = message.at("turn").get<int>();
          if (peer_ && turn >= 0 && turn <= 3)
            MiniRtcUpdateConnectionSettings(peer_, true, message.at("av1"),
                                             static_cast<MiniRtcTurnMode>(turn));
        } else if (op == "reconnect") {
          if (peer_ && !HasActiveSession()) MiniRtcReconnectSignal(peer_);
        } else if (op == "trust_add") {
          if (!AddTrustedDevice(ReadDevice(message))) error("Could not add trusted device");
        } else if (op == "trust_remove") {
          if (!RemoveTrustedDevice(message.at("key"))) error("Could not remove trusted device");
        }
        continue;
      }

      if (op == "attached" || op == "detached") {
        SharedDxgiCursorState().SetExternalControllerAvailable(op == "attached");
        local_config_sent_ = false;
        local_cursor_hidden_sent_.reset();
        local_cursor_request_sent_.reset();
        local_cursor_policy_ready_ = false;
      }
      if (op == "attached") {
        local_host_->Send({{"op", "hello"},
            {"privacy_on_connect", config_center_->IsEnablePrivacyScreen()},
            {"privacy_requested", privacy_.Requested() || config_center_->IsEnablePrivacyScreen()}});
      } else if (op == "detached") {
        SharedDxgiCursorState().SetExternalCursorHidden(false);
        signal_connected_ = false;
        device_presence_cache_.SetSignalConnected(false);
        signal_status_ = MiniRtcSignalReconnecting;
        std::vector<std::string> ids;
        {
          std::lock_guard lock(local_host_state_mutex_);
          for (const auto& [id, _] : mirrored_controllers_) ids.push_back(id);
        }
        for (const auto& id : ids) remove_mirror(id);
        privacy_.Disconnected();
      } else if (op == "status" && peer_) {
        // Retain omitted fields across heartbeats, including while a local
        // settings request is pending. Detach/attach clears this snapshot.
        local_host_status_.Merge(message);
        const auto& status = local_host_status_.values;
        // Apply before mirroring Connected/resuming privacy. The GUI then
        // reuses this owner instead of launching another cursor hider.
        SharedDxgiCursorState().SetExternalCursorHidden(status.value("cursor_owned", false));
        const bool online = status.at("online");
        if (online && !signal_connected_) need_to_send_recent_connections_ = true;
        signal_connected_ = online;
        device_presence_cache_.SetSignalConnected(online);
        signal_status_ = online ? MiniRtcSignalConnected : MiniRtcSignalReconnecting;
        if (pending_local_config_.is_null()) {
          if (local_host_status_.config_dirty) {
            const auto& config = status.at("config").get_ref<const std::string&>();
            const bool changed = config != config_center_->ExportSharedHostConfig();
            if (!changed || config_center_->ApplySharedHostConfig(config) == 0) {
              local_host_status_.config_dirty = false;
              if (changed) ApplySettingsFromConfig(false);
            }
          }
          if (!local_host_status_.config_dirty && local_host_status_.identity_dirty) {
            // ApplyHostIdentity may activate the identity before a disk write
            // fails. Retry persistence even if ActiveIdentity already matches.
            local_host_status_.identity_dirty = !settings_.ApplyHostIdentity(
                status.at("identity"), status.at("self_hosted"));
          }
        }
        {
          std::lock_guard lock(password_change_mutex_);
          password_change_pending_ = status.at("password_pending");
          credential_recovery_in_progress_ = false;
          credential_recovery_attempt_pending_ = false;
        }
        if (local_host_status_.controllers_dirty) {
          bool applied = true;
          const auto& controllers = status.at("controllers");
          if (!controllers.empty() && std::all_of(controllers.begin(), controllers.end(),
                  [](const auto& info) { return info.contains("host_info"); }))
            local_cursor_policy_ready_ = true;
          std::vector<std::string> removed;
          {
            std::lock_guard lock(local_host_state_mutex_);
            for (const auto& [id, _] : mirrored_controllers_)
              if (!controllers.contains(id)) removed.push_back(id);
          }
          for (const auto& id : removed) remove_mirror(id);
          for (const auto& [id, info] : controllers.items()) {
            if (!ensure_mirror(id, info.at("epoch"))) { applied = false; continue; }
            if (info.contains("verified")) {
              std::lock_guard lock(controller_trust_mutex_);
              auto& trust = controller_trust_[id];
              trust.events = peer_events_.get();
              trust.verified = ReadDevice(info.at("verified"));
            }
            for (const char* field : {"host_info", "preview"}) {
              if (!info.contains(field)) continue;
              bool changed = false;
              {
                std::lock_guard lock(local_host_state_mutex_);
                auto& previous = local_controller_info_[id][field];
                changed = previous != info.at(field);
                previous = info.at(field);
              }
              if (changed) {
                const auto data = info.at(field).dump();
                deliver(id, control_data_label_, data.data(), data.size());
              }
            }
          }
          local_host_status_.controllers_dirty = !applied;
        }
        if (trusted_devices_ && local_host_status_.trusted_dirty) {
          std::vector<TrustedDevices::Device> current;
          for (const auto& item : status.at("trusted")) current.push_back(ReadDevice(item));
          trusted_devices_->Replace(current);
          local_host_status_.trusted_dirty = false;
        }
        if (IsCurrentSessionUserDesktopActive()) {
          privacy_.CaptureChanged(status.at("capture"));
          privacy_.ResumeAfterDesktopRecovery();
        } else privacy_.SuspendForDesktop();
      } else if (op == "data") {
        const auto id = message.at("id").get<std::string>();
        if (!ensure_mirror(id, message.at("epoch"))) continue;
        const auto stream = message.at("stream").get<std::string>();
        const auto& data = message.at("bytes").get_binary();
        deliver(id, stream, reinterpret_cast<const char*>(data.data()), data.size());
      } else if (op == "signal" && peer_events_) {
        const auto data = message.at("message").get<std::string>();
        PeerEventHandler::OnSignalMessage(data.data(), data.size(), peer_events_.get());
      } else if (op == "config_result") {
        if (!pending_local_config_.is_null() && message.at("request") == pending_local_config_.at("request")) {
          pending_local_config_ = Json{};
          local_host_status_.config_dirty = local_host_status_.identity_dirty = true;
        }
      } else if (op == "error") {
        error(message.at("message"));
      }
    } catch (const std::exception&) {
      LOG_WARN("Ignored malformed local host message");
    }
  }
  if (managed_host_frontend_ && !pending_local_config_.is_null() && !local_config_sent_)
    local_config_sent_ = local_host_->Send(pending_local_config_);
  if (managed_host_frontend_) {
    // Report only the GUI's own helper and privacy intent. Echoing a borrowed
    // host owner back as GUI-owned would create a feedback loop on release.
    const bool hidden = SharedDxgiCursorState().LocalPrivacyCursorHidden();
    const bool requested = privacy_.Requested();
    if (requested || !config_center_->IsEnablePrivacyScreen()) local_cursor_policy_ready_ = true;
    if (local_cursor_hidden_sent_ != hidden ||
        (local_cursor_policy_ready_ && local_cursor_request_sent_ != requested)) {
      Json command{{"op", "privacy_cursor"}, {"hidden", hidden}};
      if (local_cursor_policy_ready_) command["requested"] = requested;
      if (local_host_->Send(command)) {
        local_cursor_hidden_sent_ = hidden;
        if (local_cursor_policy_ready_) local_cursor_request_sent_ = requested;
      }
    }
  }
  if (unattended_host_ && GetTickCount64() >= next_local_host_status_) {
    next_local_host_status_ = GetTickCount64() + 250;
    PublishLocalHostStatus();
    if (show_offline_warning_window_) {
      error(offline_warning_text_);
      show_offline_warning_window_ = false;
    }
  }
}
}  // namespace crossdesk
