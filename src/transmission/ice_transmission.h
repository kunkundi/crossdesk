/*
 * @Author: DI JUNKUN
 * @Date: 2023-11-24
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _ICE_TRANSMISSION_H_
#define _ICE_TRANSMISSION_H_

#include <iostream>

#include "congestion_control.h"
#include "ice_agent.h"
#include "io_statistics.h"
#include "ringbuffer.h"
#include "rtp_audio_receiver.h"
#include "rtp_audio_sender.h"
#include "rtp_codec.h"
#include "rtp_data_receiver.h"
#include "rtp_data_sender.h"
#include "rtp_packet.h"
#include "rtp_video_receiver.h"
#include "rtp_video_sender.h"
#include "ws_client.h"

class IceTransmission {
 public:
  typedef enum { VIDEO = 96, AUDIO = 97, DATA = 127 } DATA_TYPE;
  enum VideoFrameType {
    kEmptyFrame = 0,
    kVideoFrameKey = 3,
    kVideoFrameDelta = 4,
  };

  enum TraversalType { TP2P = 0, TRelay = 1, TUnknown = 2 };

 public:
  IceTransmission(bool offer_peer, std::string &transmission_id,
                  std::string &user_id, std::string &remote_user_id,
                  std::shared_ptr<WsClient> ice_ws_transmission,
                  std::function<void(std::string)> on_ice_status_change);
  ~IceTransmission();

 public:
  int SetLocalCapabilities(bool use_trickle_ice, bool use_reliable_ice,
                           bool enable_turn, bool force_turn,
                           std::vector<int> &video_payload_types,
                           std::vector<int> &audio_payload_types);

  int InitIceTransmission(std::string &stun_ip, int stun_port,
                          std::string &turn_ip, int turn_port,
                          std::string &turn_username,
                          std::string &turn_password,
                          RtpPacket::PAYLOAD_TYPE video_codec_payload_type);

  int DestroyIceTransmission();

  void SetOnReceiveVideoFunc(
      std::function<void(const char *, size_t, const char *, size_t)>
          on_receive_video) {
    on_receive_video_ = on_receive_video;
  }

  void SetOnReceiveAudioFunc(
      std::function<void(const char *, size_t, const char *, size_t)>
          on_receive_audio) {
    on_receive_audio_ = on_receive_audio;
  }

  void SetOnReceiveDataFunc(
      std::function<void(const char *, size_t, const char *, size_t)>
          on_receive_data) {
    on_receive_data_ = on_receive_data;
  }

  void SetOnReceiveNetStatusReportFunc(
      std::function<void(int, TraversalType, const unsigned short,
                         const unsigned short, void *)>
          on_receive_net_status_report) {
    on_receive_net_status_report_ = on_receive_net_status_report;
  }

  int JoinTransmission();

  int SetTransmissionId(const std::string &transmission_id);

  int SendData(DATA_TYPE type, const char *data, size_t size);

  int SendVideoData(VideoFrameType frame_type, const char *data, size_t size);

  int SendAudioData(const char *data, size_t size);

  int SendUserData(const char *data, size_t size);

 public:
  int GatherCandidates();

  int SendLocalStreamSdp();

  int SetRemoteSdp(const std::string &remote_sdp);

  int SendOffer();

  int SendAnswer();

  std::vector<RtpPacket::PAYLOAD_TYPE> GetNegotiatedCapabilities();

 private:
  int AppendLocalCapabilitiesToOffer(const std::string &remote_sdp);
  int AppendLocalCapabilitiesToAnswer(const std::string &remote_sdp);
  std::string GetRemoteCapabilities(const std::string &remote_sdp);

  bool NegotiateVideoPayloadType(const std::string &remote_sdp);
  bool NegotiateAudioPayloadType(const std::string &remote_sdp);
  bool NegotiateDataPayloadType(const std::string &remote_sdp);

  int CreateMediaCodec();

 private:
  uint8_t CheckIsRtcpPacket(const char *buffer, size_t size);
  uint8_t CheckIsVideoPacket(const char *buffer, size_t size);
  uint8_t CheckIsAudioPacket(const char *buffer, size_t size);
  uint8_t CheckIsDataPacket(const char *buffer, size_t size);

 private:
  bool use_trickle_ice_ = true;
  bool enable_turn_ = false;
  bool use_reliable_ice_ = false;
  bool force_turn_ = false;
  std::vector<int> support_video_payload_types_;
  std::vector<int> support_audio_payload_types_;
  std::vector<int> support_data_payload_types_;

  std::string local_sdp_;
  std::string remote_sdp_;
  std::string new_local_candidate_;
  std::string local_candidates_;
  std::string remote_candidates_;
  unsigned int connection_id_ = 0;
  bool offer_peer_;
  std::string transmission_id_;
  std::string user_id_;
  std::string remote_user_id_;
  std::string remote_ice_username_ = "";
  NiceComponentState state_ = NICE_COMPONENT_STATE_DISCONNECTED;
  TraversalType traversal_type_ = TraversalType::TP2P;

 private:
  std::unique_ptr<IceAgent> ice_agent_ = nullptr;
  std::shared_ptr<WsClient> ice_ws_transport_ = nullptr;
  CongestionControl *congestion_control_ = nullptr;
  std::function<void(const char *, size_t, const char *, size_t)>
      on_receive_video_ = nullptr;
  std::function<void(const char *, size_t, const char *, size_t)>
      on_receive_audio_ = nullptr;
  std::function<void(const char *, size_t, const char *, size_t)>
      on_receive_data_ = nullptr;
  std::function<void(std::string)> on_ice_status_change_ = nullptr;
  std::function<void(int, TraversalType, const unsigned short,
                     const unsigned short, void *)>
      on_receive_net_status_report_ = nullptr;

 private:
  std::unique_ptr<RtpCodec> video_rtp_codec_ = nullptr;
  std::unique_ptr<RtpCodec> audio_rtp_codec_ = nullptr;
  std::unique_ptr<RtpCodec> data_rtp_codec_ = nullptr;
  std::unique_ptr<RtpVideoReceiver> rtp_video_receiver_ = nullptr;
  std::unique_ptr<RtpVideoSender> rtp_video_sender_ = nullptr;
  std::unique_ptr<RtpAudioReceiver> rtp_audio_receiver_ = nullptr;
  std::unique_ptr<RtpAudioSender> rtp_audio_sender_ = nullptr;
  std::unique_ptr<RtpDataReceiver> rtp_data_receiver_ = nullptr;
  std::unique_ptr<RtpDataSender> rtp_data_sender_ = nullptr;
  RtpPacket pop_packet_;
  bool start_send_packet_ = false;

  uint32_t last_complete_frame_ts_ = 0;

 private:
  std::unique_ptr<IOStatistics> ice_io_statistics_ = nullptr;

 private:
  RtpPacket::PAYLOAD_TYPE video_codec_payload_type_;
  bool remote_capabilities_got_ = false;
  RtpPacket::PAYLOAD_TYPE remote_prefered_video_pt_ =
      RtpPacket::PAYLOAD_TYPE::UNDEFINED;
  RtpPacket::PAYLOAD_TYPE remote_prefered_audio_pt_ =
      RtpPacket::PAYLOAD_TYPE::UNDEFINED;
  RtpPacket::PAYLOAD_TYPE remote_prefered_data_pt_ =
      RtpPacket::PAYLOAD_TYPE::UNDEFINED;
  RtpPacket::PAYLOAD_TYPE negotiated_video_pt_ =
      RtpPacket::PAYLOAD_TYPE::UNDEFINED;
  RtpPacket::PAYLOAD_TYPE negotiated_audio_pt_ =
      RtpPacket::PAYLOAD_TYPE::UNDEFINED;
  RtpPacket::PAYLOAD_TYPE negotiated_data_pt_ =
      RtpPacket::PAYLOAD_TYPE::UNDEFINED;
};

#endif