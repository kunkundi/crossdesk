/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-21
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WINDOW_SHIFT_STATE_H_
#define _WINDOW_SHIFT_STATE_H_

#include <array>

namespace crossdesk {

// Synchronizes Slint window keyboard events and modifier snapshots with the
// remote session's pressed-key table, including provisional Shift inference,
// missed modifier key-up recovery, pointer-click reconciliation, and handoff
// to a native keyboard capturer.
class WindowShiftState {
 public:
  static constexpr int kLeft = 0xA0;
  static constexpr int kRight = 0xA1;
  static constexpr int kLeftControl = 0xA2;
  static constexpr int kRightControl = 0xA3;
  static constexpr int kLeftAlt = 0xA4;
  static constexpr int kRightAlt = 0xA5;
  static constexpr int kLeftMeta = 0x5B;
  static constexpr int kRightMeta = 0x5C;

  struct Modifiers {
    bool control = false;
    bool alt = false;
    bool shift = false;
    bool meta = false;
  };

  struct Held {
    bool left = false;
    bool right = false;
    bool left_control = false;
    bool right_control = false;
    bool left_alt = false;
    bool right_alt = false;
    bool left_meta = false;
    bool right_meta = false;
  };

  static constexpr bool IsModifierOrLockKey(int key) {
    switch (key) {
      case 0x10:  // VK_SHIFT
      case 0x11:  // VK_CONTROL
      case 0x12:  // VK_MENU
      case 0x14:  // VK_CAPITAL
      case 0x90:  // VK_NUMLOCK
      case 0x91:  // VK_SCROLL
      case kLeft:
      case kRight:
      case kLeftControl:
      case kRightControl:
      case kLeftAlt:
      case kRightAlt:
      case kLeftMeta:
      case kRightMeta:
        return true;
      default:
        return false;
    }
  }

  template <typename Send>
  void Forward(int key, bool down, Modifiers modifiers, Held held, Send send) {
    // Reconcile non-Shift modifiers whose key-up was lost while unfocused
    // (e.g. Alt+Tab, Super/Win, or Ctrl+... window switching). Preserve AltGr
    // chords where right_alt may pair with synthetic left_control or Mod5Mask.
    if (key != kLeftControl && key != kRightControl &&
        !(key == kRightAlt && down) && !modifiers.control && !held.right_alt) {
      if (held.left_control) {
        ClearWindowKey(kLeftControl);
        send(kLeftControl, false);
      }
      if (held.right_control) {
        ClearWindowKey(kRightControl);
        send(kRightControl, false);
      }
    }
    if (key != kLeftAlt && key != kRightAlt && !modifiers.alt) {
      if (held.left_alt) {
        ClearWindowKey(kLeftAlt);
        send(kLeftAlt, false);
      }
    }
    if (key != kLeftMeta && key != kRightMeta && !modifiers.meta) {
      if (held.left_meta) {
        ClearWindowKey(kLeftMeta);
        send(kLeftMeta, false);
      }
      if (held.right_meta) {
        ClearWindowKey(kRightMeta);
        send(kRightMeta, false);
      }
    }

    if (key == kLeft || key == kRight) {
      SetWindowKey(key, down);
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

    if (!modifiers.shift) {
      // A window may also miss a key-up. Trust the snapshot on an ordinary
      // key and release whichever side was forwarded before sending it.
      if (held.left) {
        ClearWindowKey(kLeft);
        send(kLeft, false);
      }
      if (held.right) {
        ClearWindowKey(kRight);
        send(kRight, false);
      }
      inferred_left_ = false;
    } else if (down && !IsModifierOrLockKey(key) && !held.left && !held.right) {
      // Never synthesize Shift on key-released or on other modifier/lock keys:
      // lagged modifier state after releasing Shift or OS/IME modifier chords
      // (Alt+Shift, Ctrl+Shift) would otherwise leave Shift stuck down.
      send(kLeft, true);
      inferred_left_ = true;
    }
    SetWindowKey(key, down);
    send(key, down);
  }

  template <typename Send>
  void Forward(int key, bool down, bool shift, Held held, Send send) {
    Forward(key, down, Modifiers{false, false, shift, false}, held, send);
  }

  template <typename Send>
  void ReleaseInferred(Send send) {
    if (inferred_left_) send(kLeft, false);
    inferred_left_ = false;
  }

  // Reconciles held modifiers against live OS modifier state right before a
  // remote pointer press or wheel scroll in window-event mode.
  template <typename Send>
  void ReconcileModifiers(Modifiers live, Held held, Send send) {
    if (!live.shift) {
      if (held.left || inferred_left_) {
        ClearWindowKey(kLeft);
        inferred_left_ = false;
        if (held.left) send(kLeft, false);
      }
      if (held.right) {
        ClearWindowKey(kRight);
        send(kRight, false);
      }
    }
    if (!live.control) {
      if (held.left_control) {
        ClearWindowKey(kLeftControl);
        send(kLeftControl, false);
      }
      if (held.right_control) {
        ClearWindowKey(kRightControl);
        send(kRightControl, false);
      }
    }
    if (!live.alt) {
      if (held.left_alt) {
        ClearWindowKey(kLeftAlt);
        send(kLeftAlt, false);
      }
      if (held.right_alt) {
        ClearWindowKey(kRightAlt);
        send(kRightAlt, false);
      }
    }
    if (!live.meta) {
      if (held.left_meta) {
        ClearWindowKey(kLeftMeta);
        send(kLeftMeta, false);
      }
      if (held.right_meta) {
        ClearWindowKey(kRightMeta);
        send(kRightMeta, false);
      }
    }
  }

  template <typename Send>
  void ReleaseWindowKey(int key, bool is_currently_pressed, Send send) {
    if (ConsumeWindowKey(key) && is_currently_pressed) {
      send(key, false);
    }
  }

  bool HasWindowKey(int key) const {
    return key >= 0 && key < static_cast<int>(window_keys_.size()) &&
           window_keys_[static_cast<size_t>(key)];
  }

  bool ConsumeWindowKey(int key) {
    if (!HasWindowKey(key)) return false;
    window_keys_[static_cast<size_t>(key)] = false;
    return true;
  }

  void ClearWindowKey(int key) {
    if (key >= 0 && key < static_cast<int>(window_keys_.size())) {
      window_keys_[static_cast<size_t>(key)] = false;
    }
  }

  void ClearWindowKeys() { window_keys_.fill(false); }

 private:
  void SetWindowKey(int key, bool down) {
    if (key >= 0 && key < static_cast<int>(window_keys_.size())) {
      window_keys_[static_cast<size_t>(key)] = down;
    }
  }

  bool inferred_left_ = false;
  std::array<bool, 256> window_keys_{};
};

}  // namespace crossdesk

#endif