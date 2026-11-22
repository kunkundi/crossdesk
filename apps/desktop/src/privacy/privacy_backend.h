/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-10
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PRIVACY_BACKEND_H_
#define _PRIVACY_BACKEND_H_

#include <memory>
#include <string>

namespace crossdesk {

struct PrivacyScreenText {
  std::string unlock_hint = "To turn off the privacy screen, press";
  std::string locked_hint = "Privacy screen is on. Local unlocking is disabled.";
  bool allow_local_unlock = true;
  bool black_screen = false;
};

struct PrivacyCapabilities {
  bool overlay = false;
  bool block_input = false;
  std::string reason;
};

struct PrivacyHealth {
  std::string failure;
  bool emergency_exit = false;
  bool ready = true;
};

// Lifecycle, message pumping and native resource operations run on the
// controller's message thread. Const metadata queries are serialized with
// those operations by the controller mutex and must not call native UI APIs.
// AppKit work is queued to the main thread without waiting under this mutex.
class PrivacyBackend {
 public:
  virtual ~PrivacyBackend() = default;
  virtual PrivacyCapabilities Query() = 0;
  virtual bool Enable(bool block_input, const PrivacyScreenText& text,
                      std::string& error) = 0;
  // Update a live cover and its local shortcut policy without exposing the desktop.
  virtual void UpdateText(const PrivacyScreenText& text) {}
  virtual void Recover() = 0;
  virtual bool IsRecovered() const = 0;
  virtual bool RecoveryPending() const { return false; }
  virtual PrivacyHealth Poll() = 0;
  // Wait on both native window messages and controller commands.
  // Wake is the only backend operation callable from another thread.
  virtual void WaitForEvents(unsigned timeout_ms) = 0;
  virtual void Wake() = 0;
};

#ifdef _WIN32
std::unique_ptr<PrivacyBackend> CreateWindowsPrivacyBackend();
bool IsWindowsPrivacyDesktopAvailable();
#endif
#ifdef __APPLE__
std::unique_ptr<PrivacyBackend> CreateMacPrivacyBackend();
// Flush queued AppKit cleanup after joining the controller at app shutdown.
void FlushMacPrivacyTasks();
#endif
#if defined(__linux__) && !defined(__OHOS__)
std::unique_ptr<PrivacyBackend> CreateLinuxPrivacyBackend();
#endif

}  // namespace crossdesk

#endif  // _PRIVACY_BACKEND_H_
