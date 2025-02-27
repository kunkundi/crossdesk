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

  uint32_t GetSsrc() {
    if (rtp_audio_receiver_) {
      return rtp_audio_receiver_->GetSsrc();
    }
    return 0;
  }

  uint32_t GetRemoteSsrc() {
    if (rtp_audio_receiver_) {
      return rtp_audio_receiver_->GetRemoteSsrc();
    }
    return 0;
  }

  int OnReceiveRtpPacket(const char *data, size_t size);

  void OnSenderReport(const SenderReport &sender_report) {
    if (rtp_audio_receiver_) {
      rtp_audio_receiver_->OnSenderReport(sender_report);
    }
  }

 private:
  std::shared_ptr<IceAgent> ice_agent_ = nullptr;
  std::shared_ptr<IOStatistics> ice_io_statistics_ = nullptr;
  std::unique_ptr<RtpAudioReceiver> rtp_audio_receiver_ = nullptr;
  std::function<void(const char *, size_t)> on_receive_audio_ = nullptr;
};

#endif