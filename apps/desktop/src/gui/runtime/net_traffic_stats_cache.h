/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-26
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _NET_TRAFFIC_STATS_CACHE_H_
#define _NET_TRAFFIC_STATS_CACHE_H_

#include <atomic>

#include "minirtc.h"

namespace crossdesk::gui_detail {

// Fields are independent observations and may come from different reports.
// Relaxed accesses keep each field safe without publishing any other state.
class NetTrafficStatsCache {
  static_assert(std::atomic<uint32_t>::is_always_lock_free &&
                    std::atomic<float>::is_always_lock_free &&
                    std::atomic<double>::is_always_lock_free &&
                    std::atomic<bool>::is_always_lock_free,
                "Network statistics require lock-free scalar atomics");

 public:
  void Store(const MiniRtcNetTrafficStats& stats) {
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
  }

  MiniRtcNetTrafficStats Load() const {
    MiniRtcNetTrafficStats stats{};
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

  void Reset() { Store(MiniRtcNetTrafficStats{}); }

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
};

}  // namespace crossdesk::gui_detail

#endif
