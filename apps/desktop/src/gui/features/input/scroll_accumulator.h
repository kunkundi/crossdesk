/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-20
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCROLL_ACCUMULATOR_H_
#define _SCROLL_ACCUMULATOR_H_

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

namespace crossdesk {

class ScrollAccumulator {
 public:
  using Clock = std::chrono::steady_clock;

  int Consume(bool vertical, float delta, Clock::time_point now = Clock::now()) {
    if (!std::isfinite(delta) || delta == 0) return 0;

    auto& axis = vertical ? vertical_ : horizontal_;
    // Give a short gesture one notch even if it ends after its first event.
    if (!axis.last_event ||
        now - *axis.last_event >= std::chrono::milliseconds(200)) {
      axis.remainder = 0;
      axis.last_event = now;
      return delta > 0 ? 1 : -1;
    }
    axis.last_event = now;
    if (axis.remainder * delta < 0) axis.remainder = 0;

    // Slint's winit backend maps one line to 60 logical pixels. Cap large
    // deltas to preserve the single notch sent for a conventional wheel event.
    constexpr float kPixelsPerNotch = 60;
    axis.remainder += std::clamp(delta, -kPixelsPerNotch, kPixelsPerNotch);
    const int steps = static_cast<int>(axis.remainder / kPixelsPerNotch);
    axis.remainder -= steps * kPixelsPerNotch;
    return steps;
  }

 private:
  struct Axis {
    double remainder = 0;
    std::optional<Clock::time_point> last_event;
  };
  Axis horizontal_;
  Axis vertical_;
};

}  // namespace crossdesk

#endif
