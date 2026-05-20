/*
 * @Author: DI JUNKUN
 * @Date: 2026-05-21
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MACOS_KEYBOARD_MODIFIER_STATE_H_
#define _MACOS_KEYBOARD_MODIFIER_STATE_H_

#include <cstdint>

namespace crossdesk {

inline constexpr uint32_t kMacInjectedModifierShift = 1u << 0;
inline constexpr uint32_t kMacInjectedModifierControl = 1u << 1;
inline constexpr uint32_t kMacInjectedModifierOption = 1u << 2;
inline constexpr uint32_t kMacInjectedModifierCommand = 1u << 3;

class MacKeyboardModifierState {
 public:
  uint32_t Update(int key_code, bool is_down) {
    bool* state = MutableStateForVk(key_code);
    if (state != nullptr) {
      *state = is_down;
    }
    return flags();
  }

  uint32_t flags() const {
    uint32_t result = 0;
    if (left_shift_down_ || right_shift_down_) {
      result |= kMacInjectedModifierShift;
    }
    if (left_control_down_ || right_control_down_) {
      result |= kMacInjectedModifierControl;
    }
    if (left_option_down_ || right_option_down_) {
      result |= kMacInjectedModifierOption;
    }
    if (left_command_down_ || right_command_down_) {
      result |= kMacInjectedModifierCommand;
    }
    return result;
  }

  void Clear() {
    left_shift_down_ = false;
    right_shift_down_ = false;
    left_control_down_ = false;
    right_control_down_ = false;
    left_option_down_ = false;
    right_option_down_ = false;
    left_command_down_ = false;
    right_command_down_ = false;
  }

 private:
  bool* MutableStateForVk(int key_code) {
    switch (key_code) {
      case 0xA0:  // VK_LSHIFT
        return &left_shift_down_;
      case 0xA1:  // VK_RSHIFT
        return &right_shift_down_;
      case 0xA2:  // VK_LCONTROL
        return &left_control_down_;
      case 0xA3:  // VK_RCONTROL
        return &right_control_down_;
      case 0xA4:  // VK_LMENU / left Option
        return &left_option_down_;
      case 0xA5:  // VK_RMENU / right Option
        return &right_option_down_;
      case 0x5B:  // VK_LWIN / left Command
        return &left_command_down_;
      case 0x5C:  // VK_RWIN / right Command
        return &right_command_down_;
      default:
        return nullptr;
    }
  }

  bool left_shift_down_ = false;
  bool right_shift_down_ = false;
  bool left_control_down_ = false;
  bool right_control_down_ = false;
  bool left_option_down_ = false;
  bool right_option_down_ = false;
  bool left_command_down_ = false;
  bool right_command_down_ = false;
};

}  // namespace crossdesk

#endif
