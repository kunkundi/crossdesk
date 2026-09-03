/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CURSOR_POSITION_H_
#define _CURSOR_POSITION_H_

#include <algorithm>
#include <vector>

#include <remote_action.h>

#include "display_info.h"

namespace crossdesk {

inline void ResetCursorPosition(CursorState* state) {
  if (!state) return;
  state->position_update = true;
  state->position_valid = false;
  state->x = 0.5f;
  state->y = 0.5f;
  state->visual_offset_x = 0.0f;
  state->visual_offset_y = 0.0f;
  state->display_id = -1;
}

inline bool NormalizeCursorPosition(
    double screen_x, double screen_y,
    const std::vector<DisplayInfo>& displays, int preferred_display,
    CursorState* state) {
  if (!state) return false;
  ResetCursorPosition(state);

  auto contains = [&](int index) {
    if (index < 0 || index >= static_cast<int>(displays.size())) return false;
    const auto& display = displays[index];
    return display.width > 0 && display.height > 0 &&
           screen_x >= display.left && screen_x < display.right &&
           screen_y >= display.top && screen_y < display.bottom;
  };

  int display_id = contains(preferred_display) ? preferred_display : -1;
  if (display_id < 0) {
    for (int index = 0; index < static_cast<int>(displays.size()); ++index) {
      if (contains(index)) {
        display_id = index;
        break;
      }
    }
  }
  if (display_id < 0) return false;

  const auto& display = displays[display_id];
  // Screen coordinates describe a continuous rectangle with an exclusive
  // right/bottom edge. Use its full extent so feedback is the exact inverse of
  // normalized input instead of accumulating a one-pixel edge convention.
  const double horizontal_extent = std::max(display.width, 1);
  const double vertical_extent = std::max(display.height, 1);
  state->position_valid = true;
  state->x = static_cast<float>(std::clamp(
      (screen_x - display.left) / horizontal_extent, 0.0, 1.0));
  state->y = static_cast<float>(std::clamp(
      (screen_y - display.top) / vertical_extent, 0.0, 1.0));
  state->display_id = display_id;
  return true;
}

}  // namespace crossdesk

#endif