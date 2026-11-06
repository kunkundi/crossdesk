/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-22
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _REMOTE_INPUT_DISPATCHER_H_
#define _REMOTE_INPUT_DISPATCHER_H_

#include <remote_action.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "input_outcome.h"

namespace crossdesk {

// Serializes native input, recovery and display resets off the RTC thread.
class RemoteInputDispatcher {
 public:
  using Apply =
      std::function<InputOutcome(const std::string&, const RemoteAction&)>;
  using Release = std::function<InputOutcome(const std::string&)>;
  using Report =
      std::function<void(const std::string&, int64_t, int64_t, InputOutcome)>;
  using ResetDisplay = std::function<InputOutcome()>;
  // Optional targeted adapter; legacy callers fall back to full Release.
  using ReleaseMouse =
      std::function<InputOutcome(const std::string&, unsigned)>;
  using Completion = std::function<void(InputOutcome)>;
  enum class Result { queued, coalesced, rejected, overflow };
  static constexpr size_t kMaxPendingPerController = 128;

  RemoteInputDispatcher(Apply apply, Release release, Report report = {},
                        ResetDisplay reset_display = {},
                        ReleaseMouse release_mouse = {});
  ~RemoteInputDispatcher();
  static bool Accepts(const RemoteAction& action);
  void Connect(const std::string& id);
  Result Push(const std::string& id, const RemoteAction& action,
              Completion completion = {});
  void Disconnect(const std::string& id);

  // Scheduled recovery is not idle work. Check HasPendingReleases() as well,
  // or Stop() before destroying the devices used by callbacks.
  void WaitIdle();
  bool HasPendingReleases();
  // Cancels pending input/timers and makes a final bounded cleanup attempt.
  void Stop();

 private:
  using Clock = std::chrono::steady_clock;
  enum class ItemKind { input, cleanup, reset_display };
  struct Item {
    std::string id;
    std::optional<RemoteAction> action;  // Present only for ordinary input.
    Clock::time_point queued_at;
    ItemKind kind = ItemKind::cleanup;
    // Zero requests full controller cleanup; otherwise only these mouse
    // buttons.
    unsigned mouse_mask = 0;
    Completion completion;
    bool IsReset() const { return kind == ItemKind::reset_display; }
    bool IsInput() const { return kind == ItemKind::input; }
    bool IsCleanup() const { return kind == ItemKind::cleanup; }
  };
  struct Retry {
    Clock::time_point due;
    std::chrono::milliseconds delay{100};
    unsigned mouse_mask = 0;
  };
  struct LogState {
    Clock::time_point slow{};
    std::array<Clock::time_point, 2> failure{};
  };

  static bool IsMove(const RemoteAction& action);
  static unsigned MouseUpMask(const RemoteAction& action);
  void RemoveResetsLocked();
  void QueueReleaseLocked(const std::string& id);
  void QueueMouseReleaseLocked(const std::string& id, unsigned mask);
  void RemoveQueuedCleanupLocked(const std::string& id);
  void ScheduleRetryLocked(const std::string& id, unsigned mouse_mask,
                           Clock::time_point finished);
  void CompleteCleanupLocked(const Item& item, InputOutcome outcome,
                             Clock::time_point finished);
  std::optional<Item> NextLocked();
  InputOutcome Execute(const Item& item);
  bool ShouldReportLocked(const Item& item, Clock::time_point now,
                          int64_t queued_ms, int64_t execution_ms,
                          InputOutcome outcome);
  void Run();

  Apply apply_;
  Release release_;
  Report report_;
  ResetDisplay reset_display_;
  ReleaseMouse release_mouse_;
  std::mutex mutex_;
  std::condition_variable ready_, idle_;
  std::deque<Item> pending_;
  std::unordered_set<std::string> connected_, blocked_;
  std::unordered_map<std::string, size_t> pending_counts_;
  std::unordered_map<std::string, Retry> retries_;
  std::unordered_map<std::string, LogState> logs_;
  bool last_was_retry_ = false;
  bool stopping_ = false;
  bool executing_ = false;
  std::once_flag stop_once_;
  std::thread worker_;
};

}  // namespace crossdesk

#endif
