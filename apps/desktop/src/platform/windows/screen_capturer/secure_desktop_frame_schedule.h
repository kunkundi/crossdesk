/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-07
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SECURE_DESKTOP_FRAME_SCHEDULE_H_
#define _SECURE_DESKTOP_FRAME_SCHEDULE_H_

#include <cstdint>
#include <string>

namespace crossdesk {

inline bool IsPendingSecureDesktopFrame(const std::string& error) {
  return error == "frame_wait_timeout" ||
         error == "shared_frame_write_in_progress" ||
         error == "shared_frame_changed_during_read";
}

// A 4K GDI frame can take longer than a single consumer wait. Do not issue
// another expensive screenshot while the shared producer is still healthy.
class SecureDesktopFrameSchedule {
 public:
  void Reset(uint64_t now) {
    last_shared_frame_ = now;
    next_start_attempt_ = 0;
  }

  void OnSharedFrame(uint64_t now) { last_shared_frame_ = now; }
  bool SharedCaptureStalled(uint64_t now) const {
    return now - last_shared_frame_ >= 1000;
  }
  bool CanStart(uint64_t now) const { return now >= next_start_attempt_; }
  void OnStartFailure(uint64_t now) { next_start_attempt_ = now + 1000; }

  static int RemainingFrameDelay(int interval_ms, uint64_t elapsed_ms) {
    return elapsed_ms >= static_cast<uint64_t>(interval_ms)
               ? 0
               : interval_ms - static_cast<int>(elapsed_ms);
  }

 private:
  uint64_t last_shared_frame_ = 0;
  uint64_t next_start_attempt_ = 0;
};

}  // namespace crossdesk

#endif