/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-16
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CURSOR_PRESENTATION_H_
#define _CURSOR_PRESENTATION_H_

#include <remote_action.h>

#include <cstdint>
#include <optional>

namespace crossdesk {

// All times use one monotonic millisecond clock. The owning session's cursor
// mutex protects this policy from the transport and UI threads.
class CursorPresentation {
 public:
  static constexpr uint64_t kStateTimeoutMs = 1500;  // Three 500ms heartbeats.
  static constexpr uint64_t kInputActivityMs = 1500;

  void Observe(const CursorState& state, uint64_t now) {
    if (state.render_mode == CursorRenderMode::unknown) {
      // Repeated unavailable samples must not extend the recovery deadline.
      if (!sample_unavailable_) stale_at_ = now + kStateTimeoutMs;
      sample_unavailable_ = true;
      return;
    }
    sample_unavailable_ = false;
    stale_at_ = now + kStateTimeoutMs;
    const bool separate =
        state.render_mode == CursorRenderMode::separate ||
        (state.render_mode == CursorRenderMode::legacy && state.visible);
    // Old senders cannot distinguish embedded from hidden. Only explicit
    // metadata from a new sender can establish the no-device compatibility
    // case.
    no_device_ = state.render_mode == CursorRenderMode::hidden &&
                 state.hidden_reason == CursorHiddenReason::no_pointing_device;
    held_shape_ = separate ? state.shape : RemoteCursorShape::none;
    if (held_shape_ != RemoteCursorShape::none) {
      last_visible_shape_ = held_shape_;
    }
    if (!no_device_) fallback_engaged_ = false;
  }

  void NoteInput(uint64_t now) { last_input_ = now; }

  RemoteCursorShape Resolve(uint64_t now) {
    if (!stale_at_) return RemoteCursorShape::default_cursor;

    const bool recent_input =
        last_input_ && now - *last_input_ < kInputActivityMs;
    const bool can_recover =
        now >= *stale_at_ || (!sample_unavailable_ && no_device_);
    if (can_recover && recent_input) {
      fallback_engaged_ = true;
    }
    // Once needed, retain the locator while idle until a good non-compatibility
    // sample arrives. Otherwise it would blink off whenever input pauses.
    return fallback_engaged_ ? last_visible_shape_ : held_shape_;
  }

 private:
  bool sample_unavailable_ = false;
  bool no_device_ = false;
  bool fallback_engaged_ = false;
  std::optional<uint64_t> stale_at_;
  std::optional<uint64_t> last_input_;
  RemoteCursorShape held_shape_ = RemoteCursorShape::default_cursor;
  RemoteCursorShape last_visible_shape_ = RemoteCursorShape::default_cursor;
};

}  // namespace crossdesk

#endif