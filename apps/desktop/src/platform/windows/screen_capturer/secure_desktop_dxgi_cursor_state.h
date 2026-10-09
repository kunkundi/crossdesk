/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SECURE_DESKTOP_DXGI_CURSOR_STATE_H_
#define _SECURE_DESKTOP_DXGI_CURSOR_STATE_H_

#include <cstdint>

namespace crossdesk {

// Owned by one secure-desktop duplication instance. Desktop-only frames and
// timeouts retain the pointer mode; PointerPosition is undefined without a
// mouse update.
class SecureDesktopDxgiCursorState {
 public:
  void Update(int64_t last_mouse_update_time, bool separate_pointer_visible) {
    if (last_mouse_update_time != 0) {
      embedded_ = !separate_pointer_visible;
    }
  }

  bool IsEmbedded(bool system_cursor_visible, bool cursor_on_output) const {
    return embedded_ && system_cursor_visible && cursor_on_output;
  }

 private:
  // Secure desktops can composite the cursor without sending pointer-plane
  // updates, even on physical displays. Until DXGI explicitly reports a
  // separate pointer, do not add another cursor to the frame or controller.
  bool embedded_ = true;
};

}  // namespace crossdesk

#endif  // _SECURE_DESKTOP_DXGI_CURSOR_STATE_H_
