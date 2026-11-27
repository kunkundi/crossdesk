/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-21
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WINDOW_SHIFT_STATE_H_
#define _WINDOW_SHIFT_STATE_H_

#include "keyboard_state.h"

namespace crossdesk {

// A window can gain focus after Shift was pressed. Slint then reports Shift
// on a character without having delivered the corresponding modifier key-down.
class WindowShiftState {
 public:
  static constexpr int kLeft = 0xA0;
  static constexpr int kRight = 0xA1;
  struct Held {
    bool left;
    bool right;
  };

  template <typename Send>
  void Forward(int key, bool down, bool shift, Held held, Send send) {
    if (key == kLeft || key == kRight) {
      if (inferred_left_ && key == kLeft) {
        inferred_left_ = false;
        if (down) return;  // The real press takes over the inferred press.
      }
      send(key, down);
      // If the real key is right Shift, establish its state before releasing
      // the provisional left Shift so an active chord stays shifted.
      ReleaseInferred(send);
      return;
    }

    const bool infer = down && !IsKeyboardModifier(key) &&
                       key != 0x14 && key != 0x90 && key != 0x91;  // Lock keys.
    Sync(shift, infer, held, send);
    send(key, down);
  }

  template <typename Send>
  void Sync(bool shift, bool infer, Held held, Send send) {
    if (!shift) {
      if (held.left) send(kLeft, false);
      if (held.right) send(kRight, false);
      inferred_left_ = false;
    } else if (infer && !held.left && !held.right) {
      send(kLeft, true);
      inferred_left_ = true;
    }
  }

  template <typename Send>
  void ReleaseInferred(Send send) {
    if (inferred_left_) send(kLeft, false);
    inferred_left_ = false;
  }

 private:
  bool inferred_left_ = false;
};

}  // namespace crossdesk

#endif