/*
 * @Author: DI JUNKUN
 * @Date: 2024-11-22
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _KEYBOARD_CAPTURER_H_
#define _KEYBOARD_CAPTURER_H_

#include <Windows.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "device_controller.h"
#include "keyboard_state.h"
#include "windows_raw_keyboard_guard.h"

namespace crossdesk {

class PlatformKeyboardCapturer final : public KeyboardCapturer {
 public:
  PlatformKeyboardCapturer() = default;
  ~PlatformKeyboardCapturer() override;

  int Hook(OnKeyAction on_key_action, void* user_ptr) override;
  int Unhook() override;
  bool IsHookActive() const override;
  int SendKeyboardCommand(int key_code, bool is_down, uint32_t scan_code = 0,
                          bool extended = false) override;

 private:
  static LRESULT CALLBACK KeyboardHookProc(int code, WPARAM message,
                                           LPARAM data);
  static thread_local PlatformKeyboardCapturer* active_capturer_;

  void CaptureThreadMain();
  bool RenewKeyboardHook();
  void RemoveKeyboardHook();
  void SnapshotLocalKeys();
  void ForwardLocalModifiers();
  void ReleaseCapturedKeys();
  void ForwardKey(int code, bool down, uint32_t scan_code, bool extended);
  bool HandleKeyboardInput(WPARAM message, const KBDLLHOOKSTRUCT& keyboard,
                           HWND foreground_window);

  OnKeyAction on_key_action_ = nullptr;
  void* user_ptr_ = nullptr;
  HWND capture_window_ = nullptr;
  std::atomic<bool> hook_active_{false};
  std::atomic<ULONGLONG> last_hook_pump_tick_{0};
  HHOOK keyboard_hook_ = nullptr;
  WindowsRawKeyboardGuard raw_keyboard_guard_;
  HHOOK retired_hook_ = nullptr;
  std::array<bool, 256> local_keys_down_{};
  std::array<KeyboardKey, 256> captured_keys_{};
  std::thread capture_thread_;
  DWORD capture_thread_id_ = 0;
  bool capture_start_complete_ = false;
  bool capture_start_succeeded_ = false;
  std::mutex capture_state_mutex_;
  std::condition_variable capture_start_condition_;
};
}  // namespace crossdesk

#endif
