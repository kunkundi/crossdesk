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

  int Consume(bool vertical, float delta, int percent, Clock::time_point now = Clock::now()) {
    if (!std::isfinite(delta) || delta == 0) return 0;
    // Slint's winit backend reports content displacement (+x = scroll left),
    // whereas wheel_horizontal uses positive steps for scrolling right.
    if (!vertical) delta = -delta;

    if (percent != sensitivity_percent_) {
      horizontal_ = {};
      vertical_ = {};
      sensitivity_percent_ = percent;
    }
    constexpr float kPixelsPerNotch = 60;
    auto& axis = vertical ? vertical_ : horizontal_;
    const bool new_gesture =
        !axis.last_event ||
        now - *axis.last_event >= std::chrono::milliseconds(200);
    axis.last_event = now;
    // Slint's winit backend represents a wheel notch as 60 logical pixels.
#if defined(_WIN32) || defined(__APPLE__)
    const bool whole_notch = std::abs(delta) >= kPixelsPerNotch;
    delta = std::clamp(delta, -kPixelsPerNotch, kPixelsPerNotch);
#if defined(__APPLE__)
    if (new_gesture) delta = std::copysign(kPixelsPerNotch, delta);
#endif
#else
    // These controllers historically send one notch per scroll event.
    const bool whole_notch = true;
    delta = std::copysign(kPixelsPerNotch, delta);
#endif
    // Retain fractional output between isolated physical clicks at low gain.
    // Pixel gestures still start fresh after a pause.
    const bool carry_notches = percent != 100 && whole_notch && axis.last_whole_notch;
    if (axis.remainder * delta < 0 || (new_gesture && !carry_notches))
      axis.remainder = 0;
    axis.last_whole_notch = whole_notch;
    constexpr double units_per_notch = kPixelsPerNotch * 100;
    axis.remainder += static_cast<double>(delta) * percent;
    const int steps = static_cast<int>(axis.remainder / units_per_notch);
    axis.remainder -= steps * units_per_notch;
    return steps;
  }

 private:
  struct Axis {
    double remainder = 0;
    std::optional<Clock::time_point> last_event;
    bool last_whole_notch = false;
  };
  int sensitivity_percent_ = 100;
  Axis horizontal_;
  Axis vertical_;
};

}  // namespace crossdesk

#endif
