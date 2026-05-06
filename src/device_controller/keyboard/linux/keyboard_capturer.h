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
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include "device_controller.h"

struct DBusConnection;
struct DBusMessageIter;

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
  Display* display_;
  Window root_;
  std::atomic<bool> running_;
  std::thread event_thread_;
  bool use_wayland_portal_ = false;
  bool wayland_init_attempted_ = false;
  DBusConnection* dbus_connection_ = nullptr;
  std::string wayland_session_handle_;
};
}  // namespace crossdesk
#endif
