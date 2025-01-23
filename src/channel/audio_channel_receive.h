/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-03
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _AUDIO_CHANNEL_RECEIVE_H_
#define _AUDIO_CHANNEL_RECEIVE_H_

#include "ice_agent.h"
#include "rtp_audio_receiver.h"

class AudioChannelReceive {
 public:
  AudioChannelReceive();
  AudioChannelReceive(
      std::shared_ptr<IceAgent> ice_agent,
      std::shared_ptr<IOStatistics> ice_io_statistics,
      std::function<void(const char *, size_t)> on_receive_audio);
  ~AudioChannelReceive();

 public:
  void Initialize(rtp::PAYLOAD_TYPE payload_type);
  void Destroy();
  int OnReceiveRtpPacket(const char *data, size_t size);

 private:
  std::shared_ptr<IceAgent> ice_agent_ = nullptr;
  std::shared_ptr<IOStatistics> ice_io_statistics_ = nullptr;
  std::unique_ptr<RtpAudioReceiver> rtp_audio_receiver_ = nullptr;
  std::function<void(const char *, size_t)> on_receive_audio_ = nullptr;
};

#endif