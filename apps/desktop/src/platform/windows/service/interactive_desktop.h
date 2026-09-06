/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-07
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _INTERACTIVE_DESKTOP_H_
#define _INTERACTIVE_DESKTOP_H_

#include <Windows.h>

#include <string>

namespace crossdesk {

inline std::wstring GetDesktopNameW(HDESK desktop) {
  DWORD bytes_needed = 0;
  if (desktop == nullptr) {
    return {};
  }
  GetUserObjectInformationW(desktop, UOI_NAME, nullptr, 0, &bytes_needed);
  if (bytes_needed == 0) {
    return {};
  }
  std::wstring name(bytes_needed / sizeof(wchar_t), L'\0');
  if (!GetUserObjectInformationW(desktop, UOI_NAME, name.data(), bytes_needed,
                                 &bytes_needed)) {
    return {};
  }
  while (!name.empty() && name.back() == L'\0') {
    name.pop_back();
  }
  return name;
}

// Use only on worker threads without windows or hooks. Restore the original
// desktop before closing the handle: Windows cannot close a desktop in use.
class ScopedInteractiveDesktop {
 public:
  ScopedInteractiveDesktop()
      : original_(GetThreadDesktop(GetCurrentThreadId())) {}
  ScopedInteractiveDesktop(const ScopedInteractiveDesktop&) = delete;
  ScopedInteractiveDesktop& operator=(const ScopedInteractiveDesktop&) = delete;

  ~ScopedInteractiveDesktop() {
    if (desktop_ != nullptr) {
      SetThreadDesktop(original_);
      CloseDesktop(desktop_);
    }
  }

  bool Bind(const std::wstring& fallback_name) {
    constexpr ACCESS_MASK access =
        DESKTOP_CREATEWINDOW | DESKTOP_WRITEOBJECTS | DESKTOP_READOBJECTS |
        DESKTOP_SWITCHDESKTOP | DESKTOP_JOURNALPLAYBACK;
    // SendInput requires playback access on the attached desktop; otherwise
    // it returns ERROR_ACCESS_DENIED even for a correctly attached SYSTEM helper.
    // The service's stage/name can lag a lock, unlock, or UAC transition.
    // Opening a cached desktop can succeed even when it is no longer visible.
    HDESK desktop = OpenInputDesktop(0, FALSE, access);
    if (desktop == nullptr && !fallback_name.empty()) {
      desktop = OpenDesktopW(fallback_name.c_str(), 0, FALSE, access);
    }
    if (desktop == nullptr) {
      return false;
    }
    if (!SetThreadDesktop(desktop)) {
      const DWORD error = GetLastError();
      CloseDesktop(desktop);
      SetLastError(error);
      return false;
    }
    if (desktop_ != nullptr) {
      CloseDesktop(desktop_);
    }
    desktop_ = desktop;
    name_ = GetDesktopNameW(desktop_);
    return true;
  }

  bool IsInputDesktop() const {
    HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (input == nullptr) {
      return false;
    }
    const std::wstring input_name = GetDesktopNameW(input);
    CloseDesktop(input);
    return !name_.empty() && _wcsicmp(name_.c_str(), input_name.c_str()) == 0;
  }

  const std::wstring& name() const { return name_; }

 private:
  HDESK original_ = nullptr;
  HDESK desktop_ = nullptr;
  std::wstring name_;
};

}  // namespace crossdesk

#endif