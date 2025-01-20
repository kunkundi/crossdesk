/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "congestion_control_feedback_generator.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "clock.h"
#include "congestion_control_feedback.h"
#include "ntp_time_util.h"
#include "rtcp_packet.h"
#include "rtp_packet_received.h"

namespace webrtc {

CongestionControlFeedbackGenerator::CongestionControlFeedbackGenerator(
    std::shared_ptr<Clock> clock, RtcpSender rtcp_sender)
    : clock_(clock),
      rtcp_sender_(std::move(rtcp_sender)),
      min_time_between_feedback_(TimeDelta::Millis(25)),
      max_time_to_wait_for_packet_with_marker_(TimeDelta::Millis(25)),
      max_time_between_feedback_(TimeDelta::Millis(250)) {}

void CongestionControlFeedbackGenerator::OnReceivedPacket(
    const RtpPacketReceived& packet) {
  marker_bit_seen_ |= packet.Marker();
  if (!first_arrival_time_since_feedback_) {
    first_arrival_time_since_feedback_ = packet.arrival_time();
  }
  feedback_trackers_[packet.Ssrc()].ReceivedPacket(packet);
  if (NextFeedbackTime() < packet.arrival_time()) {
    SendFeedback(Timestamp::Micros(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count()));
  }
}

Timestamp CongestionControlFeedbackGenerator::NextFeedbackTime() const {
  if (!first_arrival_time_since_feedback_) {
    return std::max(Timestamp::Micros(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count()) +
                        min_time_between_feedback_,
                    next_possible_feedback_send_time_);
  }

  if (!marker_bit_seen_) {
    return std::max(next_possible_feedback_send_time_,
                    *first_arrival_time_since_feedback_ +
                        max_time_to_wait_for_packet_with_marker_);
  }
  return next_possible_feedback_send_time_;
}

TimeDelta CongestionControlFeedbackGenerator::Process(Timestamp now) {
  if (NextFeedbackTime() <= now) {
    SendFeedback(now);
  }
  return NextFeedbackTime() - now;
}

void CongestionControlFeedbackGenerator::OnSendBandwidthEstimateChanged(
    DataRate estimate) {
  // Feedback reports should max occupy 5% of total bandwidth.
  max_feedback_rate_ = estimate * 0.05;
}

void CongestionControlFeedbackGenerator::SetTransportOverhead(
    DataSize overhead_per_packet) {
  packet_overhead_ = overhead_per_packet;
}

void CongestionControlFeedbackGenerator::SendFeedback(Timestamp now) {
  uint32_t compact_ntp = CompactNtp(clock_->ConvertTimestampToNtpTime(now));
  std::vector<rtcp::CongestionControlFeedback::PacketInfo> rtcp_packet_info;
  for (auto& [unused, tracker] : feedback_trackers_) {
    tracker.AddPacketsToFeedback(now, rtcp_packet_info);
  }
  marker_bit_seen_ = false;
  first_arrival_time_since_feedback_ = std::nullopt;

  auto feedback = std::make_unique<rtcp::CongestionControlFeedback>(
      std::move(rtcp_packet_info), compact_ntp);
  CalculateNextPossibleSendTime(DataSize::Bytes(feedback->BlockLength()), now);

  std::vector<std::unique_ptr<RtcpPacket>> rtcp_packets;
  rtcp_packets.push_back(std::move(feedback));
  rtcp_sender_(std::move(rtcp_packets));
}

void CongestionControlFeedbackGenerator::CalculateNextPossibleSendTime(
    DataSize feedback_size, Timestamp now) {
  TimeDelta time_since_last_sent = now - last_feedback_sent_time_;
  DataSize debt_payed = time_since_last_sent * max_feedback_rate_;
  send_rate_debt_ = debt_payed > send_rate_debt_ ? DataSize::Zero()
                                                 : send_rate_debt_ - debt_payed;
  send_rate_debt_ += feedback_size + packet_overhead_;
  last_feedback_sent_time_ = now;
  next_possible_feedback_send_time_ =
      now + std::clamp(max_feedback_rate_.IsZero()
                           ? TimeDelta::PlusInfinity()
                           : send_rate_debt_ / max_feedback_rate_,
                       min_time_between_feedback_, max_time_between_feedback_);
}

}  // namespace webrtc
