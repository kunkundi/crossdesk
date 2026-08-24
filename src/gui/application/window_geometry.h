#pragma once

#include <algorithm>

namespace crossdesk::window_geometry {

struct PhysicalRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct PhysicalSize {
  int width = 0;
  int height = 0;
};

struct PhysicalPosition {
  int x = 0;
  int y = 0;
};

constexpr PhysicalPosition CenteredPosition(const PhysicalRect& bounds,
                                            const PhysicalSize& size) {
  return {
      bounds.x + (bounds.width - std::min(bounds.width, size.width)) / 2,
      bounds.y + (bounds.height - std::min(bounds.height, size.height)) / 2,
  };
}

constexpr PhysicalPosition BottomRightPosition(const PhysicalRect& bounds,
                                               const PhysicalSize& size) {
  return {
      bounds.x + std::max(0, bounds.width - size.width),
      bounds.y + std::max(0, bounds.height - size.height),
  };
}

}  // namespace crossdesk::window_geometry
