/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-23
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _HEADLESS_RECOVERY_H_
#define _HEADLESS_RECOVERY_H_

#include <cstdint>

namespace crossdesk {

// Management-thread policy. Debounce mode changes and bound retries after a
// failed install. Capture progress is intentionally separate from DXGI retries.
class HeadlessRecovery {
 public:
  void Reset() { *this = {}; }

  bool ShouldProvision(uint64_t now, bool headless, bool busy, bool active) {
    if (!headless) {
      suspected_ = false;
      return false;
    }
    if (!suspected_) {
      suspected_ = true;
      since_ = now;
    }
    return !busy && !active && now - since_ >= 1000 &&
           (!attempted_ || now - last_attempt_ >= 15000);
  }

  void Attempted(uint64_t now) {
    attempted_ = true;
    last_attempt_ = now;
  }

 private:
  bool suspected_ = false;
  bool attempted_ = false;
  uint64_t since_ = 0;
  uint64_t last_attempt_ = 0;
};

}  // namespace crossdesk

#endif