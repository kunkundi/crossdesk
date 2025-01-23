/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-03
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _AUDIO_CHANNEL_SEND_H_
#define _AUDIO_CHANNEL_SEND_H_

#include "ice_agent.h"
#include "rtp_audio_sender.h"
#include "rtp_packetizer.h"

class AudioChannelSend {
 public:
  AudioChannelSend();
  AudioChannelSend(std::shared_ptr<IceAgent> ice_agent,
                   std::shared_ptr<IOStatistics> ice_io_statistics);
  ~AudioChannelSend();

 public:
  void Initialize(rtp::PAYLOAD_TYPE payload_type);
  void Destroy();
  int SendAudio(char *data, size_t size);

 private:
  std::shared_ptr<IceAgent> ice_agent_ = nullptr;
  std::shared_ptr<IOStatistics> ice_io_statistics_ = nullptr;
  std::unique_ptr<RtpPacketizer> rtp_packetizer_ = nullptr;
  std::unique_ptr<RtpAudioSender> rtp_audio_sender_ = nullptr;
};

#endif