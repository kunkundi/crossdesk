/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-24
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _UPDATE_CHECKER_H_
#define _UPDATE_CHECKER_H_

#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <string>

#include "version_checker.h"

namespace crossdesk {

// Owned and polled by the UI thread. Only the fetch function runs on a worker;
// it returns a value and never accesses application or UI state.
class UpdateChecker {
 public:
  // Wall time includes suspend time, so an overdue check runs on the first UI
  // tick after wake, including on platforms whose steady clock pauses in sleep.
  using Clock = std::chrono::system_clock;
  using TimePoint = Clock::time_point;
  using Fetch = std::function<std::optional<VersionInfo>()>;
  enum class Status { Idle, Checking, UpToDate, Available, Failed };

  explicit UpdateChecker(std::string current_version,
                         Fetch fetch = CheckUpdate);
  ~UpdateChecker();

  // Manual requests bypass the schedule, but share an already running check.
  bool RequestCheck(TimePoint now = Clock::now());
  // Starts due checks and consumes ready results without blocking. Returns true
  // when a result was consumed; failure leaves the last successful info intact.
  bool Poll(TimePoint now = Clock::now());
  void Stop();

  Status status() const { return status_; }
  bool update_available() const { return update_available_; }
  const std::optional<VersionInfo>& latest() const { return latest_; }

 private:
  void ScheduleRetry(TimePoint now);

  std::string current_version_;
  Fetch fetch_;
  std::future<std::optional<VersionInfo>> pending_;
  std::optional<VersionInfo> latest_;
  TimePoint next_check_{};
  Status status_ = Status::Idle;
  unsigned int failures_ = 0;
  bool update_available_ = false;
  bool stopped_ = false;
};

}  // namespace crossdesk

#endif