/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-21
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WINDOW_SHIFT_STATE_H_
#define _WINDOW_SHIFT_STATE_H_

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

    if (!shift) {
      // A window may also miss a key-up. Trust the snapshot on an ordinary
      // key and release whichever side was forwarded before sending it.
      if (held.left) send(kLeft, false);
      if (held.right) send(kRight, false);
      inferred_left_ = false;
    } else if (!held.left && !held.right) {
      send(kLeft, true);
      inferred_left_ = true;
    }
    send(key, down);
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