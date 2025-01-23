/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-03
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VIDEO_CHANNEL_RECEIVE_H_
#define _VIDEO_CHANNEL_RECEIVE_H_

#include "ice_agent.h"
#include "rtp_video_receiver.h"

class VideoChannelReceive {
 public:
  VideoChannelReceive();
  VideoChannelReceive(
      std::shared_ptr<IceAgent> ice_agent,
      std::shared_ptr<IOStatistics> ice_io_statistics,
      std::function<void(VideoFrame &)> on_receive_complete_frame);

  ~VideoChannelReceive();

 public:
  void Initialize(rtp::PAYLOAD_TYPE payload_type);
  void Destroy();

  int OnReceiveRtpPacket(const char *data, size_t size);

 private:
  std::shared_ptr<IceAgent> ice_agent_ = nullptr;
  std::shared_ptr<IOStatistics> ice_io_statistics_ = nullptr;
  std::unique_ptr<RtpVideoReceiver> rtp_video_receiver_ = nullptr;
  std::function<void(VideoFrame &)> on_receive_complete_frame_ = nullptr;
};

#endif