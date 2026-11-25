/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-07
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SESSION_LIFECYCLE_H_
#define _SESSION_LIFECYCLE_H_

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace crossdesk::gui_detail {

class SessionCallbackGate;

// Decrements the count on scope exit, including early returns and exceptions.
class CallbackGuard {
 public:
  explicit CallbackGuard(SessionCallbackGate& callbacks);
  ~CallbackGuard();

  CallbackGuard(const CallbackGuard&) = delete;
  CallbackGuard& operator=(const CallbackGuard&) = delete;

  explicit operator bool() const { return accepted_; }

 private:
  SessionCallbackGate& callbacks_;
  bool accepted_;
};

// Each peer counts callbacks as they enter and leave.
// Closing rejects new callbacks, then waits for running callbacks to finish.
class SessionCallbackGate {
 public:
  bool IsActive() const;
  CallbackGuard Enter();

  // Called by the session owner, never from within this session's callbacks.
  bool StopAcceptingIfIdle();
  void StopAccepting();
  void Close();

 private:
  friend class CallbackGuard;

  bool BeginCallback();
  void EndCallback();

  mutable std::mutex mutex_;
  std::condition_variable callbacks_finished_;
  bool accepting_callbacks_ = true;
  std::size_t running_callbacks_ = 0;
};

}  // namespace crossdesk::gui_detail

#endif
