/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-13
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PACKET_INFO_H_
#define _PACKET_INFO_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <vector>

#include "congestion_control_feedback.h"

struct RtcpPacketInfo {
  uint32_t packet_type_flags = 0;  // RTCPPacketTypeFlags bit field.

  uint32_t remote_ssrc = 0;
  std::vector<uint16_t> nack_sequence_numbers;
  // std::vector<ReportBlockData> report_block_datas;
  std::optional<int64_t> rtt;
  uint32_t receiver_estimated_max_bitrate_bps = 0;
  std::optional<webrtc::rtcp::CongestionControlFeedback>
      congestion_control_feedback;
  // std::optional<VideoBitrateAllocation> target_bitrate_allocation;
  // std::optional<NetworkStateEstimate> network_state_estimate;
  // std::unique_ptr<rtcp::LossNotification> loss_notification;
};

#endif