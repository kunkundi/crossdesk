/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-26
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VIDEO_LATENCY_H_
#define _VIDEO_LATENCY_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

namespace crossdesk {

class VideoLatencyStats;

// Travels with the exact queued pixels, including native frames. Its clock is
// translated once at the receive callback; rendering needs no live PeerPtr.
struct VideoLatencyFrame {
  using Clock = std::chrono::steady_clock;
  std::shared_ptr<VideoLatencyStats> stats;
  uint64_t id = 0;
  Clock::time_point capture_time{};
  void MarkPresented(Clock::time_point now = Clock::now()) const;
  // Fallback for renderers which expose submission but no presentation time.
  void MarkSubmitted(Clock::time_point now = Clock::now()) const;
};

class VideoLatencyStats
    : public std::enable_shared_from_this<VideoLatencyStats> {
 public:
  using Clock = VideoLatencyFrame::Clock;
  struct Snapshot {
    double average_ms;
    size_t samples;
  };

  VideoLatencyFrame Frame(uint64_t captured_us, int64_t local_now_us,
                          Clock::time_point now = Clock::now()) {
    if (captured_us == 0 || local_now_us <= 0 ||
        captured_us > uint64_t(local_now_us) ||
        uint64_t(local_now_us) - captured_us > 5'000'000)
      return {};
    std::lock_guard lock(mutex_);
    return {shared_from_this(), ++issued_id_,
            now - std::chrono::microseconds(local_now_us - captured_us)};
  }

  void Record(const VideoLatencyFrame& frame, Clock::time_point now) {
    std::lock_guard lock(mutex_);
    // Redraws, cached snapshots and late completions must not count twice.
    if (frame.id <= last_id_) return;
    last_id_ = frame.id;
    const double ms =
        std::chrono::duration<double, std::milli>(now - frame.capture_time)
            .count();
    if (ms < 0 || ms > 5000) return;
    samples_.push_back({now, ms});
    Prune(now);
  }

  std::optional<Snapshot> Get(Clock::time_point now = Clock::now()) {
    std::lock_guard lock(mutex_);
    Prune(now);
    if (samples_.empty()) return std::nullopt;
    double sum = 0;
    for (const auto& sample : samples_) {
      sum += sample.ms;
    }
    return Snapshot{sum / samples_.size(), samples_.size()};
  }

  void Reset() {
    std::lock_guard lock(mutex_);
    samples_.clear();
    last_id_ = issued_id_;  // Invalidate frames still held by a renderer.
  }

 private:
  void Prune(Clock::time_point now) {
    while (!samples_.empty() &&
           (now - samples_.front().time > std::chrono::seconds(1) ||
            samples_.size() > 240))
      samples_.pop_front();
  }
  struct Sample {
    Clock::time_point time;
    double ms;
  };
  std::mutex mutex_;
  uint64_t issued_id_ = 0;
  uint64_t last_id_ = 0;
  std::deque<Sample> samples_;
};

inline void VideoLatencyFrame::MarkSubmitted(Clock::time_point now) const {
  MarkPresented(now);
}

inline void VideoLatencyFrame::MarkPresented(Clock::time_point now) const {
  if (stats) stats->Record(*this, now);
}
}  // namespace crossdesk

#endif