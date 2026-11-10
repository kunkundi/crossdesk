/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DISPLAY_RESOLUTION_STATE_H_
#define _DISPLAY_RESOLUTION_STATE_H_

#include <display_resolution.h>

namespace crossdesk {

// Guarded by RemoteSession::display_resolution_mutex_. The current selection
// always comes from the host, never from an unacknowledged request.
struct DisplayResolutionState {
  std::vector<DisplayResolutionInfo> displays;
  DisplayResolutionChange requested;
  bool pending = false;
  bool failed = false;
  uint64_t request_tick = 0;

  bool Begin(const DisplayResolutionChange &selection, uint64_t now) {
    if (pending)
      return false;
    for (const auto &display : displays) {
      if (!SupportsDisplayResolution(display, selection))
        continue;
      if (display.current == selection.resolution)
        return false;
      uint32_t next = requested.request_id + 1;
      if (next == 0)
        next = 1;
      requested = {display.display_id, display.display_key,
                   selection.resolution, next, false};
      pending = true;
      failed = false;
      request_tick = now;
      return true;
    }
    return false;
  }

  void Complete(const DisplayResolutionChange &response,
                std::vector<DisplayResolutionInfo> actual) {
    if (response.request_id != requested.request_id ||
        response.display_id != requested.display_id ||
        response.display_key != requested.display_key ||
        !(response.resolution == requested.resolution))
      return;
    displays = std::move(actual);
    pending = false;
    failed = !response.accepted;
    if (response.accepted) {
      const auto found = std::find_if(
          displays.begin(), displays.end(), [&](const auto &display) {
            return display.display_id == response.display_id &&
                   display.display_key == response.display_key &&
                   display.current == response.resolution;
          });
      failed = found == displays.end();
    }
  }

  void Expire(uint64_t now) {
    if (pending && now - request_tick >= 10000) {
      pending = false;
      failed = true;
    }
  }
};

} // namespace crossdesk

#endif