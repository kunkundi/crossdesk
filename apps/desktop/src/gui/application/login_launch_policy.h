/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-26
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _LOGIN_LAUNCH_POLICY_H_
#define _LOGIN_LAUNCH_POLICY_H_

#include <chrono>

namespace crossdesk {

// Coordinates silent login launches (--autostart) when the desktop shell's
// notification area (for example Explorer's Shell_TrayWnd on Windows or a
// desktop panel on Linux) has not finished initializing yet. Retries tray
// registration for a bounded grace window instead of immediately showing the
// main window on the desktop, while still falling back to showing the window
// if no system tray becomes available.
class LoginLaunchTrayPolicy {
 public:
  using Clock = std::chrono::steady_clock;
  using Duration = Clock::duration;
  using TimePoint = Clock::time_point;

  static constexpr auto kDefaultGracePeriod = std::chrono::seconds(30);
  static constexpr auto kDefaultRetryInterval = std::chrono::seconds(1);

  enum class Action {
    kStayHidden,
    kRetryMinimizeToTray,
    kShowMainWindow,
  };

  explicit LoginLaunchTrayPolicy(
      Duration grace_period = kDefaultGracePeriod,
      Duration retry_interval = kDefaultRetryInterval)
      : grace_period_(grace_period), retry_interval_(retry_interval) {}

  Action Begin(bool login_launch, bool tray_supported, bool minimized_to_tray,
               TimePoint now) {
    pending_ = false;
    if (!login_launch || !tray_supported) {
      return Action::kShowMainWindow;
    }
    if (minimized_to_tray) {
      return Action::kStayHidden;
    }
    if (grace_period_ <= Duration::zero()) {
      return Action::kShowMainWindow;
    }
    pending_ = true;
    deadline_ = now + grace_period_;
    next_retry_ = now + retry_interval_;
    return Action::kStayHidden;
  }

  // Cancels pending silent-tray retries once the user or single-instance IPC
  // explicitly opens the main window, so a later retry never re-hides it.
  void Cancel() { pending_ = false; }

  bool pending() const { return pending_; }

  Action Poll(TimePoint now, bool tray_ready_now = false) const {
    if (!pending_) {
      return Action::kStayHidden;
    }
    if (tray_ready_now || now >= next_retry_ || now >= deadline_) {
      return Action::kRetryMinimizeToTray;
    }
    return Action::kStayHidden;
  }

  Action CompleteRetry(bool minimized_to_tray, TimePoint now) {
    if (!pending_) {
      return Action::kStayHidden;
    }
    if (minimized_to_tray) {
      pending_ = false;
      return Action::kStayHidden;
    }
    if (now >= deadline_) {
      pending_ = false;
      return Action::kShowMainWindow;
    }
    next_retry_ = now + retry_interval_;
    return Action::kStayHidden;
  }

 private:
  Duration grace_period_;
  Duration retry_interval_;
  bool pending_ = false;
  TimePoint deadline_{};
  TimePoint next_retry_{};
};

}  // namespace crossdesk

#endif
