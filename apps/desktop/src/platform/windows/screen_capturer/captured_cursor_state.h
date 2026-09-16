/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-16
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CAPTURED_CURSOR_STATE_H_
#define _CAPTURED_CURSOR_STATE_H_

#include <atomic>

namespace crossdesk {

// Metadata belongs to the frame callback, not to the requested capture mode.
// WGC's DLL adapter copies its frame metadata into this executable-local scope.
inline thread_local bool current_frame_has_cursor = false;

class CapturedCursorFrameScope {
 public:
  explicit CapturedCursorFrameScope(bool embedded)
      : previous_(current_frame_has_cursor) {
    current_frame_has_cursor = embedded;
  }
  ~CapturedCursorFrameScope() { current_frame_has_cursor = previous_; }
  CapturedCursorFrameScope(const CapturedCursorFrameScope&) = delete;
  CapturedCursorFrameScope& operator=(const CapturedCursorFrameScope&) = delete;

 private:
  bool previous_;
};

class CapturedCursorState {
 public:
  void Update(bool embedded, void* monitor) {
    embedded_monitor_.store(embedded ? monitor : nullptr,
                            std::memory_order_relaxed);
  }
  void Reset() { Update(false, nullptr); }
  bool IsEmbedded(void* monitor) const {
    return monitor && embedded_monitor_.load(std::memory_order_relaxed) == monitor;
  }

 private:
  // This opaque handle is the entire state; no other data needs publishing.
  std::atomic<void*> embedded_monitor_{nullptr};
};

inline CapturedCursorState& SharedCapturedCursorState() {
  static CapturedCursorState state;
  return state;
}

}  // namespace crossdesk

#endif
