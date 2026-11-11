/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-12
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _GALLERY_WINDOW_STATE_H_
#define _GALLERY_WINDOW_STATE_H_

#include <slint.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace crossdesk::gallery {

struct WindowState {
  bool wall_fullscreen = false;
  bool wall_maximized = false;
  // Store client dimensions in logical pixels, independent of the display DPI.
  std::optional<slint::LogicalSize> wall_size;
  std::optional<slint::PhysicalPosition> wall_position, connection_position;

  void Observe(bool wall, slint::PhysicalPosition position,
               slint::PhysicalSize size, float scale, bool fullscreen,
               bool maximized, float compact_height = 486,
               float compact_width = 640) {
    if (Restoring())
      return;
    if (!wall) {
      connection_position = position;
      return;
    }
    if (!fullscreen && !maximized &&
        IsUndersized(size, scale, compact_height, compact_width))
      return;
    scale = std::max(scale, 1.0f);
    wall_fullscreen = fullscreen;
    wall_maximized = !fullscreen && maximized;
    if (!fullscreen && !maximized) {
      wall_size = slint::LogicalSize(
          slint::Size<float>{size.width / scale, size.height / scale});
      wall_position = position;
    }
  }

  bool RecoverSizeMismatch(bool wall, slint::PhysicalPosition position,
                         slint::PhysicalSize size, float scale, bool visible,
                         bool minimized, bool fullscreen, bool maximized,
                         float compact_height = 486,
                         float compact_width = 640) {
    // Hidden/minimized/uninitialized windows can report temporary tiny bounds.
    // Leave those alone, and let an in-flight restoration retain its target.
    if (Restoring() || !visible || minimized || fullscreen || maximized ||
        size.width == 0 || size.height == 0)
      return false;
    // Display mode/DPI changes can enlarge the fixed connection window as
    // well as shrink it. Restore both directions at the current scale; the
    // resizable gallery only needs recovery below its minimum dimensions.
    const bool mismatch =
        wall ? IsUndersized(size, scale, compact_height, compact_width)
             : !SameSize(size, RestoreSize(false, scale, compact_height,
                                          compact_width));
    if (!mismatch)
      return false;
    if (wall) {
      wall_position = position;
      wall_fullscreen = false;
      wall_maximized = false;
    } else {
      connection_position = position;
    }
    // Keep the last valid logical size; only the current anchor is captured.
    BeginRestore();
    return true;
  }

  slint::PhysicalSize RestoreSize(bool wall, float scale,
                                  float compact_height = 486,
                                  float compact_width = 640) const {
    const float width =
        wall && wall_size ? std::max(compact_width, wall_size->width)
                          : compact_width;
    const float height = wall && wall_size
                             ? std::max(compact_height, wall_size->height)
                             : compact_height;
    scale = std::max(scale, 1.0f);
    return slint::PhysicalSize(
        slint::Size<uint32_t>{uint32_t(std::lround(width * scale)),
                              uint32_t(std::lround(height * scale))});
  }

  void BeginRestore() {
    phase_ = Phase::Normal;
    target_.reset();
    last_size_.reset();
    matches_ = 0;
  }

  void PrepareForShow(slint::Window &window, bool wall,
                      slint::PhysicalSize size,
                      std::optional<slint::PhysicalPosition> position,
                      float initial_y_offset = 0) {
    const float scale = std::max(window.scale_factor(), 1.0f);
    // The actual display scale is resolved when the native window is created.
    // Give the backend logical geometry so its first visible frame is correct.
    window.set_size(slint::LogicalSize(
        slint::Size<float>{size.width / scale, size.height / scale}));
    if (position) {
      (wall ? wall_position : connection_position) = position;
      window.set_position(slint::LogicalPosition(slint::Point<float>{
          position->x / scale, position->y / scale + initial_y_offset}));
    }
  }
  bool Restoring() const { return phase_ != Phase::Idle; }
  bool RestoringNormal() const { return phase_ == Phase::Normal; }

  // A set_size request can complete asynchronously, after layout constraints
  // and the backing scale have changed. Acknowledge observations on later
  // ticks.
  bool NormalSizeRestored(slint::PhysicalSize actual,
                          slint::PhysicalSize target) {
    if (!RestoringNormal())
      return false;
    if (target_ && SameSize(*target_, target) && SameSize(actual, target))
      ++matches_;
    else
      matches_ = 0;
    target_ = target;
    if (matches_ < 2)
      return false;
    phase_ = Phase::Presentation;
    matches_ = 0;
    last_size_.reset();
    return true;
  }

  bool FinishRestore(bool wall, slint::PhysicalSize actual, bool fullscreen,
                     bool maximized) {
    if (phase_ != Phase::Presentation)
      return false;
    const bool want_fullscreen = wall && wall_fullscreen;
    const bool want_maximized = wall && !want_fullscreen && wall_maximized;
    const bool normal = !want_fullscreen && !want_maximized;
    if (normal && !fullscreen && !maximized && target_ &&
        !SameSize(actual, *target_)) {
      // A late constraint/DPI update can invalidate an earlier acknowledgement.
      // Reissue the normal size instead of getting stuck or saving that size.
      BeginRestore();
      return false;
    }
    const bool ready = fullscreen == want_fullscreen &&
                       (want_fullscreen || maximized == want_maximized) &&
                       (!normal || (target_ && SameSize(actual, *target_)));
    if (ready && last_size_ && SameSize(actual, *last_size_))
      ++matches_;
    else
      matches_ = 0;
    last_size_ = actual;
    if (matches_ < 2)
      return false;
    phase_ = Phase::Idle;
    return true;
  }

private:
  enum class Phase { Idle, Normal, Presentation };
  Phase phase_ = Phase::Idle;
  std::optional<slint::PhysicalSize> target_, last_size_;
  int matches_ = 0;
  static bool IsUndersized(slint::PhysicalSize size, float scale,
                           float compact_height, float compact_width) {
    scale = std::max(scale, 1.0f);
    return double(size.width) + 1 < compact_width * scale ||
           double(size.height) + 1 < compact_height * scale;
  }
  static bool SameSize(slint::PhysicalSize a, slint::PhysicalSize b) {
    return std::abs(double(a.width) - b.width) <= 1 &&
           std::abs(double(a.height) - b.height) <= 1;
  }
};

} // namespace crossdesk::gallery

#endif
