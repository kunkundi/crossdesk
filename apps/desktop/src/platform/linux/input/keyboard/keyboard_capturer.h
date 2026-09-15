/*
 * @Author: DI JUNKUN
 * @Date: 2024-11-22
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _KEYBOARD_CAPTURER_H_
#define _KEYBOARD_CAPTURER_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "device_controller.h"

struct DBusConnection;
struct DBusMessageIter;
struct _XDisplay;

namespace crossdesk {

class PlatformKeyboardCapturer final : public KeyboardCapturer {
 public:
  PlatformKeyboardCapturer();
  virtual ~PlatformKeyboardCapturer();

 public:
  virtual int Hook(OnKeyAction on_key_action, void* user_ptr);
  virtual int Unhook();
  bool IsHookActive() const override { return running_.load(); }
  virtual int SendKeyboardCommand(int key_code, bool is_down,
                                  uint32_t scan_code = 0,
                                  bool extended = false);

 private:
  struct CapturedKey {
    int key_code = 0;
    uint32_t scan_code = 0;
    bool extended = false;
  };
  struct RawKeyPress {
    unsigned long time = 0;
    int source_id = 0;
  };
  bool RefreshXTestDevices();
  void CaptureKey(int source_id, int x11_keycode, bool is_down);
  void RunCapture();

  bool InitWaylandPortal();
  void CleanupWaylandPortal();
  int SendWaylandKeyboardCommand(int key_code, bool is_down, uint32_t scan_code,
                                 bool extended);
  bool NotifyWaylandKeyboardKeysym(int keysym, uint32_t state);
  bool NotifyWaylandKeyboardKeycode(int keycode, uint32_t state);
  bool SendWaylandPortalVoidCall(const char* method_name,
                                 const std::function<void(DBusMessageIter*)>&
                                     append_args);

 private:
  _XDisplay* display_;
  // Capture owns a separate connection, including its XKB event queue.
  _XDisplay* capture_display_ = nullptr;
  unsigned long capture_focus_ = 0;
  bool capture_has_focus_ = false;
  int xi_opcode_ = 0;
  int xkb_event_base_ = 0;
  OnKeyAction on_key_action_ = nullptr;
  void* user_ptr_ = nullptr;
  std::unordered_set<int> xtest_devices_;
  std::unordered_map<uint64_t, CapturedKey> captured_keys_;
  std::array<RawKeyPress, 256> raw_presses_{};
  std::atomic<bool> running_;
  std::thread event_thread_;
  std::mutex x11_injection_mutex_;
  bool x11_xtest_available_ = false;
  bool use_wayland_portal_ = false;
  bool wayland_init_attempted_ = false;
  DBusConnection* dbus_connection_ = nullptr;
  std::string wayland_session_handle_;
};
}  // namespace crossdesk
#endif
