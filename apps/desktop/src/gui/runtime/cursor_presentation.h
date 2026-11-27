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
    hidden_reason_ = state.render_mode == CursorRenderMode::hidden
                         ? state.hidden_reason : CursorHiddenReason::unspecified;
    if (separate) {
      mac_locator_allowed_ = state.shape != RemoteCursorShape::none;
    } else if (hidden_reason_ != CursorHiddenReason::system_hidden) {
      // Embedded cursors and other hiding policies end the separate-cursor
      // history. A later system_hidden sample must not revive that overlay.
      mac_locator_allowed_ = false;
    }
    held_shape_ = separate ? state.shape : RemoteCursorShape::none;
    if (held_shape_ != RemoteCursorShape::none) {
      last_visible_shape_ = held_shape_;
    }
    if (hidden_reason_ != CursorHiddenReason::no_pointing_device)
      fallback_engaged_ = false;
  }

  void NoteInput(uint64_t now) { last_input_ = now; }

  RemoteCursorShape Resolve(uint64_t now, HostPlatform remote_platform) {
    if (!stale_at_) return RemoteCursorShape::default_cursor;

    // macOS can keep its system cursor hidden after typing even while remote
    // mouse events move it. Keep a previously separate desktop locator across
    // those heartbeats; the video has no pointer to fall back to. On a new
    // connection, use an arrow until the first visible shape arrives. The
    // protocol cannot distinguish typing from other system_hidden cases;
    // restrict this policy to identified Mac hosts and stop on any explicit
    // embedded cursor or other hiding policy.
    if (remote_platform == HostPlatform::MacOS &&
        hidden_reason_ == CursorHiddenReason::system_hidden &&
        mac_locator_allowed_) {
      return last_visible_shape_;
    }

    const bool recent_input =
        last_input_ && now - *last_input_ < kInputActivityMs;
    // Windows may keep the cursor suppressed after touch or pen input even
    // while a remote mouse is moving. Give that controller a local pointer.
    if (remote_platform == HostPlatform::Windows && recent_input &&
        !sample_unavailable_ && hidden_reason_ == CursorHiddenReason::system_suppressed)
      return RemoteCursorShape::default_cursor;
    const bool can_recover =
        now >= *stale_at_ || (!sample_unavailable_ &&
                              hidden_reason_ == CursorHiddenReason::no_pointing_device);
    if (can_recover && recent_input) {
      fallback_engaged_ = true;
    }
    // Once needed, retain the locator while idle until a good non-compatibility
    // sample arrives. Otherwise it would blink off whenever input pauses.
    return fallback_engaged_ ? last_visible_shape_ : held_shape_;
  }

 private:
  bool sample_unavailable_ = false;
  CursorHiddenReason hidden_reason_ = CursorHiddenReason::unspecified;
  bool mac_locator_allowed_ = true;
  bool fallback_engaged_ = false;
  std::optional<uint64_t> stale_at_;
  std::optional<uint64_t> last_input_;
  RemoteCursorShape held_shape_ = RemoteCursorShape::default_cursor;
  RemoteCursorShape last_visible_shape_ = RemoteCursorShape::default_cursor;
};

}  // namespace crossdesk

#endif
