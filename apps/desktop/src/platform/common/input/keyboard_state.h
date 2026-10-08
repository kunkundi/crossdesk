/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _KEYBOARD_STATE_H_
#define _KEYBOARD_STATE_H_

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

namespace crossdesk {

struct KeyboardKey {
  int key_code = 0;
  uint32_t scan_code = 0;
  bool extended = false;
};

using PressedKeys = std::map<int, KeyboardKey>;

struct KeyboardInput {
  int key_code = 0;
  bool is_down = false;
  uint32_t scan_code = 0;
  bool extended = false;
};

inline bool IsKeyboardModifier(int key_code) {
  return (key_code >= 0x10 && key_code <= 0x12) ||
         (key_code >= 0xA0 && key_code <= 0xA5) ||
         key_code == 0x5B || key_code == 0x5C;
}

// Reconcile state after capture overflow without replaying discarded shortcuts.
// Keep keys that are still held, release stale keys, then restore modifiers
// before ordinary keys. Metadata matters for Enter/numpad Enter, etc.
inline std::vector<KeyboardInput> ReconcileKeyboardState(
    const PressedKeys& current, const PressedKeys& desired) {
  const auto same_key = [](const KeyboardKey& a, const KeyboardKey& b) {
    return a.scan_code == b.scan_code && a.extended == b.extended;
  };
  std::vector<KeyboardInput> result;
  for (const auto& [code, key] : current) {
    const auto it = desired.find(code);
    if (it == desired.end() || !same_key(key, it->second))
      result.push_back({code, false, key.scan_code, key.extended});
  }
  // Release ordinary keys before modifiers as well.
  std::stable_partition(result.begin(), result.end(), [](const auto& key) {
    return !IsKeyboardModifier(key.key_code);
  });
  const auto releases = result.size();
  for (const auto& [code, key] : desired) {
    const auto it = current.find(code);
    if (it == current.end() || !same_key(key, it->second))
      result.push_back({code, true, key.scan_code, key.extended});
  }
  std::stable_partition(result.begin() + releases, result.end(),
                        [](const auto& key) {
                          return IsKeyboardModifier(key.key_code);
                        });
  return result;
}

}  // namespace crossdesk

#endif