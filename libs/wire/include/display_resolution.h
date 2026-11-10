/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DISPLAY_RESOLUTION_H_
#define _DISPLAY_RESOLUTION_H_

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace crossdesk {

inline constexpr int kMaxResolutionDisplays = 32;
inline constexpr int kMaxDisplayResolutionModes = 256;

struct DisplayResolution {
  int width = 0;
  int height = 0;
  bool operator==(const DisplayResolution &other) const {
    return width == other.width && height == other.height;
  }
};

inline bool ValidDisplayResolution(const DisplayResolution &mode) {
  return mode.width > 0 && mode.height > 0 && mode.width <= 32768 &&
         mode.height <= 32768;
}

struct DisplayResolutionInfo {
  int display_id = 0;
  // Opaque native identity: reject requests for a replaced/hot-unplugged
  // screen.
  std::string display_key;
  DisplayResolution current;
  std::vector<DisplayResolution> modes;
  bool operator==(const DisplayResolutionInfo &other) const {
    return display_id == other.display_id && display_key == other.display_key &&
           current == other.current && modes == other.modes;
  }
};

struct DisplayResolutionChange {
  int display_id = 0;
  std::string display_key;
  DisplayResolution resolution;
  uint32_t request_id = 0;
  bool accepted = false;
};

inline bool SupportsDisplayResolution(const DisplayResolutionInfo &display,
                                      const DisplayResolutionChange &change) {
  return display.display_id == change.display_id &&
         display.display_key == change.display_key &&
         std::find(display.modes.begin(), display.modes.end(),
                   change.resolution) != display.modes.end();
}

} // namespace crossdesk

#endif