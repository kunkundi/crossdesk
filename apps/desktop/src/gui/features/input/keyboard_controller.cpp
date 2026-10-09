#include "features/input/keyboard_controller.h"

#include <remote_action.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "minirtc.h"
#include "rd_log.h"
#include "runtime/gui_runtime.h"
#include "windows_key_metadata.h"
#if _WIN32
#include "desktop_transition_policy.h"
#include "interactive_desktop.h"
#include "service_host.h"
#include "unattended_config.h"
#endif

namespace crossdesk {
namespace {

constexpr uint32_t kHeartbeatIntervalMs = 500;
constexpr uint32_t kRemoteReleaseTimeoutMs = 2500;
constexpr uint32_t kReleaseRetryIntervalMs = 250;

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
  if (LookupWindowsKeyMetadataFromVk(key_code, scan_code_out, extended_out)) {
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
                                          bool is_down) {
  return response.is_object() &&
         response.value("error", std::string()) == "send_input_failed" &&
         response.value("code", 0u) == ERROR_ACCESS_DENIED &&
         !is_down;
}
#endif

}  // namespace

KeyboardController::KeyboardController(GuiRuntime& owner) : owner_(owner) {}

void KeyboardController::TrackPressedKey(int key_code, bool is_down,
                                         uint32_t scan_code, bool extended) {
  std::lock_guard<std::mutex> lock(pressed_keys_mutex_);
  if (is_down) {
    pressed_keys_[key_code] = KeyboardKey{key_code, scan_code, extended};
  } else {
    pressed_keys_.erase(key_code);
  }
}

void KeyboardController::ForceReleasePressedKeys() {
  PressedKeys pressed_keys;
  {
    std::lock_guard<std::mutex> lock(pressed_keys_mutex_);
    pressed_keys.swap(pressed_keys_);
  }

  for (const auto& [_, key] : pressed_keys) {
    SendKeyCommand(key.key_code, false, key.scan_code, key.extended);
  }
  SendHeartbeat(true);
}

void KeyboardController::RecoverCapturedState(const PressedKeys& pressed) {
  std::vector<KeyboardInput> changes;
  {
    std::lock_guard lock(pressed_keys_mutex_);
    changes = ReconcileKeyboardState(pressed_keys_, pressed);
  }
  for (const auto& event : changes) {
    SendKeyCommand(event.key_code, event.is_down, event.scan_code,
                    event.extended);
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
  std::shared_lock sessions_lock(owner_.remote_sessions_mutex_);
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
  std::shared_lock sessions_lock(owner_.remote_sessions_mutex_);
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
  if (owner_.privacy_.Engaged() && !IsWindowsPrivacyDesktopAvailable()) {
    owner_.privacy_.SuspendForDesktop();
  }
  if (owner_.is_server_mode_ || !owner_.WindowsInputStage().empty()) {
    static const bool process_elevated = IsAdministratorProcess();
    return DispatchDesktopInput(
        [&] {
          return PreferUserDesktopInput(IsCurrentSessionUserDesktopActive(),
                                        owner_.windows_consent_ui_.load(),
                                        owner_.local_service_available_.load(),
                                        process_elevated);
        },
        [&] {
          SetLastError(ERROR_SUCCESS);
          const bool sent = owner_.devices_.SendKeyboardCommand(
              key_code, is_down, scan_code, extended);
          const DWORD error = GetLastError();
          const bool blocked =
              error == ERROR_ACCESS_DENIED || error == ERROR_GEN_FAILURE;
          return DesktopInputResult{
              sent, IsDesktopTransitionInputError(error) || blocked, blocked};
        },
        [&]() -> DesktopInputResult {
          const std::string response = SendCrossDeskSecureDesktopKeyInput(
              key_code, is_down, scan_code, extended, 1000);
          const auto json = nlohmann::json::parse(response, nullptr, false);
          if (!json.is_object() || !json.value("ok", false)) {
            const std::string error =
                json.is_object() ? json.value("error", "") : "";
            if (IsDesktopInputSetupPending(error)) {
              return {false, true};
            }
            if (IsTransientSecureDesktopInputFailure(json, is_down)) {
              LOG_INFO(
                  "Secure desktop keyboard injection transient failure, "
                  "key_code={}, is_down={}, response={}",
                  key_code, is_down, response);
              // The release was not injected. Keep it tracked so cleanup can
              // retry on the ordinary desktop after the transition.
              return {false, false};
            }

            LogSecureDesktopInputBlocked(
                &owner_.last_local_secure_input_block_log_tick_,
                owner_.WindowsInputStage().c_str());
            LOG_WARN(
                "Secure desktop keyboard injection failed, key_code={}, "
                "is_down={}, "
                "response={}",
                key_code, is_down, response);
            return {};
          }
          return {true, false};
        });
  }
#endif
  return owner_.devices_.SendKeyboardCommand(key_code, is_down, scan_code,
                                             extended);
}

void KeyboardController::ApplyRemoteEvent(const std::string& remote_id,
                                          const RemoteAction& action) {
  std::lock_guard input_lock(remote_input_mutex_);
  const int key_code = static_cast<int>(action.k.key_value);
  const bool is_down = action.k.flag == KeyFlag::key_down;
  const bool injected =
      InjectRemoteKey(key_code, is_down, action.k.scan_code, action.k.extended);
  if (injected) pending_key_releases_.erase(key_code);

  auto& state = remote_states_[remote_id];
  state.last_seen_tick = static_cast<uint32_t>(SDL_GetTicks());
  if (is_down && injected) {
    state.pressed_keys[key_code] =
        KeyboardKey{key_code, action.k.scan_code, action.k.extended};
  } else if (!is_down && injected) {
    state.pressed_keys.erase(key_code);
  }
}

void KeyboardController::ApplyRemoteState(const std::string& remote_id,
                                          const RemoteAction& action) {
  std::lock_guard input_lock(remote_input_mutex_);
  auto& state = remote_states_[remote_id];
  if (action.ks.seq != 0 && state.last_seq != 0 &&
      static_cast<int32_t>(action.ks.seq - state.last_seq) <= 0) {
    return;
  }

  state.last_seq = action.ks.seq;
  state.last_seen_tick = static_cast<uint32_t>(SDL_GetTicks());
  state.keyboard_state_seen = true;

  PressedKeys desired_keys;
  const size_t count =
      (std::min)(action.ks.pressed_count, kMaxKeyboardStateKeys);
  for (size_t index = 0; index < count; ++index) {
    const auto& key = action.ks.pressed_keys[index];
    const int key_code = static_cast<int>(key.key_value);
    desired_keys[key_code] = {key_code, key.scan_code, key.extended};
  }
  const auto changes = ReconcileKeyboardState(state.pressed_keys, desired_keys);
  bool can_press = true;
  for (const auto& key : changes) {
    if (key.is_down && !can_press) continue;
    if (!InjectRemoteKey(key.key_code, key.is_down, key.scan_code, key.extended)) {
      // Do not replay a shortcut with a stale/missing modifier. The next
      // heartbeat will retry from the state that was actually injected.
      can_press = false;
      continue;
    }
    pending_key_releases_.erase(key.key_code);
    if (key.is_down)
      state.pressed_keys[key.key_code] =
          {key.key_code, key.scan_code, key.extended};
    else
      state.pressed_keys.erase(key.key_code);
  }
}

void KeyboardController::ReleaseRemotePressedKeys(const std::string& remote_id,
                                                  const char* reason) {
  std::lock_guard input_lock(remote_input_mutex_);
  ReleaseRemotePressedKeysLocked(remote_id, reason);
}

void KeyboardController::ReleaseRemotePressedKeysLocked(
    std::string remote_id, const char* reason) {
  const auto state_it = remote_states_.find(remote_id);
  if (state_it == remote_states_.end()) return;
  auto keys_to_release = std::move(state_it->second.pressed_keys);
  remote_states_.erase(state_it);

  if (!keys_to_release.empty()) {
    LOG_WARN("Releasing {} remote keyboard keys for remote_id={}, reason={}",
             keys_to_release.size(), remote_id, reason ? reason : "unknown");
  }
  for (const auto& [code, key] : keys_to_release) {
    if (!InjectRemoteKey(key.key_code, false, key.scan_code, key.extended)) {
      pending_key_releases_[code] = key;
    } else {
      pending_key_releases_.erase(code);
    }
  }
}

void KeyboardController::ReleaseAllRemotePressedKeys(const char* reason) {
  {
    std::lock_guard input_lock(remote_input_mutex_);
    while (!remote_states_.empty())
      ReleaseRemotePressedKeysLocked(remote_states_.begin()->first, reason);
  }
  CheckRemoteTimeouts();
}

void KeyboardController::CheckRemoteTimeouts() {
  // Do not stall the UI while a transport thread waits on desktop IPC.
  std::unique_lock input_lock(remote_input_mutex_, std::try_to_lock);
  if (!input_lock.owns_lock()) return;
  const uint32_t now = static_cast<uint32_t>(SDL_GetTicks());
  // Retain failed releases in ordinary mode as well as privacy mode. Retry
  // only on the ordinary desktop, never through the security UI helper.
  if (!pending_key_releases_.empty() &&
      (last_release_retry_tick_ == 0 ||
       now - last_release_retry_tick_ >= kReleaseRetryIntervalMs)
#ifdef _WIN32
      && IsCurrentSessionUserDesktopActive()
#endif
  ) {
    last_release_retry_tick_ = now;
    for (auto it = pending_key_releases_.begin();
         it != pending_key_releases_.end();) {
      const auto& key = it->second;
      if (owner_.devices_.SendKeyboardCommand(
              key.key_code, false, key.scan_code, key.extended))
        it = pending_key_releases_.erase(it);
      else
        ++it;
    }
  }

  // Keep the timeout decision and release under the same lock, so a fresh
  // heartbeat cannot arrive between selecting a stale peer and clearing it.
  for (auto it = remote_states_.begin(); it != remote_states_.end();) {
    const auto& state = it->second;
    if (state.keyboard_state_seen && !state.pressed_keys.empty() &&
        state.last_seen_tick != 0 &&
        now - state.last_seen_tick > kRemoteReleaseTimeoutMs) {
      const auto expired = it++;
      ReleaseRemotePressedKeysLocked(expired->first, "keyboard_heartbeat_timeout");
    } else {
      ++it;
    }
  }
}

}  // namespace crossdesk
