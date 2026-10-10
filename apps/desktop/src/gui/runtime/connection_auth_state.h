#ifndef CROSSDESK_CONNECTION_AUTH_STATE_H_
#define CROSSDESK_CONNECTION_AUTH_STATE_H_

#include <chrono>
#include <mutex>
#include <string_view>

#include "join_failure.h"

namespace crossdesk {

// One remote session's password prompt and server-requested retry delay.
// Signaling callbacks and the UI access a single synchronized snapshot.
class ConnectionAuthState {
 public:
  using Clock = std::chrono::steady_clock;
  struct Snapshot {
    bool password_required = false;
    bool validating = false;
    bool incorrect_password = false;
    minirtc::JoinFailureKind error = minirtc::JoinFailureKind::Unknown;
    int retry_after = 0;
  };

  bool Begin(std::string_view password, Clock::time_point now = Clock::now()) {
    std::lock_guard lock(mutex_);
    if (retry_at_ > now) return false;
    state_ = {};
    retry_at_ = {};
    state_.password_required = password.empty();
    state_.validating = !password.empty();
    return state_.validating;
  }

  void Rejected(const nlohmann::json& reply,
                Clock::time_point now = Clock::now()) {
    const auto failure = minirtc::ParseJoinFailure(reply);
    std::lock_guard lock(mutex_);
    state_ = {};
    state_.error = failure.kind;
    state_.incorrect_password =
        failure.kind == minirtc::JoinFailureKind::AuthenticationFailed;
    state_.password_required =
        state_.incorrect_password ||
        failure.kind == minirtc::JoinFailureKind::PasswordRequired;
    retry_at_ = now + std::chrono::seconds(failure.retry_after);
  }

  void Complete() {
    std::lock_guard lock(mutex_);
    state_.validating = false;
  }

  Snapshot Get(Clock::time_point now = Clock::now()) const {
    std::lock_guard lock(mutex_);
    auto snapshot = state_;
    if (retry_at_ > now) {
      snapshot.retry_after = static_cast<int>(
          std::chrono::ceil<std::chrono::seconds>(retry_at_ - now).count());
    }
    return snapshot;
  }

 private:
  mutable std::mutex mutex_;
  Snapshot state_;
  Clock::time_point retry_at_{};
};

}  // namespace crossdesk

#endif
