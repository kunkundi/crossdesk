/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-03
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VIDEO_CHANNEL_SEND_H_
#define _VIDEO_CHANNEL_SEND_H_

#include "api/transport/network_types.h"
#include "api/units/timestamp.h"
#include "clock/system_clock.h"
#include "congestion_control.h"
#include "congestion_control_feedback.h"
#include "encoded_frame.h"
#include "ice_agent.h"
#include "packet_sender.h"
#include "rtp_packet_history.h"
#include "rtp_packetizer.h"
#include "rtp_video_sender.h"
#include "transport_feedback_adapter.h"

class VideoChannelSend {
 public:
  VideoChannelSend(std::shared_ptr<SystemClock> clock,
                   std::shared_ptr<IceAgent> ice_agent,
                   std::shared_ptr<PacketSender> packet_sender,
                   std::shared_ptr<IOStatistics> ice_io_statistics,
                   std::function<void(const webrtc::RtpPacketToSend& packet)>
                       on_sent_packet_func_);
  ~VideoChannelSend();

  void SetEnqueuePacketsFunc(
      std::function<
          void(std::vector<std::unique_ptr<webrtc::RtpPacketToSend>>&)>
          enqueue_packets_func);

  void OnSentRtpPacket(std::unique_ptr<webrtc::RtpPacketToSend> packet);

  void OnReceiveNack(const std::vector<uint16_t>& nack_sequence_numbers);

  std::vector<std::unique_ptr<RtpPacket>> GeneratePadding(
      uint32_t payload_size, int64_t captured_timestamp_us);

 public:
  void Initialize(rtp::PAYLOAD_TYPE payload_type);
  void Destroy();

  uint32_t GetSsrc() {
    if (rtp_video_sender_) {
      return rtp_video_sender_->GetSsrc();
    }
    return 0;
  }

  int SendVideo(const EncodedFrame& encoded_frame);

  void OnReceiverReport(const ReceiverReport& receiver_report) {
    if (rtp_video_sender_) {
      rtp_video_sender_->OnReceiverReport(receiver_report);
    }
  }

 private:
  int32_t ReSendPacket(uint16_t packet_id);

 private:
  std::shared_ptr<PacketSender> packet_sender_ = nullptr;
  std::shared_ptr<IceAgent> ice_agent_ = nullptr;
  std::shared_ptr<IOStatistics> ice_io_statistics_ = nullptr;
  std::unique_ptr<RtpPacketizer> rtp_packetizer_ = nullptr;
  std::unique_ptr<RtpVideoSender> rtp_video_sender_ = nullptr;

  std::function<void(const webrtc::RtpPacketToSend& packet)>
      on_sent_packet_func_ = nullptr;

 private:
  std::shared_ptr<SystemClock> clock_;
  RtpPacketHistory rtp_packet_history_;
  int64_t delta_ntp_internal_ms_;

 private:
  FILE* file_rtp_sent_ = nullptr;
};

#endif