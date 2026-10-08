/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WINDOWS_RAW_KEYBOARD_GUARD_H_
#define _WINDOWS_RAW_KEYBOARD_GUARD_H_

#include <windows.h>

#include <optional>
#include <vector>

namespace crossdesk {

// Winit registers raw keyboard input for the process. Windows bypasses this
// process's WH_KEYBOARD_LL hook while that registration is active. Temporarily
// suspend only the keyboard registration while forwarding remote input, then
// restore the backend's latest registration on exit. Raw mouse input is kept.
class WindowsRawKeyboardGuard {
 public:
  bool Suspend(bool& changed) {
    changed = false;
    std::optional<RAWINPUTDEVICE> current;
    if (!FindKeyboard(current)) return false;
    if (!current) return true;
    const RAWINPUTDEVICE removal{1, 6, RIDEV_REMOVE, nullptr};
    if (!RegisterRawInputDevices(&removal, 1, sizeof(removal))) return false;
    saved_ = current;
    // GetRegisteredRawInputDevices can omit DEVNOTIFY from its result.
    // Winit requests it for keyboard hotplug notifications, so retain it.
    saved_->dwFlags |= RIDEV_DEVNOTIFY;
    changed = true;
    return true;
  }

  bool Restore() {
    if (!saved_) return true;
    std::optional<RAWINPUTDEVICE> current;
    if (!FindKeyboard(current)) return false;
    // A backend may have changed its registration or destroyed its target
    // since capture stopped. Never overwrite a newer registration.
    if (current || (saved_->hwndTarget && !IsWindow(saved_->hwndTarget))) {
      saved_.reset();
      return true;
    }
    if (!RegisterRawInputDevices(&*saved_, 1, sizeof(RAWINPUTDEVICE))) return false;
    saved_.reset();
    return true;
  }

 private:
  static bool FindKeyboard(std::optional<RAWINPUTDEVICE>& result) {
    result.reset();
    for (int attempt = 0; attempt < 3; ++attempt) {
      UINT count = 0;
      const UINT status = GetRegisteredRawInputDevices(
          nullptr, &count, sizeof(RAWINPUTDEVICE));
      if (status == static_cast<UINT>(-1) &&
          GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
      if (count == 0) return true;
      std::vector<RAWINPUTDEVICE> devices(count);
      const UINT read = GetRegisteredRawInputDevices(
          devices.data(), &count, sizeof(RAWINPUTDEVICE));
      if (read == static_cast<UINT>(-1)) {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) continue;
        return false;
      }
      for (UINT i = 0; i < read; ++i) {
        if (devices[i].usUsagePage == 1 && devices[i].usUsage == 6) {
          result = devices[i];
          break;
        }
      }
      return true;
    }
    SetLastError(ERROR_RETRY);
    return false;
  }

  std::optional<RAWINPUTDEVICE> saved_;
};

}  // namespace crossdesk

#endif