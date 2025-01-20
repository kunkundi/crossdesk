/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-03
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VIDEO_CHANNEL_SEND_H_
#define _VIDEO_CHANNEL_SEND_H_

#include "congestion_control_feedback.h"
#include "ice_agent.h"
#include "rtp_codec.h"
#include "rtp_video_sender.h"
#include "transport_feedback_adapter.h"

class VideoChannelSend {
 public:
  VideoChannelSend();
  VideoChannelSend(std::shared_ptr<IceAgent> ice_agent,
                   std::shared_ptr<IOStatistics> ice_io_statistics);
  ~VideoChannelSend();

 public:
  void Initialize(RtpPacket::PAYLOAD_TYPE payload_type);
  void Destroy();

  int SendVideo(char* data, size_t size);

  void OnCongestionControlFeedback(
      int64_t recv_ts, const webrtc::rtcp::CongestionControlFeedback& feedback);

  void HandleTransportPacketsFeedback(
      const webrtc::TransportPacketsFeedback& feedback);

 private:
  std::shared_ptr<IceAgent> ice_agent_ = nullptr;
  std::shared_ptr<IOStatistics> ice_io_statistics_ = nullptr;
  std::unique_ptr<RtpCodec> video_rtp_codec_ = nullptr;
  std::unique_ptr<RtpVideoSender> rtp_video_sender_ = nullptr;

 private:
  int64_t current_offset_ = std::numeric_limits<int64_t>::min();
  // Used by RFC 8888 congestion control feedback to track base time.
  std::optional<uint32_t> last_feedback_compact_ntp_time_;
  int feedback_count_ = 0;

  webrtc::TransportFeedbackAdapter transport_feedback_adapter_;
};

#endif