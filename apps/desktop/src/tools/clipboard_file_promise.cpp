#include "clipboard_file_promise.h"

namespace crossdesk {

ClipboardFilePromise::Files::~Files() {
  std::error_code ec;
  if (!directory.empty()) std::filesystem::remove_all(directory, ec);
}

void ClipboardFilePromise::SetRequest(std::function<void()> request) {
  std::lock_guard lock(mutex_);
  if (!cancelled_ && !requested_) request_ = std::move(request);
}

bool ClipboardFilePromise::Request() {
  std::function<void()> request;
  {
    std::lock_guard lock(mutex_);
    if (cancelled_) return false;
    if (requested_) return true;
    if (!request_) return false;
    requested_ = true;
    request.swap(request_);
  }
  request();
  return valid();
}

std::shared_ptr<ClipboardFilePromise::Files> ClipboardFilePromise::Wait() {
  if (!Request()) return {};
  std::unique_lock lock(mutex_);
  wake_.wait(lock, [this] { return cancelled_ || bool(files_); });
  return cancelled_ ? nullptr : files_;
}

void ClipboardFilePromise::Resolve(std::shared_ptr<Files> files) {
  {
    std::lock_guard lock(mutex_);
    if (cancelled_ || !requested_) return;
    files_ = std::move(files);
  }
  wake_.notify_all();
}

void ClipboardFilePromise::Cancel() {
  {
    std::lock_guard lock(mutex_);
    cancelled_ = true;
    request_ = {};
    files_.reset();
  }
  wake_.notify_all();
}

void ClipboardFilePromise::Fail() {
  {
    std::lock_guard lock(mutex_);
    failed_ = true;
  }
  Cancel();
}

bool ClipboardFilePromise::failed() const {
  std::lock_guard lock(mutex_);
  return failed_;
}

bool ClipboardFilePromise::valid() const {
  std::lock_guard lock(mutex_);
  return !cancelled_;
}

}  // namespace crossdesk
