/*
 * @Author: DI JUNKUN
 * @Date: 2024-11-22
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _KEYBOARD_CAPTURER_H_
#define _KEYBOARD_CAPTURER_H_

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <atomic>
#include <thread>

#include "device_controller.h"

namespace crossdesk {

class KeyboardCapturer : public DeviceController {
 public:
  KeyboardCapturer();
  virtual ~KeyboardCapturer();

 public:
  virtual int Hook(OnKeyAction on_key_action, void* user_ptr);
  virtual int Unhook();
  virtual int SendKeyboardCommand(int key_code, bool is_down);

 private:
  Display* display_;
  Window root_;
  std::atomic<bool> running_;
  std::thread event_thread_;
};
}  // namespace crossdesk
#endif