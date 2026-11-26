/*
 * @Author: DI JUNKUN
 * @Date: 2024-11-22
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
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
  bool SupportsTextInput() const override { return true; }
  int SendTextCommand(const std::string& text) override;
  int SendKeyboardCommand(int key_code, bool is_down, uint32_t scan_code = 0,
                          bool extended = false) override;

 private:
  static LRESULT CALLBACK KeyboardHookProc(int code, WPARAM message,
                                           LPARAM data);
  static void CALLBACK ForegroundEventProc(HWINEVENTHOOK hook, DWORD event,
                                           HWND hwnd, LONG id_object,
                                           LONG id_child, DWORD event_thread,
                                           DWORD event_time);
  static thread_local PlatformKeyboardCapturer* active_capturer_;

  void CaptureThreadMain();
  bool RenewKeyboardHook();
  void RemoveKeyboardHook();
  bool IsWindowsKeyDown(int key_code) const;
  void RefreshLocalKeys();
  void SnapshotLocalKeys();
  void ForwardLocalModifiers(int skip_key = 0);
  void ReleaseCapturedKeys();
  void ForwardKey(int code, bool down, uint32_t scan_code, bool extended);
  void HandleForegroundChanged(HWND foreground_window);
  bool HandleKeyboardInput(WPARAM message, const KBDLLHOOKSTRUCT& keyboard,
                           HWND foreground_window);

  OnKeyAction on_key_action_ = nullptr;
  void* user_ptr_ = nullptr;
  HWND capture_window_ = nullptr;
  std::atomic<bool> hook_active_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<ULONGLONG> last_hook_pump_tick_{0};
  HHOOK keyboard_hook_ = nullptr;
  HWINEVENTHOOK foreground_hook_ = nullptr;
  WindowsRawKeyboardGuard raw_keyboard_guard_;
  HHOOK retired_hook_ = nullptr;
  bool capture_window_focused_ = false;
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
