/*
 * @Author: DI JUNKUN
 * @Date: 2024-11-22
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _KEYBOARD_CAPTURER_H_
#define _KEYBOARD_CAPTURER_H_

#include <Windows.h>

#include "device_controller.h"

namespace crossdesk {

class KeyboardCapturer : public DeviceController {
 public:
  KeyboardCapturer();
  virtual ~KeyboardCapturer();

 public:
  virtual int Hook(OnKeyAction on_key_action, void* user_ptr);
  virtual int Unhook();
  virtual int SendKeyboardCommand(int key_code, bool is_down,
                                  uint32_t scan_code = 0,
                                  bool extended = false);

 private:
  HHOOK keyboard_hook_ = nullptr;
};
}  // namespace crossdesk

#endif