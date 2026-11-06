/*
 * @Author: DI JUNKUN
 * @Date: 2024-11-22
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _KEYBOARD_CAPTURER_H_
#define _KEYBOARD_CAPTURER_H_

#include <ApplicationServices/ApplicationServices.h>

#include "device_controller.h"
#include "macos_keyboard_modifier_state.h"

namespace crossdesk {

class PlatformKeyboardCapturer final : public KeyboardCapturer {
 public:
  PlatformKeyboardCapturer();
  ~PlatformKeyboardCapturer() override;

 public:
  int Hook(OnKeyAction on_key_action, void* user_ptr) override;
  int Unhook() override;
  bool SupportsTextInput() const override { return true; }
  int SendTextCommand(const std::string& text) override;
  int SendKeyboardCommand(int key_code, bool is_down,
                          uint32_t scan_code = 0,
                          bool extended = false) override;

 private:
  CFMachPortRef event_tap_ = nullptr;
  CFRunLoopSourceRef run_loop_source_ = nullptr;

 public:
  bool caps_lock_flag_ = false;
  bool shift_flag_ = false;
  bool control_flag_ = false;
  bool option_flag_ = false;
  bool command_flag_ = false;
  int fn_key_code_ = 0x3F;
  MacKeyboardModifierState injected_modifier_state_;
};
}  // namespace crossdesk
#endif
