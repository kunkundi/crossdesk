#include "update_checker.h"

#include <algorithm>
#include <utility>

#include "rd_log.h"

namespace crossdesk {
using namespace std::chrono_literals;

UpdateChecker::UpdateChecker(std::string current_version, Fetch fetch)
    : current_version_(std::move(current_version)), fetch_(std::move(fetch)) {}

UpdateChecker::~UpdateChecker() { Stop(); }

bool UpdateChecker::RequestCheck(TimePoint now) {
  if (stopped_ || pending_.valid()) {
    return false;
  }
  try {
    pending_ = std::async(std::launch::async, fetch_);
    status_ = Status::Checking;
    return true;
  } catch (const std::exception& error) {
    LOG_WARN("Could not start update check: {}", error.what());
    ScheduleRetry(now);
    return false;
  }
}

bool UpdateChecker::Poll(TimePoint now) {
  if (stopped_) {
    return false;
  }
  if (!pending_.valid()) {
    if (now >= next_check_) {
      RequestCheck(now);
    }
    return false;
  }
  if (pending_.wait_for(0ms) != std::future_status::ready) {
    return false;
  }

  std::optional<VersionInfo> result;
  try {
    result = pending_.get();
  } catch (const std::exception& error) {
    LOG_WARN("Update check failed: {}", error.what());
  } catch (...) {
    LOG_WARN("Update check failed with an unknown error");
  }
  if (!result) {
    ScheduleRetry(now);
    return true;
  }
  latest_ = std::move(result);
  update_available_ =
      IsNewerVersionWithMetadata(current_version_, latest_->version,
                                 latest_->release_date, latest_->patch);
  status_ = update_available_ ? Status::Available : Status::UpToDate;
  failures_ = 0;
  next_check_ = now + 6h;
  return true;
}

void UpdateChecker::ScheduleRetry(TimePoint now) {
  constexpr std::chrono::minutes delays[] = {5min, 30min, 60min};
  next_check_ = now + delays[std::min(failures_, 2u)];
  failures_ = std::min(failures_ + 1, 3u);
  status_ = Status::Failed;
}

void UpdateChecker::Stop() {
  stopped_ = true;
  // Join before application/logging teardown. The HTTP client uses finite
  // connection/read/write and overall request timeouts; no worker is detached.
  if (pending_.valid()) {
    pending_.wait();
    pending_ = {};
  }
}

}  // namespace crossdesk
