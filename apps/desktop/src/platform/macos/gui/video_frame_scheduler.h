/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-15
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VIDEO_FRAME_SCHEDULER_H_
#define _VIDEO_FRAME_SCHEDULER_H_

#include <functional>
#include <memory>

namespace crossdesk {

// Coalesces decoded-frame notifications onto the AppKit main thread. There is
// at most one queued wakeup, carrying no pixels or session identity. The native
// compositor keeps its existing latest-frame slot and display synchronization.
class MacVideoFrameScheduler {
 public:
  MacVideoFrameScheduler();
  ~MacVideoFrameScheduler();
  MacVideoFrameScheduler(const MacVideoFrameScheduler&) = delete;
  MacVideoFrameScheduler& operator=(const MacVideoFrameScheduler&) = delete;

  // All object methods are main-thread-only. Only the returned notifier may
  // be called from any thread; it is harmless after Stop or destruction.
  void Start(std::function<void(double)> callback);
  void SetEnabled(bool enabled);
  void Stop();
  bool IsRunning() const;
  std::function<void()> Notifier() const;
  void Request() const;

 private:
  struct State;
  static void Request(const std::shared_ptr<State>& state);
  std::shared_ptr<State> state_;
};

}  // namespace crossdesk

#endif
