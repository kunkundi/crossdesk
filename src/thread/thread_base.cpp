#include "thread_base.h"

#include "log.h"

ThreadBase::ThreadBase()
    : running_(false),
      pause_(false),
      period_(std::chrono::milliseconds(100)),
      thread_name_("UnnamedThread") {}

ThreadBase::~ThreadBase() { Stop(); }

void ThreadBase::Start() {
  {
    std::lock_guard<std::mutex> lock(cv_mtx_);
    if (running_) {
      return;
    }
    running_ = true;
  }

  std::thread t(&ThreadBase::Run, this);
  thread_ = std::move(t);
}

void ThreadBase::Stop() {
  {
    std::lock_guard<std::mutex> lock(cv_mtx_);
    if (!running_) {
      return;
    }
    running_ = false;
  }

  cv_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ThreadBase::Pause() { pause_ = true; }

void ThreadBase::Resume() { pause_ = false; }

void ThreadBase::SetPeriod(std::chrono::milliseconds period) {
  std::lock_guard<std::mutex> lock(cv_mtx_);
  period_ = period;
}

void ThreadBase::SetThreadName(const std::string& name) {
  std::lock_guard<std::mutex> lock(cv_mtx_);
  thread_name_ = name;
}

std::string ThreadBase::GetThreadName() {
  std::lock_guard<std::mutex> lock(cv_mtx_);
  return thread_name_;
}

void ThreadBase::Run() {
  while (running_) {
    std::unique_lock<std::mutex> lock(cv_mtx_);
    cv_.wait_for(lock, period_, [this] { return !running_; });
    if (running_) {
      Process();
    }
  }
}