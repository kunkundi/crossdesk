/*
 * @Author: DI JUNKUN
 * @Date: 2024-11-22
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _KEYBOARD_CAPTURER_H_
#define _KEYBOARD_CAPTURER_H_

#include <Windows.h>

#include <condition_variable>
#include <mutex>
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
  virtual int SendKeyboardCommand(int key_code, bool is_down,
                                  uint32_t scan_code = 0,
                                  bool extended = false);

 private:
  static LRESULT CALLBACK RawInputWindowProc(HWND window, UINT message,
                                             WPARAM w_param, LPARAM l_param);

  void RawInputThreadMain();
  bool CreateRawInputWindow();
  void DestroyRawInputWindow();
  void HandleRawInput(HRAWINPUT raw_input_handle);

  OnKeyAction on_key_action_ = nullptr;
  void* user_ptr_ = nullptr;
  HWND raw_input_window_ = nullptr;
  bool raw_input_registered_ = false;
  std::thread capture_thread_;
  DWORD capture_thread_id_ = 0;
  bool capture_start_complete_ = false;
  bool capture_start_succeeded_ = false;
  std::mutex capture_state_mutex_;
  std::condition_variable capture_start_condition_;
};
}  // namespace crossdesk

#endif
