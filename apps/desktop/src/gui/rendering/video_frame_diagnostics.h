/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-15
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VIDEO_FRAME_DIAGNOSTICS_H_
#define _VIDEO_FRAME_DIAGNOSTICS_H_

#include <optional>
#include <string>

#include "rd_log.h"
#include "rendering/video_latency.h"

namespace crossdesk {

inline void LogVideoFrameDiagnostics(
    const std::string& remote_id,
    const std::optional<VideoLatencyStats::DiagnosticsSnapshot>& snapshot) {
  if (!snapshot) return;
  const auto& value = *snapshot;
  for (size_t i = 0; i < value.gaps.size(); ++i) {
    const auto& gap = value.gaps[i];
    if (gap.intervals == 0 && gap.ongoing_gap_ms == 0) continue;
    LOG_DEBUG("Video frame gaps: remote={} endpoint={} threshold_ms={} "
              "intervals={} interval_avg_ms={} interval_max_ms={} "
              "frame_gap_count={} frame_gap_total_ms={} frame_gap_max_ms={} "
              "ongoing_gap_ms={} window_ms={} final={} display={}",
              remote_id, i == 0 ? "presented" : "estimated",
              VideoLatencyStats::kFrameGapThresholdMs, gap.intervals,
              gap.intervals ? gap.interval_total_ms / gap.intervals : 0,
              gap.interval_max_ms, gap.gap_count, gap.gap_total_ms,
              gap.gap_max_ms, gap.ongoing_gap_ms, value.window_ms, value.final,
              value.stream_index);
  }
  const auto& pipeline = value.pipeline;
  const auto count = [&](VideoFrameStage stage) {
    return pipeline.frames[static_cast<size_t>(stage)];
  };
  bool has_frames = false;
  for (auto frames : pipeline.frames) has_frames |= frames != 0;
  if (!has_frames && pipeline.render_ticks == 0) return;
  // Stages describe events in this window, so a frame may cross a boundary.
  // Native GPU completions and actual presentations remain separate counts.
  // Drawable waits cover committed frames only, excluding nil/idle outcomes.
  LOG_DEBUG("Video frame pipeline: remote={} display={} window_ms={} final={} "
            "received={} accepted={} replaced={} busy={} committed={} "
            "gpu_completed={} presented={} render_ticks={} "
            "timer_late_avg_ms={} timer_late_max_ms={} "
            "receive_to_commit_avg_ms={} receive_to_commit_max_ms={} "
            "committed_drawable_wait_avg_ms={} committed_drawable_wait_max_ms={}",
            remote_id, value.stream_index, value.window_ms, value.final,
            count(VideoFrameStage::received), count(VideoFrameStage::accepted),
            count(VideoFrameStage::replaced), count(VideoFrameStage::busy),
            count(VideoFrameStage::committed),
            count(VideoFrameStage::gpu_completed), count(VideoFrameStage::presented),
            pipeline.render_ticks,
            pipeline.render_ticks ? pipeline.timer_late_total_ms / pipeline.render_ticks : 0,
            pipeline.timer_late_max_ms,
            pipeline.commit_timing_samples
                ? pipeline.receive_to_commit_total_ms / pipeline.commit_timing_samples : 0,
            pipeline.receive_to_commit_max_ms,
            count(VideoFrameStage::committed)
                ? pipeline.drawable_wait_total_ms / count(VideoFrameStage::committed) : 0,
            pipeline.drawable_wait_max_ms);
}

}  // namespace crossdesk

#endif
