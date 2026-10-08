/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-08
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SECURE_DESKTOP_FRAME_PACER_H_
#define _SECURE_DESKTOP_FRAME_PACER_H_

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace crossdesk {

// Sleep's system-tick rounding can turn a 33ms frame into a 47ms frame.
// Request precise waits only on this capture thread, without changing the
// machine's timer resolution. Older Windows versions use a regular timer.
class SecureDesktopFramePacer {
 public:
  using Clock = std::chrono::steady_clock;
  explicit SecureDesktopFramePacer(int fps)
      : interval_(std::chrono::nanoseconds(
            1000000000LL / (std::clamp)(fps > 0 ? fps : 30, 1, 1000))) {
    constexpr DWORD kHighResolution = 0x00000002;
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, kHighResolution,
                                   TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
  }
  ~SecureDesktopFramePacer() {
    if (timer_) CloseHandle(timer_);
  }
  SecureDesktopFramePacer(const SecureDesktopFramePacer&) = delete;
  SecureDesktopFramePacer& operator=(const SecureDesktopFramePacer&) = delete;

  void Wait(Clock::time_point frame_started) {
    const auto now = Clock::now();
    // Keep fractional frame periods and compensate for small scheduler delays.
    // After a slow frame, resume now instead of issuing a burst of old frames.
    if (deadline_ == Clock::time_point{}) deadline_ = frame_started;
    deadline_ += interval_;
    if (deadline_ <= now) {
      deadline_ = now;
      return;
    }
    LARGE_INTEGER due{};
    const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           deadline_ - now).count() / 100;
    due.QuadPart = -(ticks > 0 ? ticks : 1);
    if (timer_ && SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE) &&
        WaitForSingleObject(timer_, INFINITE) == WAIT_OBJECT_0) return;
    std::this_thread::sleep_until(deadline_);
  }

 private:
  HANDLE timer_ = nullptr;
  Clock::duration interval_;
  Clock::time_point deadline_{};
};

}  // namespace crossdesk

#endif  // _SECURE_DESKTOP_FRAME_PACER_H_
