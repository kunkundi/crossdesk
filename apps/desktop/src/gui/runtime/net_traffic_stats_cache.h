/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-26
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _NET_TRAFFIC_STATS_CACHE_H_
#define _NET_TRAFFIC_STATS_CACHE_H_

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

#include "minirtc.h"
#include "runtime/connection_latency.h"

namespace crossdesk::gui_detail {

// Fields are independent observations and may come from different reports.
// Relaxed accesses keep scalar fields safe without publishing other state;
// codec names and the selected endpoint pair are copied under one mutex.
class NetTrafficStatsCache {
  static_assert(std::atomic<uint32_t>::is_always_lock_free &&
                    std::atomic<float>::is_always_lock_free &&
                    std::atomic<double>::is_always_lock_free &&
                    std::atomic<bool>::is_always_lock_free,
                "Network statistics require lock-free scalar atomics");

 public:
  using Clock = std::chrono::steady_clock;

  struct VideoCodecNames {
    std::string remote_encoder;
    std::string local_decoder;
  };

  void StoreRemoteVideoEncoder(const std::string& encoder) {
    std::lock_guard lock(text_mutex_);
    codecs_.remote_encoder = encoder;
  }

  VideoCodecNames LoadVideoCodecs() const {
    std::lock_guard lock(text_mutex_);
    return codecs_;
  }

  void Store(const MiniRtcNetTrafficStats& stats) {
    {
      std::lock_guard lock(text_mutex_);
      local_endpoint_.assign(stats.local_endpoint,
          strnlen(stats.local_endpoint, sizeof(stats.local_endpoint)));
      remote_endpoint_.assign(stats.remote_endpoint,
          strnlen(stats.remote_endpoint, sizeof(stats.remote_endpoint)));
      local_encoder_.assign(
          stats.video_encoder,
          strnlen(stats.video_encoder, sizeof(stats.video_encoder)));
      codecs_.local_decoder.assign(
          stats.video_decoder,
          strnlen(stats.video_decoder, sizeof(stats.video_decoder)));
    }
    video_inbound_stats_.Store(stats.video_inbound_stats);
    video_outbound_stats_.Store(stats.video_outbound_stats);
    audio_inbound_stats_.Store(stats.audio_inbound_stats);
    audio_outbound_stats_.Store(stats.audio_outbound_stats);
    data_inbound_stats_.Store(stats.data_inbound_stats);
    data_outbound_stats_.Store(stats.data_outbound_stats);
    total_inbound_stats_.Store(stats.total_inbound_stats);
    total_outbound_stats_.Store(stats.total_outbound_stats);
    srtp_active_.store(stats.srtp_active, std::memory_order_relaxed);
    rtt_ms_.store(stats.rtt_ms, std::memory_order_relaxed);
    connection_latency_.Update(stats.rtt_ms);
    last_report_ms_.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch()).count(),
        std::memory_order_relaxed);
  }

  MiniRtcNetTrafficStats Load() const {
    MiniRtcNetTrafficStats stats{};
    {
      std::lock_guard lock(text_mutex_);
      local_endpoint_.copy(stats.local_endpoint, sizeof(stats.local_endpoint) - 1);
      remote_endpoint_.copy(stats.remote_endpoint, sizeof(stats.remote_endpoint) - 1);
      local_encoder_.copy(stats.video_encoder, sizeof(stats.video_encoder) - 1);
      codecs_.local_decoder.copy(stats.video_decoder, sizeof(stats.video_decoder) - 1);
    }
    stats.video_inbound_stats = video_inbound_stats_.Load();
    stats.video_outbound_stats = video_outbound_stats_.Load();
    stats.audio_inbound_stats = audio_inbound_stats_.Load();
    stats.audio_outbound_stats = audio_outbound_stats_.Load();
    stats.data_inbound_stats = data_inbound_stats_.Load();
    stats.data_outbound_stats = data_outbound_stats_.Load();
    stats.total_inbound_stats = total_inbound_stats_.Load();
    stats.total_outbound_stats = total_outbound_stats_.Load();
    stats.srtp_active = srtp_active_.load(std::memory_order_relaxed);
    stats.rtt_ms = rtt_ms_.load(std::memory_order_relaxed);
    return stats;
  }

  uint32_t DataOutboundBitrate() const {
    return data_outbound_stats_.bitrate.load(std::memory_order_relaxed);
  }

  std::optional<double> SmoothedRttMs(
      ConnectionLatencyStats::Clock::time_point now =
          ConnectionLatencyStats::Clock::now()) const {
    return connection_latency_.Get(now);
  }

  void ResetLatency() { connection_latency_.Reset(); }

  bool HasRecentReport(Clock::time_point now = Clock::now()) const {
    const auto last_report = last_report_ms_.load(std::memory_order_relaxed);
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()).count();
    return last_report > 0 && now_ms - last_report < 3000;
  }

  void Reset() {
    Store(MiniRtcNetTrafficStats{});
    {
      std::lock_guard lock(text_mutex_);
      codecs_ = {};
    }
    ResetLatency();
    last_report_ms_.store(0, std::memory_order_relaxed);
  }

 private:
  struct InboundStats {
    void Store(const MiniRtcInboundStats& stats) {
      bitrate.store(stats.bitrate, std::memory_order_relaxed);
      rtp_packet_count.store(stats.rtp_packet_count, std::memory_order_relaxed);
      loss_rate.store(stats.loss_rate, std::memory_order_relaxed);
    }

    MiniRtcInboundStats Load() const {
      return {bitrate.load(std::memory_order_relaxed),
              rtp_packet_count.load(std::memory_order_relaxed),
              loss_rate.load(std::memory_order_relaxed)};
    }

    std::atomic<uint32_t> bitrate{0};
    std::atomic<uint32_t> rtp_packet_count{0};
    std::atomic<float> loss_rate{0};
  };

  struct OutboundStats {
    void Store(const MiniRtcOutboundStats& stats) {
      bitrate.store(stats.bitrate, std::memory_order_relaxed);
      rtp_packet_count.store(stats.rtp_packet_count, std::memory_order_relaxed);
    }

    MiniRtcOutboundStats Load() const {
      return {bitrate.load(std::memory_order_relaxed),
              rtp_packet_count.load(std::memory_order_relaxed)};
    }

    std::atomic<uint32_t> bitrate{0};
    std::atomic<uint32_t> rtp_packet_count{0};
  };

  InboundStats video_inbound_stats_;
  OutboundStats video_outbound_stats_;
  InboundStats audio_inbound_stats_;
  OutboundStats audio_outbound_stats_;
  InboundStats data_inbound_stats_;
  OutboundStats data_outbound_stats_;
  InboundStats total_inbound_stats_;
  OutboundStats total_outbound_stats_;
  std::atomic<bool> srtp_active_{false};
  std::atomic<double> rtt_ms_{-1};
  std::atomic<int64_t> last_report_ms_{0};
  ConnectionLatencyStats connection_latency_;
  mutable std::mutex text_mutex_;
  std::string local_encoder_;
  std::string local_endpoint_;
  std::string remote_endpoint_;
  VideoCodecNames codecs_;
};

}  // namespace crossdesk::gui_detail

#endif
