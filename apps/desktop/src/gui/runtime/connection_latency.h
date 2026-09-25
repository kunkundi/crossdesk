/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-26
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CONNECTION_LATENCY_H_
#define _CONNECTION_LATENCY_H_

#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>

namespace crossdesk::gui_detail {

// Presentation-only smoothing. The transport's raw RTT remains available for
// recovery, congestion control and diagnostics. A missing one-second report
// does not flicker, but a disconnected/stalled peer cannot leave a stale value.
class ConnectionLatencyStats {
 public:
  using Clock = std::chrono::steady_clock;

  void Update(double rtt_ms, Clock::time_point now = Clock::now()) {
    if (!std::isfinite(rtt_ms) || rtt_ms < 0 || rtt_ms > 2000) return;
    std::lock_guard lock(mutex_);
    if (average_ms_ && now < updated_) return;
    if (!Fresh(now)) {
      average_ms_ = rtt_ms;
    } else {
      *average_ms_ += 0.25 * (rtt_ms - *average_ms_);
    }
    updated_ = now;
  }

  std::optional<double> Get(Clock::time_point now = Clock::now()) const {
    std::lock_guard lock(mutex_);
    return Fresh(now) ? average_ms_ : std::nullopt;
  }

  void Reset() {
    std::lock_guard lock(mutex_);
    average_ms_.reset();
  }

 private:
  bool Fresh(Clock::time_point now) const {
    // The UI may snapshot `now` just before the report thread updates us.
    // A newer valid report must not cause a one-frame unavailable flicker.
    return average_ms_ && now - updated_ < std::chrono::seconds(3);
  }
  mutable std::mutex mutex_;
  std::optional<double> average_ms_;
  Clock::time_point updated_{};
};

}  // namespace crossdesk::gui_detail

#endif