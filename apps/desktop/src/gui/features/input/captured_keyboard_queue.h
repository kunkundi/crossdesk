/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CAPTURED_KEYBOARD_QUEUE_H_
#define _CAPTURED_KEYBOARD_QUEUE_H_

#include <array>
#include <cstddef>
#include <mutex>
#include <vector>

#include "keyboard_state.h"

namespace crossdesk {

class CapturedKeyboardQueue {
 public:
  struct Batch {
    std::vector<KeyboardInput> events;
    bool resync = false;
    PressedKeys pressed;
  };

  void Push(const KeyboardInput& input) {
    std::lock_guard lock(mutex_);
    if (input.is_down)
      pressed_[input.key_code] =
          {input.key_code, input.scan_code, input.extended};
    else
      pressed_.erase(input.key_code);

    if (resync_) return;
    if (count_ == kCapacity) {
      // The event history is incomplete. Retain the authoritative state,
      // including releases, rather than dropping arbitrary key edges.
      count_ = 0;
      resync_ = true;
      return;
    }
    events_[count_++] = input;
  }

  Batch Drain() {
    std::lock_guard lock(mutex_);
    Batch batch;
    batch.resync = resync_;
    if (resync_) batch.pressed = pressed_;
    else batch.events.assign(events_.begin(), events_.begin() + count_);
    count_ = 0;
    resync_ = false;
    return batch;
  }

  void Clear() {
    std::lock_guard lock(mutex_);
    count_ = 0;
    resync_ = false;
    pressed_.clear();
  }

 private:
  static constexpr size_t kCapacity = 512;
  std::mutex mutex_;
  std::array<KeyboardInput, kCapacity> events_{};
  size_t count_ = 0;
  bool resync_ = false;
  PressedKeys pressed_;
};

}  // namespace crossdesk

#endif