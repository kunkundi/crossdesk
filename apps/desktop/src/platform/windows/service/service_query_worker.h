/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-08
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SERVICE_QUERY_WORKER_H_
#define _SERVICE_QUERY_WORKER_H_

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace crossdesk {

struct ServiceQueryResult {
  std::string command;
  std::string response;
  bool user_desktop_recovered = false;
};

// One bounded query at a time. Reset invalidates in-flight results without
// joining the worker on the UI thread; only destruction waits for completion.
class ServiceQueryWorker {
 public:
  using Query = std::function<ServiceQueryResult(const std::string&)>;
  explicit ServiceQueryWorker(Query query)
      : thread_([this, query = std::move(query)] {
          std::unique_lock lock(mutex_);
          while (true) {
            wake_.wait(lock, [this] {
              return stopping_ ||
                     (!result_ && (sas_ || cancel_consent_ || status_));
            });
            if (stopping_) return;
            std::string command = "status";
            if (sas_) {
              sas_ = false;
              command = "sas";
            } else if (cancel_consent_) {
              cancel_consent_ = false;
              command = "cancel-consent";
            } else {
              status_ = false;
            }
            const uint64_t generation = generation_;
            running_command_ = command;
            running_generation_ = generation;
            lock.unlock();
            ServiceQueryResult result;
            try {
              result = query(command);
            } catch (...) {
              result.response = R"({"ok":false,"error":"service_query_failed"})";
            }
            result.command = command;
            lock.lock();
            running_command_.clear();
            if (generation == generation_) result_ = std::move(result);
          }
        }) {}

  ~ServiceQueryWorker() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    wake_.notify_one();
    thread_.join();
  }
  ServiceQueryWorker(const ServiceQueryWorker&) = delete;
  ServiceQueryWorker& operator=(const ServiceQueryWorker&) = delete;

  void RequestStatus() {
    std::lock_guard lock(mutex_);
    if ((running_command_ == "status" && running_generation_ == generation_) ||
        result_ || sas_ || cancel_consent_) return;
    status_ = true;
    wake_.notify_one();
  }

  void RequestCancelConsent() {
    std::lock_guard lock(mutex_);
    // Cancelling consent changes the interactive stage, so a status result
    // captured before the cancel must not be published afterwards.
    ++generation_;
    status_ = false;
    result_.reset();
    cancel_consent_ = true;
    wake_.notify_one();
  }

  void RequestSas() {
    std::lock_guard lock(mutex_);
    // A pre-SAS status must not overwrite the optimistic secure-desktop state.
    ++generation_;
    status_ = false;
    result_.reset();
    sas_ = true;
    wake_.notify_one();
  }

  std::optional<ServiceQueryResult> Take() {
    std::lock_guard lock(mutex_);
    auto result = std::exchange(result_, std::nullopt);
    wake_.notify_one();
    return result;
  }

  void Reset() {
    std::lock_guard lock(mutex_);
    ++generation_;
    status_ = sas_ = cancel_consent_ = false;
    result_.reset();
  }

 private:
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_ = false, status_ = false, sas_ = false,
       cancel_consent_ = false;
  uint64_t generation_ = 0, running_generation_ = 0;
  std::string running_command_;
  std::optional<ServiceQueryResult> result_;
  std::thread thread_;
};

}  // namespace crossdesk
#endif  // _SERVICE_QUERY_WORKER_H_
