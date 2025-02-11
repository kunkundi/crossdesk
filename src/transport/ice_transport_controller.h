/*
 * @Author: DI JUNKUN
 * @Date: 2025-02-11
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _ICE_TRANSPORT_CONTROLLER_H_
#define _ICE_TRANSPORT_CONTROLLER_H_

#include "api/transport/network_types.h"
#include "api/units/timestamp.h"
#include "audio_channel_receive.h"
#include "audio_channel_send.h"
#include "audio_decoder.h"
#include "audio_encoder.h"
#include "clock.h"
#include "congestion_control.h"
#include "congestion_control_feedback.h"
#include "data_channel_receive.h"
#include "data_channel_send.h"
#include "ice_agent.h"
#include "transport_feedback_adapter.h"
#include "video_channel_receive.h"
#include "video_channel_send.h"
#include "video_decoder_factory.h"
#include "video_encoder_factory.h"

typedef void (*OnReceiveVideo)(const XVideoFrame *, const char *, const size_t,
                               void *);
typedef void (*OnReceiveAudio)(const char *, size_t, const char *, const size_t,
                               void *);
typedef void (*OnReceiveData)(const char *, size_t, const char *, const size_t,
                              void *);

class IceTransportController
    : public std::enable_shared_from_this<IceTransportController> {
 public:
  IceTransportController();
  ~IceTransportController();

 public:
  void Create(std::string remote_user_id,
              rtp::PAYLOAD_TYPE video_codec_payload_type,
              bool hardware_acceleration, std::shared_ptr<IceAgent> ice_agent,
              std::shared_ptr<IOStatistics> ice_io_statistics,
              OnReceiveVideo on_receive_video, OnReceiveAudio on_receive_audio,
              OnReceiveData on_receive_data, void *user_data);
  void Destroy();

  int SendVideo(const XVideoFrame *video_frame);
  int SendAudio(const char *data, size_t size);
  int SendData(const char *data, size_t size);

  int OnReceiveVideoRtpPacket(const char *data, size_t size);
  int OnReceiveAudioRtpPacket(const char *data, size_t size);
  int OnReceiveDataRtpPacket(const char *data, size_t size);

  void OnReceiveCompleteFrame(VideoFrame &video_frame);
  void OnReceiveCompleteAudio(const char *data, size_t size);
  void OnReceiveCompleteData(const char *data, size_t size);

 public:
  void OnCongestionControlFeedback(
      const webrtc::rtcp::CongestionControlFeedback &feedback);

 private:
  int CreateVideoCodec(rtp::PAYLOAD_TYPE video_pt, bool hardware_acceleration);
  int CreateAudioCodec();

 private:
  void OnSentRtpPacket(const webrtc::RtpPacketToSend &packet);
  void HandleTransportPacketsFeedback(
      const webrtc::TransportPacketsFeedback &feedback);
  void PostUpdates(webrtc::NetworkControlUpdate update);
  void UpdateControlState();
  void UpdateCongestedState();

 private:
  std::unique_ptr<VideoChannelSend> video_channel_send_ = nullptr;
  std::unique_ptr<AudioChannelSend> audio_channel_send_ = nullptr;
  std::unique_ptr<DataChannelSend> data_channel_send_ = nullptr;

  std::unique_ptr<VideoChannelReceive> video_channel_receive_ = nullptr;
  std::unique_ptr<AudioChannelReceive> audio_channel_receive_ = nullptr;
  std::unique_ptr<DataChannelReceive> data_channel_receive_ = nullptr;

  OnReceiveVideo on_receive_video_ = nullptr;
  OnReceiveAudio on_receive_audio_ = nullptr;
  OnReceiveData on_receive_data_ = nullptr;

 private:
  std::shared_ptr<IceAgent> ice_agent_ = nullptr;
  std::shared_ptr<IOStatistics> ice_io_statistics_ = nullptr;
  std::unique_ptr<RtpPacketizer> rtp_packetizer_ = nullptr;
  std::unique_ptr<RtpVideoSender> rtp_video_sender_ = nullptr;
  std::string remote_user_id_;
  void *user_data_ = nullptr;

 private:
  std::shared_ptr<Clock> clock_;
  webrtc::TransportFeedbackAdapter transport_feedback_adapter_;
  std::unique_ptr<CongestionControl> controller_;

 private:
  std::unique_ptr<VideoEncoder> video_encoder_ = nullptr;
  std::unique_ptr<VideoDecoder> video_decoder_ = nullptr;
  bool b_force_i_frame_;
  bool video_codec_inited_;
  bool load_nvcodec_dll_success_;
  bool hardware_acceleration_;

 private:
  std::unique_ptr<AudioEncoder> audio_encoder_ = nullptr;
  std::unique_ptr<AudioDecoder> audio_decoder_ = nullptr;
  bool audio_codec_inited_ = false;

 private:
  uint64_t target_bitrate_ = 0;
};

#endif