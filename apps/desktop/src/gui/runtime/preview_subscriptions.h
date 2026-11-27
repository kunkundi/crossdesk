/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-12
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PREVIEW_SUBSCRIPTIONS_H_
#define _PREVIEW_SUBSCRIPTIONS_H_

#include <preview_subscription.h>

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace crossdesk {
// Accessed by the network callback and screen-capture callback. Legacy
// controllers have no entry and continue receiving their normal stream.
class PreviewSubscriptions {
public:
  using Clock = std::chrono::steady_clock;
  std::optional<PreviewSubscription> Get(const std::string &id) {
    std::lock_guard lock(mutex_);
    const auto found = states_.find(id);
    return found == states_.end() ? std::nullopt
                                 : std::optional(found->second.request);
  }
  bool Set(const std::string &id, const PreviewSubscription &request) {
    std::lock_guard lock(mutex_);
    const auto found = states_.find(id);
    if (found != states_.end() &&
        !request.IsNewerThan(found->second.request))
      return false;
    auto &state = states_[id];
    if (state.request.fps != request.fps) state.next.clear();
    state.request = request;
    if (request.fps > 0) frame_rates_[id] = request.frame_rate;
    return true;
  }
  void SetVideoFrameRate(const std::string& id, int fps) {
    std::lock_guard lock(mutex_);
    frame_rates_[id] = fps;
  }
  // Shared interactive capture has two rates. Sparse and paused previews
  // keep their own downstream cadence and do not require a 60 FPS source.
  int CaptureFrameRate(const std::vector<std::string>& connected) {
    std::lock_guard lock(mutex_);
    for (const auto& id : connected) {
      const auto preview = states_.find(id);
      if (preview != states_.end() && preview->second.request.fps < 60) continue;
      const auto rate = frame_rates_.find(id);
      if (rate == frame_rates_.end() || rate->second == 60) return 60;
    }
    return 30;
  }
  bool WantsVideo(const std::vector<std::string>& connected) {
    std::lock_guard lock(mutex_);
    for (const auto& id : connected) {
      const auto it = states_.find(id);
      if (it == states_.end() || it->second.request.fps > 0) return true;
    }
    return false;
  }
  bool ShouldSend(const std::string &id, Clock::time_point now = Clock::now(),
                  const std::string &stream = {}) {
    std::lock_guard lock(mutex_);
    const auto found = states_.find(id);
    if (found == states_.end())
      return true;
    auto &state = found->second;
    // MiniRTC owns pause/resume. This gate only limits preview cadence, so
    // paused input reaches its cheap send gate without dividing by zero here.
    if (!state.request.fps)
      return true;
    // Interactive streams use the encoder's own 30/60 FPS cadence. A second
    // time gate here can drop valid frames because capture timing varies.
    if (state.request.fps == 60)
      return true;
    auto &next = state.next[stream];
    if (state.request.fps == 15) {
      // Keep a stable 15 FPS deadline across capture jitter. Rebasing every
      // frame can turn small timing errors into extra dropped capture frames.
      const auto interval = std::chrono::nanoseconds(1'000'000'000 / 15);
      if (now + std::chrono::milliseconds(1) < next) return false;
      next = next == Clock::time_point{} || now - next > interval
          ? now + interval : next + interval;
      return true;
    }
    if (now < next)
      return false;
    next = now + std::chrono::milliseconds(1000 / state.request.fps);
    return true;
  }
  void Remove(const std::string &id) {
    std::lock_guard lock(mutex_);
    states_.erase(id);
    frame_rates_.erase(id);
  }

private:
  struct State {
    PreviewSubscription request;
    // One display must not consume another display's preview frame budget.
    std::unordered_map<std::string, Clock::time_point> next;
  };
  std::mutex mutex_;
  std::unordered_map<std::string, State> states_;
  std::unordered_map<std::string, int> frame_rates_;
};
} // namespace crossdesk

#endif
