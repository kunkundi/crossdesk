/* Copyright (c) 2026 by DI JUNKUN, All Rights Reserved. */
#ifndef _PRIVACY_CONTROLLER_H_
#define _PRIVACY_CONTROLLER_H_

#include <remote_action.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include "privacy_backend.h"

namespace crossdesk {

// Privacy is optional: it must never gate desktop video, audio or remote input.
// A failed backend is recovered automatically while the session keeps running.
class PrivacyController {
 public:
  using Factory = std::function<std::unique_ptr<PrivacyBackend>()>;
  explicit PrivacyController(Factory factory = {});
  ~PrivacyController();
  PrivacyController(const PrivacyController&) = delete;
  PrivacyController& operator=(const PrivacyController&) = delete;
  PrivacyStatus Snapshot() const;
  void SetText(const PrivacyScreenText& text);
  void Enable(bool block_input);
  void EnableOnConnection();
  // Preserve automatic session intent while Windows shows another desktop.
  void SuspendForDesktop();
  void ResumeAfterDesktopRecovery();
  void CancelAutomaticResume();
  void Disable();
  void Disconnected();
  void Shutdown();
  void Fail(const std::string& reason);
  bool Engaged() const { return engaged_.load(); }
  void CaptureChanged(bool running);

 private:
  void Run(Factory factory);
  void WakeLocked();
  void SetState(PrivacyState state, const std::string& reason);
  void FailLocked(const std::string& reason);
  void EnableOnConnectionLocked();
  void DisableLocked();
  static uint64_t Now();
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::thread thread_;
  std::unique_ptr<PrivacyBackend> backend_;
  PrivacyStatus status_{};
  PrivacyScreenText text_;
  std::atomic<bool> engaged_{false};
  bool quit_ = false;
  bool enable_pending_ = false;
  bool disable_pending_ = false;
  bool block_requested_ = false;
  bool automatic_enable_ = false;
  bool resume_after_unlock_ = false;
  bool capture_running_ = false;
  std::string failure_;
  uint64_t started_ = 0;
  uint64_t last_capability_check_ = 0;
};
}  // namespace crossdesk
#endif
