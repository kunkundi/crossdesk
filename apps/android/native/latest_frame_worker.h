#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace crossdesk::android_controller {
// Rendering may wait for Surface buffers. Never hold up the decoder or build
// a FIFO of obsolete pictures: keep at most one pending decoded frame.
template <typename Frame>
class LatestFrameWorker {
 public:
  explicit LatestFrameWorker(std::function<void(Frame)> render)
      : render_(std::move(render)), thread_([this] { Run(); }) {}
  ~LatestFrameWorker() { Stop(); }
  LatestFrameWorker(const LatestFrameWorker&) = delete;
  LatestFrameWorker& operator=(const LatestFrameWorker&) = delete;

  bool Submit(Frame frame) {
    std::lock_guard lock(mutex_);
    if (stopped_) return false;
    pending_ = std::move(frame);
    changed_.notify_one();
    return true;
  }
  void Clear() {
    std::lock_guard lock(mutex_);
    pending_.reset();
  }
  // Called by the session owner, before destroying the peer or Surface.
  void Stop() {
    {
      std::lock_guard lock(mutex_);
      stopped_ = true;
      pending_.reset();
    }
    changed_.notify_one();
    if (thread_.joinable()) thread_.join();
  }

 private:
  void Run() {
    for (;;) {
      std::unique_lock lock(mutex_);
      changed_.wait(lock, [this] { return stopped_ || pending_.has_value(); });
      if (stopped_) return;
      Frame frame = std::move(*pending_);
      pending_.reset();
      lock.unlock();
      render_(std::move(frame));
    }
  }
  const std::function<void(Frame)> render_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::optional<Frame> pending_;
  bool stopped_ = false;
  std::thread thread_;
};
}  // namespace crossdesk::android_controller
