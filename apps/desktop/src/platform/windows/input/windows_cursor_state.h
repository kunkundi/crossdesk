/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-16
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WINDOWS_CURSOR_STATE_H_
#define _WINDOWS_CURSOR_STATE_H_

#include <remote_action.h>
#include <windows.h>

#include <chrono>
#include <vector>

#include "windows_cursor_shape.h"

namespace crossdesk {

// Used by both ordinary and secure-desktop capture. Device absence is only a
// compatibility hint: a hidden cursor with a null handle, or touch/pen
// suppression, must retain the application's normal hiding behavior.
class WindowsCursorState {
 public:
  void Sample(const CURSORINFO& info, bool embedded, CursorState* state) {
    const bool showing = (info.flags & CURSOR_SHOWING) != 0;
    state->render_mode = embedded ? CursorRenderMode::embedded
                         : showing ? CursorRenderMode::separate
                                   : CursorRenderMode::hidden;
    state->visible = state->render_mode == CursorRenderMode::separate;
    state->shape = state->visible ? ShapeFromWindowsCursor(info.hCursor)
                                  : RemoteCursorShape::none;
    state->hidden_reason = CursorHiddenReason::unspecified;
    if (!showing && !embedded) {
      state->hidden_reason = (info.flags & CURSOR_SUPPRESSED)
                                 ? CursorHiddenReason::system_suppressed
                             : info.hCursor && NoPointingDevice()
                                 ? CursorHiddenReason::no_pointing_device
                                 : CursorHiddenReason::system_hidden;
    }
  }

 private:
  // Query only for potentially recoverable hidden cursors, at most once per
  // second. Require two successful absent observations. Query failures, RDP
  // (whose devices may be absent from Raw Input), and present mice all disallow
  // fallback and break the sequence of absent observations.
  bool NoPointingDevice() {
    const auto now = std::chrono::steady_clock::now();
    if (now < next_device_check_) return absence_confirmed_;
    next_device_check_ = now + std::chrono::seconds(1);
    const bool absent = DetectDeviceAbsence();
    absence_confirmed_ = absent && absent_before_;
    absent_before_ = absent;
    return absence_confirmed_;
  }

  static bool DetectDeviceAbsence() {
    if (GetSystemMetrics(SM_MOUSEPRESENT) ||
        GetSystemMetrics(SM_REMOTESESSION)) {
      return false;
    }
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) ==
            static_cast<UINT>(-1) ||
        count > 4096)
      return false;
    if (count == 0) return true;
    std::vector<RAWINPUTDEVICELIST> devices(count);
    const UINT read = GetRawInputDeviceList(devices.data(), &count,
                                            sizeof(RAWINPUTDEVICELIST));
    if (read == static_cast<UINT>(-1) || read > devices.size()) return false;
    for (UINT i = 0; i < read; ++i) {
      if (devices[i].dwType == RIM_TYPEMOUSE) return false;
    }
    return true;
  }

  bool absent_before_ = false;
  bool absence_confirmed_ = false;
  std::chrono::steady_clock::time_point next_device_check_{};
};

}  // namespace crossdesk

#endif
