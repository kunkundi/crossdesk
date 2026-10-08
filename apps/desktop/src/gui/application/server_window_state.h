/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SERVER_WINDOW_STATE_H_
#define _SERVER_WINDOW_STATE_H_

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace crossdesk::server_window_state {

// All values use logical desktop coordinates (including negative monitor
// origins). The bar's anchor is independent of the panel's changing height.
struct Rect {
  float x, y, width, height;
};

struct EdgeLayout {
  Rect bar;
  Rect panel;
  bool above;
};

inline EdgeLayout PlacePanel(const Rect &screen, float position,
                             float bar_width, float panel_height) {
  constexpr float bar_height = 36;
  constexpr float gap = 6;
  constexpr float margin = 8;
  const float travel = std::max(0.0f, screen.height - bar_height - 2 * margin);
  const float top =
      screen.y + margin + travel * std::clamp(position, 0.0f, 1.0f);
  const float above = std::max(0.0f, top - screen.y - margin - gap);
  const float below = std::max(0.0f, screen.y + screen.height - margin - top -
                                         bar_height - gap);
  const bool opens_above =
      below < panel_height && (above >= panel_height || above > below);
  const float height = std::min(panel_height, opens_above ? above : below);
  const float width = std::min(225.0f, screen.width);
  return {{screen.x + screen.width - bar_width, top, bar_width, bar_height},
          {screen.x + screen.width - width,
           opens_above ? top - gap - height : top + bar_height + gap, width,
           height},
          opens_above};
}

inline bool Contains(const Rect &rect, float x, float y) {
  return x >= rect.x && y >= rect.y && x < rect.x + rect.width &&
         y < rect.y + rect.height;
}

inline int ReconcileSelectedController(const std::vector<std::string> &ids,
                                       std::string *selected_id) {
  if (!selected_id) {
    return 0;
  }
  if (ids.empty()) {
    selected_id->clear();
    return 0;
  }

  const auto selected = std::find(ids.begin(), ids.end(), *selected_id);
  if (selected == ids.end()) {
    *selected_id = ids.front();
    return 0;
  }
  return static_cast<int>(std::distance(ids.begin(), selected));
}
} // namespace crossdesk::server_window_state

#endif
