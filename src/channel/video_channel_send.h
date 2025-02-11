/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-03
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VIDEO_CHANNEL_SEND_H_
#define _VIDEO_CHANNEL_SEND_H_

#include "api/transport/network_types.h"
#include "api/units/timestamp.h"
#include "clock.h"
#include "congestion_control.h"
#include "congestion_control_feedback.h"
#include "ice_agent.h"
#include "rtp_packetizer.h"
#include "rtp_video_sender.h"
#include "transport_feedback.h"
#include "transport_feedback_adapter.h"

class VideoChannelSend {
 public:
  VideoChannelSend();
  VideoChannelSend(std::shared_ptr<webrtc::Clock> clock,
                   std::shared_ptr<IceAgent> ice_agent,
                   std::shared_ptr<IOStatistics> ice_io_statistics,
                   std::function<void(const webrtc::RtpPacketToSend& packet)>
                       on_sent_packet_func_);
  ~VideoChannelSend();

 public:
  void Initialize(rtp::PAYLOAD_TYPE payload_type);
  void Destroy();

  int SendVideo(char* data, size_t size);

  void OnCongestionControlFeedback(
      Timestamp recv_ts,
      const webrtc::rtcp::CongestionControlFeedback& feedback);

  void HandleTransportPacketsFeedback(
      const webrtc::TransportPacketsFeedback& feedback);

 private:
  void PostUpdates(webrtc::NetworkControlUpdate update);
  void UpdateControlState();
  void UpdateCongestedState();

 private:
  std::shared_ptr<IceAgent> ice_agent_ = nullptr;
  std::shared_ptr<IOStatistics> ice_io_statistics_ = nullptr;
  std::unique_ptr<RtpPacketizer> rtp_packetizer_ = nullptr;
  std::unique_ptr<RtpVideoSender> rtp_video_sender_ = nullptr;

  std::function<void(const webrtc::RtpPacketToSend& packet)>
      on_sent_packet_func_ = nullptr;

 private:
  std::shared_ptr<Clock> clock_;
};

#endif