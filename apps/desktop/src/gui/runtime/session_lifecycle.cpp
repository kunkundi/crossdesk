#include "runtime/session_lifecycle.h"

namespace crossdesk::gui_detail {

CallbackGuard::CallbackGuard(SessionCallbackGate& callbacks)
    : callbacks_(callbacks), accepted_(callbacks.BeginCallback()) {}

CallbackGuard::~CallbackGuard() {
  if (accepted_) {
    callbacks_.EndCallback();
  }
}

bool SessionCallbackGate::IsActive() const {
  std::lock_guard lock(mutex_);
  return accepting_callbacks_;
}

CallbackGuard SessionCallbackGate::Enter() {
  return CallbackGuard(*this);
}

bool SessionCallbackGate::BeginCallback() {
  std::lock_guard lock(mutex_);
  if (!accepting_callbacks_) {
    return false;
  }
  ++running_callbacks_;
  return true;
}

void SessionCallbackGate::EndCallback() {
  std::lock_guard lock(mutex_);
  --running_callbacks_;
  if (running_callbacks_ == 0) {
    callbacks_finished_.notify_all();
  }
}

bool SessionCallbackGate::StopAcceptingIfIdle() {
  std::lock_guard lock(mutex_);
  if (!accepting_callbacks_ || running_callbacks_ != 0) return false;
  accepting_callbacks_ = false;
  return true;
}

void SessionCallbackGate::StopAccepting() {
  std::lock_guard lock(mutex_);
  accepting_callbacks_ = false;
}

void SessionCallbackGate::Close() {
  std::unique_lock lock(mutex_);
  accepting_callbacks_ = false;
  // Waiting releases the lock so running callbacks can call EndCallback().
  callbacks_finished_.wait(lock, [this] { return running_callbacks_ == 0; });
}

}  // namespace crossdesk::gui_detail
