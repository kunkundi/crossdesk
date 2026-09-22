/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-22
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VIDEO_GEOMETRY_H_
#define _VIDEO_GEOMETRY_H_

#include <algorithm>
#include <cmath>
#include <optional>

namespace crossdesk {

struct VideoPoint {
  float x;
  float y;
};

// Area dimensions and pointer positions must use the same coordinate space.
// Slint supplies both in logical pixels; remote input is resolution
// independent.
inline std::optional<VideoPoint> MapVideoPoint(float x, float y,
                                               float area_width,
                                               float area_height,
                                               int video_width,
                                               int video_height) {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(area_width) ||
      !std::isfinite(area_height) || area_width <= 0 || area_height <= 0 ||
      video_width <= 0 || video_height <= 0) {
    return std::nullopt;
  }

  const double scale =
      std::min(static_cast<double>(area_width) / video_width,
               static_cast<double>(area_height) / video_height);
  const double width = video_width * scale;
  const double height = video_height * scale;
  const double left = (area_width - width) * 0.5;
  const double top = (area_height - height) * 0.5;
  if (x < left || x > left + width || y < top || y > top + height) {
    return std::nullopt;
  }
  return VideoPoint{static_cast<float>((x - left) / width),
                    static_cast<float>((y - top) / height)};
}

}  // namespace crossdesk

#endif