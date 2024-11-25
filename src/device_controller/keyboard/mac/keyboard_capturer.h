/*
 * @Author: DI JUNKUN
 * @Date: 2024-11-22
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _KEYBOARD_CAPTURER_H_
#define _KEYBOARD_CAPTURER_H_

#include <ApplicationServices/ApplicationServices.h>

#include "device_controller.h"

class KeyboardCapturer : public DeviceController {
 public:
  KeyboardCapturer();
  virtual ~KeyboardCapturer();

 public:
  virtual int Hook(OnKeyAction on_key_action, void *user_ptr);
  virtual int Unhook();
  virtual int SendKeyboardCommand(int key_code, bool is_down);

 private:
  CFMachPortRef event_tap_;
  CFRunLoopSourceRef run_loop_source_;

 public:
  bool caps_lock_flag_ = false;
  bool shift_flag_ = false;
  bool control_flag_ = false;
  bool option_flag_ = false;
  bool command_flag_ = false;
};

#endif