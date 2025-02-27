/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-03
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VIDEO_CHANNEL_RECEIVE_H_
#define _VIDEO_CHANNEL_RECEIVE_H_

#include "clock/system_clock.h"
#include "ice_agent.h"
#include "rtp_video_receiver.h"

class VideoChannelReceive {
 public:
  VideoChannelReceive();
  VideoChannelReceive(
      std::shared_ptr<SystemClock> clock, std::shared_ptr<IceAgent> ice_agent,
      std::shared_ptr<IOStatistics> ice_io_statistics,
      std::function<void(VideoFrame &)> on_receive_complete_frame);

  ~VideoChannelReceive();

 public:
  void Initialize(rtp::PAYLOAD_TYPE payload_type);
  void Destroy();

  uint32_t GetSsrc() {
    if (rtp_video_receiver_) {
      return rtp_video_receiver_->GetSsrc();
    }
    return 0;
  }

  uint32_t GetRemoteSsrc() {
    if (rtp_video_receiver_) {
      return rtp_video_receiver_->GetRemoteSsrc();
    }
    return 0;
  }

  int OnReceiveRtpPacket(const char *data, size_t size);

  void OnSenderReport(const SenderReport &sender_report) {
    if (rtp_video_receiver_) {
      rtp_video_receiver_->OnSenderReport(sender_report);
    }
  }

 private:
  std::shared_ptr<IceAgent> ice_agent_ = nullptr;
  std::shared_ptr<IOStatistics> ice_io_statistics_ = nullptr;
  std::unique_ptr<RtpVideoReceiver> rtp_video_receiver_ = nullptr;
  std::function<void(VideoFrame &)> on_receive_complete_frame_ = nullptr;

 private:
  std::shared_ptr<SystemClock> clock_;
};

#endif