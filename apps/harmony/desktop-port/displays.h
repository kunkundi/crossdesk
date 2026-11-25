/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-15
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DISPLAYS_H_
#define _DISPLAYS_H_

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace crossdesk::harmony::desktop {
struct DesktopDisplay {
  int id;
  std::string name;
  bool primary;
  int x, y, width, height;
  int usable_x, usable_y, usable_width, usable_height;
  float density = 1.0f;
  // SDL reserves zero for failure; Harmony's primary display can be zero.
  uint32_t SdlId() const { return uint32_t(id) + 1; }
};

inline std::vector<DesktopDisplay> ReadDisplays(const nlohmann::json& environment) {
  std::vector<DesktopDisplay> result;
  for (const auto& item : environment.value("displays", nlohmann::json::array())) {
    const int64_t id = item.value("id", int64_t(-1));
    const int width = item.value("width", 0), height = item.value("height", 0);
    if (id < 0 || id > INT32_MAX || width <= 0 || height <= 0) continue;
    if (std::any_of(result.begin(), result.end(), [id](const auto& d) { return d.id == id; })) continue;
    const int x = item.value("x", 0), y = item.value("y", 0);
    result.push_back({int(id), item.value("name", "Harmony Display"), item.value("primary", false),
        x, y, width, height, item.value("usableX", x), item.value("usableY", y),
        item.value("usableWidth", width), item.value("usableHeight", height),
        item.value("density", 1.0f)});
  }
  std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
    return a.primary != b.primary ? a.primary : a.id < b.id;
  });
  return result;
}

inline const DesktopDisplay* DisplayAt(const std::vector<DesktopDisplay>& displays, int x, int y) {
  const DesktopDisplay* nearest = nullptr;
  int64_t distance = std::numeric_limits<int64_t>::max();
  for (const auto& d : displays) {
    const int64_t dx = int64_t(x) - std::clamp<int64_t>(x, d.x, int64_t(d.x) + d.width - 1);
    const int64_t dy = int64_t(y) - std::clamp<int64_t>(y, d.y, int64_t(d.y) + d.height - 1);
    const int64_t next = dx * dx + dy * dy;
    if (next < distance) { distance = next; nearest = &d; }
  }
  return nearest;
}
}

#endif
