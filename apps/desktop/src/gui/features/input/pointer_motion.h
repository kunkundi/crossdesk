/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-21
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _POINTER_MOTION_H_
#define _POINTER_MOTION_H_

#include <algorithm>

#include "rendering/video_geometry.h"

namespace crossdesk {

class PointerMotion {
 public:
  // Positions and viewport dimensions are in logical pixels.
  VideoPoint Update(VideoPoint local, VideoPoint viewport, int percent) {
    if (percent == 100 || !previous_ || viewport.x != size_.x || viewport.y != size_.y) {
      x_ = local.x;
      y_ = local.y;
    } else {
      const double gain = percent / 100.0;
      x_ = std::clamp<double>(x_ + (local.x - previous_->x) * gain, 0, viewport.x);
      y_ = std::clamp<double>(y_ + (local.y - previous_->y) * gain, 0, viewport.y);
    }
    previous_ = local;
    size_ = viewport;
    return {static_cast<float>(x_), static_cast<float>(y_)};
  }

 private:
  std::optional<VideoPoint> previous_;
  VideoPoint size_{};
  double x_ = 0, y_ = 0;
};

}  // namespace crossdesk

#endif
