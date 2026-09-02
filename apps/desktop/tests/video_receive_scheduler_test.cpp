#include <cstdint>
#include <iostream>
#include <memory>

#include "video_receive_scheduler.h"

namespace {

std::unique_ptr<minirtc::ReceivedFrame>
MakeFrame(uint32_t rtp_timestamp, int64_t received_us, bool key_frame = false) {
  const uint8_t payload = key_frame ? 0x65 : 0x41;
  auto frame =
      std::make_unique<minirtc::ReceivedFrame>(&payload, sizeof(payload));
  frame->SetRtpTimestamp(rtp_timestamp);
  frame->SetReceivedTimestamp(received_us);
  frame->SetCapturedTimestamp(received_us - 5000);
  frame->SetFrameType(key_frame ? minirtc::kVideoFrameKey
                                : minirtc::kVideoFrameDelta);
  return frame;
}

bool Expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool TestOneFramePerPlaybackCycle() {
  minirtc::VideoReceiveScheduler scheduler(60);
  scheduler.Enqueue(MakeFrame(90000, 1000000), 1000000);
  scheduler.Enqueue(MakeFrame(91500, 1016667), 1016667);

  auto first = scheduler.Poll(1016667, false);
  bool ok = Expect(first.frame != nullptr, "first frame was not released");
  ok &= Expect(first.frame && first.frame->RtpTimestamp() == 90000,
               "scheduler did not preserve RTP order");

  auto same_cycle = scheduler.Poll(1016667, false);
  ok &= Expect(!same_cycle.frame,
               "scheduler released more than one frame in a cycle");
  ok &= Expect(same_cycle.next_delay_us > 0,
               "scheduler did not schedule the next playback cycle");

  auto second = scheduler.Poll(1033334, false);
  ok &= Expect(second.frame != nullptr,
               "second frame was not released on the next cycle");
  ok &= Expect(second.frame && second.frame->RtpTimestamp() == 91500,
               "second RTP timestamp was incorrect");
  return ok;
}

bool TestSevereLagJumpsToNewestKeyFrame() {
  minirtc::VideoReceiveScheduler scheduler(60);
  for (uint32_t i = 0; i < 10; ++i) {
    scheduler.Enqueue(MakeFrame(90000 + i * 1500, 1000000 + i * 16667, i == 8),
                      1000000 + i * 16667);
  }

  auto result = scheduler.Poll(1200000, false);
  const auto metrics = scheduler.GetMetrics(1200000);
  bool ok = Expect(result.frame && result.frame->IsKeyFrame(),
                   "catch-up did not jump to the newest key frame");
  ok &= Expect(result.dropped_frames == 8,
               "catch-up dropped an unexpected number of frames");
  ok &= Expect(metrics.catch_up_count == 1,
               "catch-up metric was not incremented");
  return ok;
}

bool TestSevereLagRequestsKeyFrameWhenNoSyncPointExists() {
  minirtc::VideoReceiveScheduler scheduler(60);
  for (uint32_t i = 0; i < 10; ++i) {
    scheduler.Enqueue(MakeFrame(180000 + i * 1500, 2000000 + i * 16667),
                      2000000 + i * 16667);
  }

  auto result = scheduler.Poll(2200000, false);
  const auto metrics = scheduler.GetMetrics(2200000);
  bool ok = Expect(!result.frame,
                   "undecodable delta frame was released during catch-up");
  ok &=
      Expect(result.request_key_frame, "catch-up did not request a key frame");
  ok &= Expect(metrics.buffered_frames == 0 && metrics.awaiting_key_frame,
               "scheduler did not enter key-frame wait state");
  return ok;
}

bool TestAdaptiveBufferStaysWithinOneToThreeFrames() {
  minirtc::VideoReceiveScheduler scheduler(60);
  int64_t arrival_us = 3000000;
  for (uint32_t i = 0; i < 40; ++i) {
    arrival_us += (i % 2 == 0) ? 5000 : 30000;
    scheduler.Enqueue(MakeFrame(270000 + i * 1500, arrival_us), arrival_us);
  }

  const auto metrics = scheduler.GetMetrics(arrival_us);
  bool ok = Expect(metrics.target_buffer_frames >= 1 &&
                       metrics.target_buffer_frames <= 3,
                   "adaptive target left the 1-3 frame range");
  ok &= Expect(metrics.target_buffer_frames > 1,
               "arrival jitter did not increase the target buffer");
  return ok;
}

bool TestLegacyMicrosecondTimestampFallsBackToArrivalCadence() {
  minirtc::VideoReceiveScheduler scheduler(60);
  int64_t arrival_us = 4000000;
  for (uint32_t i = 0; i < 20; ++i) {
    scheduler.Enqueue(MakeFrame(500000 + i * 16667, arrival_us), arrival_us);
    scheduler.Poll(arrival_us, false);
    arrival_us += 16667;
  }
  scheduler.Poll(arrival_us, false);

  const auto metrics = scheduler.GetMetrics(arrival_us);
  bool ok = Expect(metrics.estimated_frame_interval_us >= 15000 &&
                       metrics.estimated_frame_interval_us <= 20000,
                   "legacy RTP units slowed the playback interval");
  ok &= Expect(metrics.timestamp_fallbacks > 0,
               "legacy RTP units did not activate timestamp fallback");
  ok &= Expect(metrics.released_frames == 20 && metrics.dropped_frames == 0,
               "legacy RTP units caused scheduler frame loss");
  return ok;
}

bool TestSixtyFpsCadenceDoesNotTriggerCatchUp() {
  minirtc::VideoReceiveScheduler scheduler(60);
  constexpr int64_t kStartUs = 6000000;
  constexpr int64_t kFrameIntervalUs = 16667;
  constexpr uint32_t kStartRtpTimestamp = 700000;
  constexpr uint32_t kRtpFrameInterval = 1500;

  for (uint32_t i = 0; i < 60; ++i) {
    const int64_t now_us = kStartUs + static_cast<int64_t>(i) *
                                          kFrameIntervalUs;
    scheduler.Enqueue(
        MakeFrame(kStartRtpTimestamp + i * kRtpFrameInterval, now_us),
        now_us);
    scheduler.Poll(now_us, false);
  }
  scheduler.Poll(kStartUs + 60 * kFrameIntervalUs, false);

  const auto metrics =
      scheduler.GetMetrics(kStartUs + 60 * kFrameIntervalUs);
  bool ok = Expect(metrics.released_frames == 60,
                   "60 fps input was not released at 60 fps cadence");
  ok &= Expect(metrics.dropped_frames == 0 && metrics.catch_up_count == 0,
               "steady 60 fps input incorrectly triggered catch-up");
  ok &= Expect(metrics.estimated_frame_interval_us >= 15000 &&
                   metrics.estimated_frame_interval_us <= 18000,
               "steady 60 fps interval estimate drifted");
  return ok;
}

bool TestKeyFrameRequestCooldownKeepsDecodableProgress() {
  minirtc::VideoReceiveScheduler scheduler(60);
  for (uint32_t i = 0; i < 10; ++i) {
    scheduler.Enqueue(MakeFrame(600000 + i * 1500,
                                5000000 + static_cast<int64_t>(i) * 16667),
                      5000000 + static_cast<int64_t>(i) * 16667);
  }
  auto first_catch_up = scheduler.Poll(5200000, false);
  bool ok = Expect(first_catch_up.request_key_frame,
                   "initial severe lag did not request a key frame");

  scheduler.Enqueue(MakeFrame(615000, 5220000, true), 5220000);
  auto key_frame = scheduler.Poll(5240000, false);
  ok &= Expect(key_frame.frame && key_frame.frame->IsKeyFrame(),
               "requested key frame was not released");

  for (uint32_t i = 0; i < 10; ++i) {
    scheduler.Enqueue(MakeFrame(616500 + i * 1500,
                                5250000 + static_cast<int64_t>(i) * 16667),
                      5250000 + static_cast<int64_t>(i) * 16667);
  }
  auto cooldown_catch_up = scheduler.Poll(5450000, false);
  const auto metrics = scheduler.GetMetrics(5450000);
  ok &= Expect(!cooldown_catch_up.request_key_frame,
               "key frame request cooldown was ignored");
  ok &= Expect(cooldown_catch_up.frame != nullptr,
               "cooldown prevented forward playback progress");
  ok &= Expect(metrics.key_frame_requests == 1,
               "key frame request metric ignored cooldown");
  return ok;
}

} // namespace

int main() {
  bool ok = true;
  ok &= TestOneFramePerPlaybackCycle();
  ok &= TestSevereLagJumpsToNewestKeyFrame();
  ok &= TestSevereLagRequestsKeyFrameWhenNoSyncPointExists();
  ok &= TestAdaptiveBufferStaysWithinOneToThreeFrames();
  ok &= TestLegacyMicrosecondTimestampFallsBackToArrivalCadence();
  ok &= TestSixtyFpsCadenceDoesNotTriggerCatchUp();
  ok &= TestKeyFrameRequestCooldownKeepsDecodableProgress();
  return ok ? 0 : 1;
}
