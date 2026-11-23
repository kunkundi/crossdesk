#include "platform/macos/gui/video_frame_scheduler.h"

#import <Foundation/Foundation.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <utility>

namespace crossdesk {

struct MacVideoFrameScheduler::State {
  std::atomic<bool> enabled{false};
  std::atomic<bool> queued{false};
  // Only the main thread reads or changes the callback.
  std::function<void(double)> callback;
};

MacVideoFrameScheduler::MacVideoFrameScheduler() = default;
MacVideoFrameScheduler::~MacVideoFrameScheduler() { Stop(); }

void MacVideoFrameScheduler::Start(std::function<void(double)> callback) {
  assert(NSThread.isMainThread);
  Stop();
  state_ = std::make_shared<State>();
  state_->callback = std::move(callback);
}

void MacVideoFrameScheduler::SetEnabled(bool enabled) {
  assert(NSThread.isMainThread);
  if (state_) state_->enabled.store(enabled, std::memory_order_release);
}

void MacVideoFrameScheduler::Stop() {
  assert(NSThread.isMainThread);
  if (!state_) return;
  state_->enabled.store(false, std::memory_order_release);
  state_->callback = {};
  state_.reset();
}

bool MacVideoFrameScheduler::IsRunning() const {
  assert(NSThread.isMainThread);
  return state_ && bool(state_->callback);
}

std::function<void()> MacVideoFrameScheduler::Notifier() const {
  assert(NSThread.isMainThread);
  return [state = state_] { Request(state); };
}

void MacVideoFrameScheduler::Request() const {
  assert(NSThread.isMainThread);
  Request(state_);
}

void MacVideoFrameScheduler::Request(std::shared_ptr<State> state) {
  if (!state || !state->enabled.load(std::memory_order_acquire) ||
      state->queued.exchange(true, std::memory_order_acq_rel)) return;
  const auto queued_at = std::chrono::steady_clock::now();
  // The block owns the state even if the notifier is destroyed before it runs.
  // Stop clears its callback so an old wakeup cannot present after teardown.
  dispatch_async(dispatch_get_main_queue(), ^{
    // Clear before rendering so arrival during rendering can queue one more
    // wakeup. Clearing afterwards would lose that last dirty notification.
    state->queued.store(false, std::memory_order_release);
    if (!state->enabled.load(std::memory_order_acquire)) return;
    auto callback = state->callback;
    if (callback) callback(std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - queued_at).count());
  });
}

}  // namespace crossdesk
