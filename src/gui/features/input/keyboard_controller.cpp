#include "features/input/keyboard_controller.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "minirtc.h"
#include "rd_log.h"
#include "runtime/gui_runtime.h"
#include "windows_key_metadata.h"
#if _WIN32
#include "interactive_state.h"
#include "service_host.h"
#endif

namespace crossdesk {
namespace {

constexpr uint32_t kHeartbeatIntervalMs = 500;
constexpr uint32_t kRemoteReleaseTimeoutMs = 2500;

int NormalizeWindowsModifierVk(int key_code, uint32_t scan_code,
                               bool extended) {
#if _WIN32
  if (key_code != 0x10 && key_code != 0x11 && key_code != 0x12) {
    return key_code;
  }

  UINT scan_code_with_prefix = static_cast<UINT>(scan_code & 0xFF);
  if (extended) {
    scan_code_with_prefix |= 0xE000;
  }
  const UINT normalized_vk =
      MapVirtualKeyW(scan_code_with_prefix, MAPVK_VSC_TO_VK_EX);
  return normalized_vk != 0 ? static_cast<int>(normalized_vk) : key_code;
#else
  (void)scan_code;
  (void)extended;
  return key_code;
#endif
}

void PopulateWindowsKeyMetadataFromVk(int key_code, uint32_t* scan_code_out,
                                      bool* extended_out) {
  if (!scan_code_out || !extended_out) {
    return;
  }
#if _WIN32
  const UINT scan_code =
      MapVirtualKeyW(static_cast<UINT>(key_code), MAPVK_VK_TO_VSC_EX);
  if (scan_code != 0) {
    *scan_code_out = static_cast<uint32_t>(scan_code & 0xFF);
    *extended_out = (scan_code & 0xFF00) != 0;
    return;
  }
#endif
  LookupWindowsKeyMetadataFromVk(key_code, scan_code_out, extended_out);
}

#if _WIN32
constexpr uint32_t kSecureDesktopInputLogIntervalMs = 2000;

void LogSecureDesktopInputBlocked(uint32_t* last_tick, const char* stage) {
  const uint32_t now = static_cast<uint32_t>(SDL_GetTicks());
  if (*last_tick != 0 && now - *last_tick < kSecureDesktopInputLogIntervalMs) {
    return;
  }
  *last_tick = now;
  LOG_WARN(
      "local secure-desktop input blocked, stage={}, normal SendInput path "
      "cannot drive the Windows password UI",
      stage ? stage : "");
}

bool IsTransientSecureDesktopInputFailure(const nlohmann::json& response,
                                          const RemoteAction& action) {
  return response.is_object() &&
         response.value("error", std::string()) == "send_input_failed" &&
         response.value("code", 0u) == ERROR_ACCESS_DENIED &&
         action.type == ControlType::keyboard &&
         action.k.flag == KeyFlag::key_up;
}
#endif

}  // namespace

KeyboardController::KeyboardController(GuiRuntime& owner) : owner_(owner) {}

void KeyboardController::TrackPressedKey(int key_code, bool is_down,
                                         uint32_t scan_code, bool extended) {
  std::lock_guard<std::mutex> lock(pressed_keys_mutex_);
  if (is_down) {
    pressed_keys_[key_code] = PressedKey{key_code, scan_code, extended};
  } else {
    pressed_keys_.erase(key_code);
  }
}

void KeyboardController::ForceReleasePressedKeys() {
  std::vector<PressedKey> pressed_keys;
  {
    std::lock_guard<std::mutex> lock(pressed_keys_mutex_);
    pressed_keys.reserve(pressed_keys_.size());
    for (const auto& [_, key] : pressed_keys_) {
      pressed_keys.push_back(key);
    }
    pressed_keys_.clear();
  }

  for (const PressedKey& key : pressed_keys) {
    SendKeyCommand(key.key_code, false, key.scan_code, key.extended);
  }
  SendHeartbeat(true);
}

void KeyboardController::SendHeartbeat(bool force) {
  const uint32_t now = static_cast<uint32_t>(SDL_GetTicks());
  if (!force && now - last_heartbeat_tick_ < kHeartbeatIntervalMs) {
    return;
  }

  RemoteAction action{};
  action.type = ControlType::keyboard_state;
  action.ks.seq = ++state_sequence_;
  {
    std::lock_guard<std::mutex> lock(pressed_keys_mutex_);
    size_t index = 0;
    for (const auto& [_, key] : pressed_keys_) {
      if (index >= kMaxKeyboardStateKeys) {
        LOG_WARN("Keyboard heartbeat truncated, pressed_keys={}",
                 pressed_keys_.size());
        break;
      }
      action.ks.pressed_keys[index].key_value =
          static_cast<size_t>(key.key_code);
      action.ks.pressed_keys[index].scan_code = key.scan_code;
      action.ks.pressed_keys[index].extended = key.extended;
      ++index;
    }
    action.ks.pressed_count = index;
  }

  const std::string target_id = owner_.controlled_remote_id_.empty()
                                    ? owner_.focused_remote_id_
                                    : owner_.controlled_remote_id_;
  const auto props_it = owner_.remote_sessions_.find(target_id);
  if (target_id.empty() || props_it == owner_.remote_sessions_.end() ||
      props_it->second->connection_status_.load() !=
          ConnectionStatus::Connected ||
      !props_it->second->peer_) {
    last_heartbeat_tick_ = now;
    return;
  }

  const std::string message = action.to_json();
  const int result = SendReliableDataFrame(
      props_it->second->peer_, message.c_str(), message.size(),
      props_it->second->keyboard_label_.c_str());
  if (result != 0) {
    LOG_WARN("Send keyboard heartbeat failed, remote_id={}, ret={}", target_id,
             result);
  }
  last_heartbeat_tick_ = now;
}

int KeyboardController::SendKeyCommand(int key_code, bool is_down,
                                       uint32_t scan_code, bool extended) {
  if (scan_code == 0) {
    PopulateWindowsKeyMetadataFromVk(key_code, &scan_code, &extended);
  }
#if _WIN32
  key_code = NormalizeWindowsModifierVk(key_code, scan_code, extended);
#endif

  RemoteAction action{};
  action.type = ControlType::keyboard;
  action.k.flag = is_down ? KeyFlag::key_down : KeyFlag::key_up;
  action.k.key_value = key_code;
  action.k.scan_code = scan_code;
  action.k.extended = extended;

  const std::string target_id = owner_.controlled_remote_id_.empty()
                                    ? owner_.focused_remote_id_
                                    : owner_.controlled_remote_id_;
  const auto props_it = owner_.remote_sessions_.find(target_id);
  if (!target_id.empty() && props_it != owner_.remote_sessions_.end() &&
      props_it->second->connection_status_.load() ==
          ConnectionStatus::Connected &&
      props_it->second->peer_) {
    const std::string message = action.to_json();
    const int result = SendReliableDataFrame(
        props_it->second->peer_, message.c_str(), message.size(),
        props_it->second->keyboard_label_.c_str());
    if (result != 0) {
      LOG_WARN("Send keyboard command failed, remote_id={}, ret={}", target_id,
               result);
    }
  }

  TrackPressedKey(key_code, is_down, scan_code, extended);
  return 0;
}

bool KeyboardController::InjectRemoteKey(int key_code, bool is_down,
                                         uint32_t scan_code, bool extended) {
#if _WIN32
  if (owner_.local_service_status_received_ &&
      IsSecureDesktopInteractionRequired(owner_.local_interactive_stage_)) {
    const std::string response = SendCrossDeskSecureDesktopKeyInput(
        key_code, is_down, scan_code, extended, 1000);
    const auto json = nlohmann::json::parse(response, nullptr, false);
    if (json.is_discarded() || !json.value("ok", false)) {
      RemoteAction action{};
      action.type = ControlType::keyboard;
      action.k.key_value = static_cast<size_t>(key_code);
      action.k.scan_code = scan_code;
      action.k.extended = extended;
      action.k.flag = is_down ? KeyFlag::key_down : KeyFlag::key_up;
      if (!json.is_discarded() &&
          IsTransientSecureDesktopInputFailure(json, action)) {
        LOG_INFO(
            "Secure desktop keyboard injection transient failure, "
            "key_code={}, is_down={}, response={}",
            key_code, is_down, response);
        return true;
      }

      LogSecureDesktopInputBlocked(
          &owner_.last_local_secure_input_block_log_tick_,
          owner_.local_interactive_stage_.c_str());
      LOG_WARN(
          "Secure desktop keyboard injection failed, key_code={}, is_down={}, "
          "response={}",
          key_code, is_down, response);
      return false;
    }
    return true;
  }
#endif
  return owner_.devices_.SendKeyboardCommand(key_code, is_down, scan_code,
                                             extended);
}

void KeyboardController::ApplyRemoteEvent(const std::string& remote_id,
                                          const RemoteAction& action) {
  const int key_code = static_cast<int>(action.k.key_value);
  const bool is_down = action.k.flag == KeyFlag::key_down;
  const bool injected =
      InjectRemoteKey(key_code, is_down, action.k.scan_code, action.k.extended);

  std::lock_guard<std::mutex> lock(remote_states_mutex_);
  auto& state = remote_states_[remote_id];
  state.last_seen_tick = static_cast<uint32_t>(SDL_GetTicks());
  if (is_down && injected) {
    state.pressed_keys[key_code] =
        PressedKey{key_code, action.k.scan_code, action.k.extended};
  } else if (!is_down && injected) {
    state.pressed_keys.erase(key_code);
  }
}

void KeyboardController::ApplyRemoteState(const std::string& remote_id,
                                          const RemoteAction& action) {
  std::vector<PressedKey> keys_to_release;
  std::vector<PressedKey> keys_to_press;
  {
    std::lock_guard<std::mutex> lock(remote_states_mutex_);
    auto& state = remote_states_[remote_id];
    if (action.ks.seq != 0 && state.last_seq != 0 &&
        static_cast<int32_t>(action.ks.seq - state.last_seq) <= 0) {
      return;
    }

    state.last_seq = action.ks.seq;
    state.last_seen_tick = static_cast<uint32_t>(SDL_GetTicks());
    state.keyboard_state_seen = true;

    std::unordered_map<int, PressedKey> desired_keys;
    const size_t count =
        (std::min)(action.ks.pressed_count, kMaxKeyboardStateKeys);
    for (size_t index = 0; index < count; ++index) {
      const auto& key = action.ks.pressed_keys[index];
      const int key_code = static_cast<int>(key.key_value);
      desired_keys[key_code] =
          PressedKey{key_code, key.scan_code, key.extended};
    }
    for (const auto& [key_code, key] : state.pressed_keys) {
      if (desired_keys.find(key_code) == desired_keys.end()) {
        keys_to_release.push_back(key);
      }
    }
    for (const auto& [key_code, key] : desired_keys) {
      if (state.pressed_keys.find(key_code) == state.pressed_keys.end()) {
        keys_to_press.push_back(key);
      }
    }
  }

  for (const PressedKey& key : keys_to_release) {
    if (InjectRemoteKey(key.key_code, false, key.scan_code, key.extended)) {
      std::lock_guard<std::mutex> lock(remote_states_mutex_);
      const auto state_it = remote_states_.find(remote_id);
      if (state_it != remote_states_.end()) {
        state_it->second.pressed_keys.erase(key.key_code);
      }
    }
  }
  for (const PressedKey& key : keys_to_press) {
    if (InjectRemoteKey(key.key_code, true, key.scan_code, key.extended)) {
      std::lock_guard<std::mutex> lock(remote_states_mutex_);
      remote_states_[remote_id].pressed_keys[key.key_code] = key;
    }
  }
}

void KeyboardController::ReleaseRemotePressedKeys(const std::string& remote_id,
                                                  const char* reason) {
  std::vector<PressedKey> keys_to_release;
  {
    std::lock_guard<std::mutex> lock(remote_states_mutex_);
    const auto state_it = remote_states_.find(remote_id);
    if (state_it == remote_states_.end()) {
      return;
    }
    for (const auto& [_, key] : state_it->second.pressed_keys) {
      keys_to_release.push_back(key);
    }
    remote_states_.erase(state_it);
  }

  if (!keys_to_release.empty()) {
    LOG_WARN("Releasing {} remote keyboard keys for remote_id={}, reason={}",
             keys_to_release.size(), remote_id, reason ? reason : "unknown");
  }
  for (const PressedKey& key : keys_to_release) {
    InjectRemoteKey(key.key_code, false, key.scan_code, key.extended);
  }
}

void KeyboardController::CheckRemoteTimeouts() {
  const uint32_t now = static_cast<uint32_t>(SDL_GetTicks());
  std::vector<std::string> timed_out_remotes;
  {
    std::lock_guard<std::mutex> lock(remote_states_mutex_);
    for (const auto& [remote_id, state] : remote_states_) {
      if (state.keyboard_state_seen && !state.pressed_keys.empty() &&
          state.last_seen_tick != 0 &&
          now - state.last_seen_tick > kRemoteReleaseTimeoutMs) {
        timed_out_remotes.push_back(remote_id);
      }
    }
  }
  for (const std::string& remote_id : timed_out_remotes) {
    ReleaseRemotePressedKeys(remote_id, "keyboard_heartbeat_timeout");
  }
}

}  // namespace crossdesk
