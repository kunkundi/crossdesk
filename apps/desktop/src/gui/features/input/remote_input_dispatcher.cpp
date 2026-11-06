#include "remote_input_dispatcher.h"

#include <algorithm>
#include <utility>

namespace crossdesk {

RemoteInputDispatcher::RemoteInputDispatcher(Apply apply, Release release,
                                             Report report,
                                             ResetDisplay reset_display,
                                             ReleaseMouse release_mouse)
    : apply_(std::move(apply)),
      release_(std::move(release)),
      report_(std::move(report)),
      reset_display_(std::move(reset_display)),
      release_mouse_(std::move(release_mouse)),
      worker_([this] { Run(); }) {}

RemoteInputDispatcher::~RemoteInputDispatcher() { Stop(); }

bool RemoteInputDispatcher::Accepts(const RemoteAction& action) {
  return action.type == ControlType::mouse ||
         action.type == ControlType::keyboard ||
         action.type == ControlType::keyboard_state ||
         action.type == ControlType::text_input ||
         action.type == ControlType::display_id ||
         action.type == ControlType::service_command;
}

void RemoteInputDispatcher::Connect(const std::string& id) {
  if (id.empty()) return;
  std::lock_guard lock(mutex_);
  if (stopping_ || !connected_.insert(id).second) return;
  logs_.erase(id);  // A new session must get its first diagnostic.
  // In-flight resets finish before this session's input; queued old resets
  // must not change the new session's display. Failed releases are retained.
  RemoveResetsLocked();
  ready_.notify_one();
}

RemoteInputDispatcher::Result RemoteInputDispatcher::Push(
    const std::string& id, const RemoteAction& action, Completion completion) {
  // HostInfo owns raw arrays; it must never enter this value-copy queue.
  if (!Accepts(action)) return Result::rejected;
  std::lock_guard lock(mutex_);
  if (stopping_ || !connected_.count(id)) return Result::rejected;
  if (!completion && IsMove(action) && !pending_.empty()) {
    auto& last = pending_.back();
    if (!last.completion && last.id == id && last.IsInput() && IsMove(*last.action)) {
      last.action = action;
      last.queued_at = Clock::now();
      return Result::coalesced;
    }
  }
  if (pending_counts_[id] >= kMaxPendingPerController) {
    QueueReleaseLocked(id);
    return Result::overflow;
  }
  pending_.push_back({id, action, Clock::now(), ItemKind::input, 0, std::move(completion)});
  ++pending_counts_[id];
  ready_.notify_one();
  return Result::queued;
}

void RemoteInputDispatcher::Disconnect(const std::string& id) {
  std::lock_guard lock(mutex_);
  if (stopping_ || !connected_.erase(id)) return;
  QueueReleaseLocked(id);
  if (connected_.empty() && reset_display_) {
    RemoveResetsLocked();
    pending_.push_back(
        {{}, std::nullopt, Clock::now(), ItemKind::reset_display});
  }
  ready_.notify_one();
}

void RemoteInputDispatcher::WaitIdle() {
  std::unique_lock lock(mutex_);
  idle_.wait(lock, [this] { return pending_.empty() && !executing_; });
}

bool RemoteInputDispatcher::HasPendingReleases() {
  std::lock_guard lock(mutex_);
  return !blocked_.empty();
}

void RemoteInputDispatcher::Stop() {
  std::call_once(stop_once_, [this] {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
      auto ids = std::move(connected_);
      connected_.clear();
      ids.insert(blocked_.begin(), blocked_.end());
      for (const auto& item : pending_)
        if (!item.IsReset()) ids.insert(item.id);
      pending_.clear();
      pending_counts_.clear();
      retries_.clear();
      for (const auto& id : ids) {
        pending_.push_back({id, std::nullopt, Clock::now()});
        pending_counts_[id] = 1;
      }
    }
    ready_.notify_one();
    worker_.join();
  });
}

bool RemoteInputDispatcher::IsMove(const RemoteAction& action) {
  return action.type == ControlType::mouse && action.m.flag == MouseFlag::move;
}

unsigned RemoteInputDispatcher::MouseUpMask(const RemoteAction& action) {
  if (action.type != ControlType::mouse) return 0;
  switch (action.m.flag) {
    case MouseFlag::left_up:
      return 1;
    case MouseFlag::right_up:
      return 2;
    case MouseFlag::middle_up:
      return 4;
    default:
      return 0;
  }
}

void RemoteInputDispatcher::RemoveResetsLocked() {
  pending_.erase(
      std::remove_if(pending_.begin(), pending_.end(),
                     [](const Item& item) { return item.IsReset(); }),
      pending_.end());
  if (pending_.empty() && !executing_) idle_.notify_all();
}

void RemoteInputDispatcher::QueueReleaseLocked(const std::string& id) {
  pending_.erase(
      std::remove_if(pending_.begin(), pending_.end(),
                     [&](const Item& item) { return item.id == id; }),
      pending_.end());
  pending_counts_.erase(id);
  blocked_.insert(id);
  // Repeated overflow/disconnect must not defeat the retry backoff.
  if (auto retry = retries_.find(id); retry != retries_.end()) {
    retry->second.mouse_mask = 0;  // Escalate a button repair on disconnect.
  } else {
    pending_.push_back({id, std::nullopt, Clock::now()});
    pending_counts_[id] = 1;
  }
  ready_.notify_one();
}

void RemoteInputDispatcher::QueueMouseReleaseLocked(const std::string& id,
                                                    unsigned mask) {
  // A concurrent disconnect/overflow already requested the broader cleanup.
  if (!blocked_.insert(id).second) return;
  // Keep subsequent clicks/keys queued, but don't apply them until the old
  // button is confirmed up. Never replay a timed-out down/click operation.
  pending_.push_front(
      {id, std::nullopt, Clock::now(), ItemKind::cleanup, mask});
  ++pending_counts_[id];
  ready_.notify_one();
}

std::optional<RemoteInputDispatcher::Item> RemoteInputDispatcher::NextLocked() {
  const auto now = Clock::now();
  auto next = retries_.end();
  for (auto it = retries_.begin(); it != retries_.end(); ++it)
    if (next == retries_.end() || it->second.due < next->second.due) next = it;
  auto it =
      std::find_if(pending_.begin(), pending_.end(), [&](const Item& item) {
        return !item.IsInput() || !blocked_.count(item.id);
      });
  // Alternate due retries with runnable queued work. Multiple slow cleanup
  // calls must not monopolize this worker; continuous input must not starve
  // recovery either. Input for the recovering controller remains blocked.
  if (next != retries_.end() && next->second.due <= now &&
      (it == pending_.end() || !last_was_retry_)) {
    last_was_retry_ = true;
    return Item{next->first, std::nullopt, next->second.due, ItemKind::cleanup,
                next->second.mouse_mask};
  }
  if (it == pending_.end()) return std::nullopt;
  last_was_retry_ = false;
  Item item = std::move(*it);
  pending_.erase(it);
  if (!item.IsReset() && --pending_counts_.at(item.id) == 0)
    pending_counts_.erase(item.id);
  return item;
}

bool RemoteInputDispatcher::ShouldReportLocked(const Item& item,
                                               Clock::time_point now,
                                               int64_t queued_ms,
                                               int64_t execution_ms,
                                               InputOutcome outcome) {
  if (!report_) return false;
  auto& state = logs_[item.id];
  auto& last = outcome == InputOutcome::failed
                   ? state.failure[item.IsInput() ? 0 : 1]
                   : state.slow;
  if (outcome != InputOutcome::failed && queued_ms < 50 && execution_ms < 50)
    return false;
  if (last != Clock::time_point{} && now - last < std::chrono::seconds(2))
    return false;
  last = now;
  return true;
}

void RemoteInputDispatcher::RemoveQueuedCleanupLocked(const std::string& id) {
  // Cleanup queued during the running call covers the same old state. Merge
  // duplicates so they cannot bypass backoff or release a later session twice.
  const size_t before = pending_.size();
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                [&](const Item& queued) {
                                  return queued.id == id && queued.IsCleanup();
                                }),
                 pending_.end());
  const size_t removed = before - pending_.size();
  if (removed && (pending_counts_.at(id) -= removed) == 0)
    pending_counts_.erase(id);
}

void RemoteInputDispatcher::ScheduleRetryLocked(const std::string& id,
                                                unsigned mouse_mask,
                                                Clock::time_point finished) {
  auto [retry, inserted] = retries_.try_emplace(id);
  retry->second.mouse_mask = mouse_mask;
  if (!inserted)
    retry->second.delay =
        std::min(retry->second.delay * 2, std::chrono::milliseconds(1000));
  retry->second.due = finished + retry->second.delay;
  blocked_.insert(id);
}

void RemoteInputDispatcher::CompleteCleanupLocked(const Item& item,
                                                  InputOutcome outcome,
                                                  Clock::time_point finished) {
  if (stopping_) {
    retries_.erase(item.id);
    blocked_.erase(item.id);
    return;
  }
  // A successful button repair cannot discharge a concurrent request to
  // release all keyboard/mouse state after disconnect or queue overflow.
  const bool queued_full_cleanup =
      item.mouse_mask &&
      std::any_of(pending_.begin(), pending_.end(), [&](const Item& queued) {
        return queued.id == item.id && queued.IsCleanup() && !queued.mouse_mask;
      });
  if (queued_full_cleanup) {
    retries_.erase(item.id);  // The queued full cleanup owns recovery now.
    return;
  }
  auto retry = retries_.find(item.id);
  if (item.mouse_mask && retry != retries_.end() && !retry->second.mouse_mask) {
    if (outcome == InputOutcome::failed)
      ScheduleRetryLocked(item.id, 0, finished);
    else
      retry->second.due = finished;
    return;
  }
  RemoveQueuedCleanupLocked(item.id);
  if (outcome == InputOutcome::failed) {
    ScheduleRetryLocked(item.id, item.mouse_mask, finished);
  } else {
    retries_.erase(item.id);
    blocked_.erase(item.id);
  }
}

InputOutcome RemoteInputDispatcher::Execute(const Item& item) {
  if (item.IsReset()) return reset_display_();
  if (item.IsInput()) return apply_(item.id, *item.action);
  if (item.mouse_mask && release_mouse_)
    return release_mouse_(item.id, item.mouse_mask);
  return release_(item.id);
}

void RemoteInputDispatcher::Run() {
  std::unique_lock lock(mutex_);
  for (;;) {
    auto next = NextLocked();
    if (!next) {
      if (stopping_) return;
      auto due = Clock::time_point::max();
      for (const auto& [_, retry] : retries_) due = std::min(due, retry.due);
      if (due == Clock::time_point::max())
        ready_.wait(lock);
      else
        ready_.wait_until(lock, due);
      continue;
    }
    Item item = std::move(*next);
    executing_ = true;
    lock.unlock();
    const auto started = Clock::now();
    InputOutcome outcome = InputOutcome::failed;
    bool threw = false;
    try {
      outcome = Execute(item);
    } catch (...) {
      threw = true;
    }
    const auto finished = Clock::now();
    const auto queued_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(started -
                                                              item.queued_at)
            .count();
    const auto execution_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(finished -
                                                              started)
            .count();
    lock.lock();
    if (item.IsCleanup()) {
      CompleteCleanupLocked(item, outcome, finished);
    } else if (item.IsInput() && !stopping_) {
      if (threw && connected_.count(item.id)) {
        QueueReleaseLocked(item.id);
      } else if (outcome == InputOutcome::failed) {
        if (const unsigned mask = MouseUpMask(*item.action))
          QueueMouseReleaseLocked(item.id, mask);
      }
    }
    const bool report =
        ShouldReportLocked(item, finished, queued_ms, execution_ms, outcome);
    if (!connected_.count(item.id) && !blocked_.count(item.id) &&
        !pending_counts_.count(item.id))
      logs_.erase(item.id);
    lock.unlock();
    if (item.completion) {
      try { item.completion(outcome); } catch (...) {}
    }
    if (report) {
      try {
        report_(item.id, queued_ms, execution_ms, outcome);
      } catch (...) {
      }  // Diagnostics must not terminate the input worker.
    }
    lock.lock();
    executing_ = false;
    if (pending_.empty()) idle_.notify_all();
  }
}

}  // namespace crossdesk
