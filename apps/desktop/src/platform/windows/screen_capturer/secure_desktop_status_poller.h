/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-07
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SECURE_DESKTOP_STATUS_POLLER_H_
#define _SECURE_DESKTOP_STATUS_POLLER_H_

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace crossdesk {

struct SecureDesktopServiceStatus {
  bool service_available = false;
  bool capture_active = false;
  bool helper_running = false;
  uint32_t helper_process_id = 0;
  uint32_t active_session_id = 0xFFFFFFFF;
  uint32_t error_code = 0;
  std::string interactive_stage;
  std::string interactive_desktop;
  std::string error;

  void UpdateFrom(SecureDesktopServiceStatus latest) {
    if (latest.service_available) {
      *this = std::move(latest);
    } else {
      // Failed IPC says nothing about the desktop. Retain the last confirmed
      // capture state until a valid response reports a change or an unlock.
      service_available = false;
      error = std::move(latest.error);
      error_code = latest.error_code;
    }
  }
};

// The query runs without holding the snapshot mutex. Slow service/helper IPC
// must not stop delivery of frames that are already available in shared memory.
class SecureDesktopStatusPoller {
 public:
  using Query = std::function<SecureDesktopServiceStatus()>;

  struct Sample {
    SecureDesktopServiceStatus status;
    int64_t query_ms = 0;
  };

  explicit SecureDesktopStatusPoller(Query query,
                                     std::chrono::milliseconds interval)
      : thread_([this, query = std::move(query), interval]() {
          std::unique_lock<std::mutex> lock(mutex_);
          while (!stopping_) {
            lock.unlock();
            const auto started = std::chrono::steady_clock::now();
            auto latest = query();
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started);
            lock.lock();
            sample_.status.UpdateFrom(std::move(latest));
            sample_.query_ms = elapsed.count();
            pending_ = true;
            // Even a failed/slow query gets a pause, avoiding a retry storm.
            wake_.wait_for(lock, interval, [this] { return stopping_; });
          }
        }) {}

  ~SecureDesktopStatusPoller() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    wake_.notify_one();
    thread_.join();
  }

  SecureDesktopStatusPoller(const SecureDesktopStatusPoller&) = delete;
  SecureDesktopStatusPoller& operator=(const SecureDesktopStatusPoller&) =
      delete;

  std::optional<Sample> Take() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pending_) {
      return std::nullopt;
    }
    pending_ = false;
    return sample_;
  }

 private:
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_ = false;
  bool pending_ = false;
  Sample sample_;
  std::thread thread_;
};

}  // namespace crossdesk

#endif