/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-08
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SHARED_CAPTURE_LIFECYCLE_H_
#define _SHARED_CAPTURE_LIFECYCLE_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace crossdesk {

// Serializes complete control operations, including joining the old producer.
// The producer never takes control_mutex_: resources are immutable from its
// launch until its join, and cleanup always runs after the join.
class SharedCaptureLifecycle {
  // Preserve admission order while a slow join holds up several retries. A
  // queued stop must not be overtaken by a new start and then stop that
  // producer.
  class ControlOperation {
   public:
    explicit ControlOperation(SharedCaptureLifecycle& owner)
        : owner_(owner),
          ticket_(owner.next_ticket_.fetch_add(1)),
          lock_(owner.control_mutex_) {
      owner_.control_ready_.wait(
          lock_, [&] { return owner_.serving_ticket_ == ticket_; });
    }
    ~ControlOperation() {
      ++owner_.serving_ticket_;
      lock_.unlock();
      owner_.control_ready_.notify_all();
    }

   private:
    SharedCaptureLifecycle& owner_;
    uint64_t ticket_;
    std::unique_lock<std::mutex> lock_;
  };

 public:
  enum class StartResult { Started, Failed, ShuttingDown };
  SharedCaptureLifecycle() = default;
  ~SharedCaptureLifecycle() { Shutdown(); }
  SharedCaptureLifecycle(const SharedCaptureLifecycle&) = delete;
  SharedCaptureLifecycle& operator=(const SharedCaptureLifecycle&) = delete;

  StartResult Start(std::function<bool()> prepare,
                    std::function<void()> capture,
                    std::function<void()> cleanup,
                    std::function<bool()> reuse = {}) {
    if (shutting_down_.load()) return StartResult::ShuttingDown;
    ControlOperation operation(*this);
    if (shutting_down_.load()) return StartResult::ShuttingDown;
    if (!producer_finished_.load() && reuse && reuse())
      return StartResult::Started;
    StopLocked();
    cleanup_ = std::move(cleanup);
    try {
      if (prepare()) {
        stop_requested_.store(false, std::memory_order_relaxed);
        producer_finished_.store(false);
        producer_ = std::thread([this, capture = std::move(capture)] {
          capture();
          producer_finished_.store(true);
        });
        return StartResult::Started;
      }
    } catch (...) {
      // Failed allocation/thread creation must leave the prepared handles
      // owned by cleanup, not leak them from an abandoned start request.
    }
    StopLocked();
    return StartResult::Failed;
  }

  void Stop() {
    ControlOperation operation(*this);
    StopLocked();
  }

  void Shutdown() {
    // Publish before waiting for the lock. Detached IPC clients that are
    // already queued cannot start another producer after server shutdown.
    shutting_down_.store(true);
    Stop();
  }

  bool StopRequested() const {
    return stop_requested_.load(std::memory_order_relaxed);
  }

 private:
  void StopLocked() {
    stop_requested_.store(true, std::memory_order_relaxed);
    if (producer_.joinable()) producer_.join();
    producer_finished_.store(true);
    if (cleanup_) cleanup_();
    cleanup_ = {};
  }

  std::mutex control_mutex_;
  std::condition_variable control_ready_;
  std::atomic<uint64_t> next_ticket_{0};
  uint64_t serving_ticket_ = 0;
  std::atomic<bool> shutting_down_{false};
  std::atomic<bool> stop_requested_{true};
  std::atomic<bool> producer_finished_{true};
  std::thread producer_;
  std::function<void()> cleanup_;
};

}  // namespace crossdesk
#endif  // _SHARED_CAPTURE_LIFECYCLE_H_
