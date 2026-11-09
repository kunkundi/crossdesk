/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-10
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PRIVACY_CURSOR_GUARD_H_
#define _PRIVACY_CURSOR_GUARD_H_

#include <Windows.h>

#include <string>

namespace crossdesk {

// Owned by one privacy or capture thread. The child owns cursor suppression
// and restores it on Stop(), parent exit, or optional capture-owner exit.
class PrivacyCursorGuard {
 public:
  PrivacyCursorGuard() = default;
  PrivacyCursorGuard(const PrivacyCursorGuard&) = delete;
  PrivacyCursorGuard& operator=(const PrivacyCursorGuard&) = delete;
  ~PrivacyCursorGuard();
  // Capture on Winlogon can specify its bound desktop and an additional
  // process whose exit must restore the cursor, even if capture is blocked.
  bool Start(std::string& error, const std::wstring& desktop = {},
             HANDLE capture_owner = nullptr);
  bool Healthy(std::string& error);
  bool Stop(std::string& error);
  bool stopped() const { return !delegated_ && !process_ && !restore_pending_; }
  bool delegated() const { return delegated_; }

 private:
  bool RestoreAfterFailure(std::string& error);
  HANDLE process_ = nullptr;
  HANDLE stop_event_ = nullptr;
  bool restore_pending_ = false;
  bool helper_failure_reported_ = false;
  bool delegated_ = false;
  std::wstring desktop_;
};

}  // namespace crossdesk

#endif  // _PRIVACY_CURSOR_GUARD_H_
