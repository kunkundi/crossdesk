/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-03
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _ICE_TRANSPORT_H_
#define _ICE_TRANSPORT_H_

#include <iostream>

#include "audio_decoder.h"
#include "audio_encoder.h"
// #include "congestion_control.h"
#include "audio_channel_receive.h"
#include "audio_channel_send.h"
#include "data_channel_receive.h"
#include "data_channel_send.h"
#include "ice_agent.h"
#include "io_statistics.h"
#include "ringbuffer.h"
#include "rtcp_packet_info.h"
#include "rtp_audio_receiver.h"
#include "rtp_audio_sender.h"
#include "rtp_data_receiver.h"
#include "rtp_data_sender.h"
#include "rtp_packet.h"
#include "rtp_video_receiver.h"
#include "rtp_video_sender.h"
#include "video_channel_receive.h"
#include "video_channel_send.h"
#include "video_decoder_factory.h"
#include "video_encoder_factory.h"
#include "ws_client.h"

class IceTransport : public std::enable_shared_from_this<IceTransport> {
 public:
  typedef enum { VIDEO = 96, AUDIO = 97, DATA = 127 } DATA_TYPE;
  typedef enum { H264 = 96, AV1 = 99 } VIDEO_TYPE;
  enum VideoFrameType {
    kEmptyFrame = 0,
    kVideoFrameKey = 3,
    kVideoFrameDelta = 4,
  };

  enum TraversalType { TP2P = 0, TRelay = 1, TUnknown = 2 };

 public:
  IceTransport(bool offer_peer, std::string &transmission_id,
               std::string &user_id, std::string &remote_user_id,
               std::shared_ptr<WsClient> ice_ws_transmission,
               std::function<void(std::string, const std::string &)>
                   on_ice_status_change,
               void *user_data);
  ~IceTransport();

 public:
  int SetLocalCapabilities(bool hardware_acceleration, bool use_trickle_ice,
                           bool use_reliable_ice, bool enable_turn,
                           bool force_turn,
                           std::vector<int> &video_payload_types,
                           std::vector<int> &audio_payload_types);

  int InitIceTransmission(std::string &stun_ip, int stun_port,
                          std::string &turn_ip, int turn_port,
                          std::string &turn_username,
                          std::string &turn_password,
                          rtp::PAYLOAD_TYPE video_codec_payload_type);

  int DestroyIceTransmission();

  void SetOnReceiveVideoFunc(
      std::function<void(const XVideoFrame *, const char *, const size_t,
                         void *)>
          on_receive_video) {
    on_receive_video_ = on_receive_video;
  }

  void SetOnReceiveAudioFunc(
      std::function<void(const char *, size_t, const char *, const size_t,
                         void *)>
          on_receive_audio) {
    on_receive_audio_ = on_receive_audio;
  }

  void SetOnReceiveDataFunc(
      std::function<void(const char *, size_t, const char *, const size_t,
                         void *)>
          on_receive_data) {
    on_receive_data_ = on_receive_data;
  }

  void SetOnReceiveNetStatusReportFunc(
      std::function<void(const char *, const size_t, TraversalMode,
                         const XNetTrafficStats *, const char *, const size_t,
                         void *)>
          on_receive_net_status_report) {
    on_receive_net_status_report_ = on_receive_net_status_report;
  }

  int JoinTransmission();

  int SetTransmissionId(const std::string &transmission_id);

  int SendVideoFrame(const XVideoFrame *video_frame);

  int SendAudioFrame(const char *data, size_t size);

  int SendDataFrame(const char *data, size_t size);

 public:
  int GatherCandidates();

  int SendLocalStreamSdp();

  int SetRemoteSdp(const std::string &remote_sdp);

  int SendOffer();

  int SendAnswer();

  std::vector<rtp::PAYLOAD_TYPE> GetNegotiatedCapabilities();

 private:
  int AppendLocalCapabilitiesToOffer(const std::string &remote_sdp);
  int AppendLocalCapabilitiesToAnswer(const std::string &remote_sdp);
  std::string GetRemoteCapabilities(const std::string &remote_sdp);

  bool NegotiateVideoPayloadType(const std::string &remote_sdp);
  bool NegotiateAudioPayloadType(const std::string &remote_sdp);
  bool NegotiateDataPayloadType(const std::string &remote_sdp);

  int CreateVideoCodec(rtp::PAYLOAD_TYPE video_pt, bool hardware_acceleration);
  int CreateAudioCodec();

 private:
  uint8_t CheckIsRtpPacket(const char *buffer, size_t size);
  uint8_t CheckIsRtcpPacket(const char *buffer, size_t size);
  uint8_t CheckIsVideoPacket(const char *buffer, size_t size);
  uint8_t CheckIsAudioPacket(const char *buffer, size_t size);
  uint8_t CheckIsDataPacket(const char *buffer, size_t size);

 private:
  void InitializeIOStatistics();

  void InitializeChannels(rtp::PAYLOAD_TYPE video_codec_payload_type);

  void OnIceStateChange(NiceAgent *agent, guint stream_id, guint component_id,
                        NiceComponentState state, gpointer user_ptr);

  void OnNewLocalCandidate(NiceAgent *agent, guint stream_id,
                           guint component_id, gchar *foundation,
                           gpointer user_ptr);

  void OnGatheringDone(NiceAgent *agent, guint stream_id, gpointer user_ptr);

  void OnNewSelectedPair(NiceAgent *agent, guint stream_id, guint component_id,
                         const char *lfoundation, const char *rfoundation,
                         gpointer user_ptr);

  void OnReceiveBuffer(NiceAgent *agent, guint stream_id, guint component_id,
                       guint size, gchar *buffer, gpointer user_ptr);

  void OnReceiveCompleteFrame(VideoFrame &video_frame);

  void OnReceiveCompleteAudio(const char *data, size_t size);

  void OnReceiveCompleteData(const char *data, size_t size);

  bool ParseRtcpPacket(const uint8_t *buffer, size_t size,
                       RtcpPacketInfo *rtcp_packet_info);

  bool HandleCongestionControlFeedback(
      const webrtc::rtcp::CommonHeader &rtcp_block,
      RtcpPacketInfo *rtcp_packet_info);

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
  void *user_data_ = nullptr;

 private:
  std::shared_ptr<IceAgent> ice_agent_ = nullptr;
  bool is_closed_ = false;
  std::shared_ptr<WsClient> ice_ws_transport_ = nullptr;
  //   CongestionControl *congestion_control_ = nullptr;
  std::function<void(const XVideoFrame *, const char *, const size_t, void *)>
      on_receive_video_ = nullptr;
  std::function<void(const char *, size_t, const char *, const size_t, void *)>
      on_receive_audio_ = nullptr;
  std::function<void(const char *, size_t, const char *, const size_t, void *)>
      on_receive_data_ = nullptr;

  std::function<void(std::string, const std::string &)> on_ice_status_change_ =
      nullptr;

  std::function<void(const char *, const size_t, TraversalMode,
                     const XNetTrafficStats *, const char *, const size_t,
                     void *)>
      on_receive_net_status_report_ = nullptr;

 private:
  std::unique_ptr<VideoChannelSend> video_channel_send_ = nullptr;
  std::unique_ptr<VideoChannelReceive> video_channel_receive_ = nullptr;
  std::unique_ptr<AudioChannelSend> audio_channel_send_ = nullptr;
  std::unique_ptr<AudioChannelReceive> audio_channel_receive_ = nullptr;
  std::unique_ptr<DataChannelSend> data_channel_send_ = nullptr;
  std::unique_ptr<DataChannelReceive> data_channel_receive_ = nullptr;

  std::unique_ptr<RtpVideoReceiver> rtp_video_receiver_ = nullptr;
  std::unique_ptr<RtpVideoSender> rtp_video_sender_ = nullptr;
  std::unique_ptr<RtpAudioReceiver> rtp_audio_receiver_ = nullptr;
  std::unique_ptr<RtpAudioSender> rtp_audio_sender_ = nullptr;
  std::unique_ptr<RtpDataReceiver> rtp_data_receiver_ = nullptr;
  std::unique_ptr<RtpDataSender> rtp_data_sender_ = nullptr;
  bool start_send_packet_ = false;

  uint32_t last_complete_frame_ts_ = 0;

 private:
  std::shared_ptr<IOStatistics> ice_io_statistics_ = nullptr;

 private:
  rtp::PAYLOAD_TYPE video_codec_payload_type_;
  bool remote_capabilities_got_ = false;
  rtp::PAYLOAD_TYPE remote_prefered_video_pt_ = rtp::PAYLOAD_TYPE::UNDEFINED;
  rtp::PAYLOAD_TYPE remote_prefered_audio_pt_ = rtp::PAYLOAD_TYPE::UNDEFINED;
  rtp::PAYLOAD_TYPE remote_prefered_data_pt_ = rtp::PAYLOAD_TYPE::UNDEFINED;
  rtp::PAYLOAD_TYPE negotiated_video_pt_ = rtp::PAYLOAD_TYPE::UNDEFINED;
  rtp::PAYLOAD_TYPE negotiated_audio_pt_ = rtp::PAYLOAD_TYPE::UNDEFINED;
  rtp::PAYLOAD_TYPE negotiated_data_pt_ = rtp::PAYLOAD_TYPE::UNDEFINED;

 private:
  std::unique_ptr<VideoEncoder> video_encoder_ = nullptr;
  std::unique_ptr<VideoDecoder> video_decoder_ = nullptr;
  bool b_force_i_frame_ = false;
  bool video_codec_inited_ = false;
  bool load_nvcodec_dll_success_ = false;
  bool hardware_acceleration_ = false;

 private:
  std::unique_ptr<AudioEncoder> audio_encoder_ = nullptr;
  std::unique_ptr<AudioDecoder> audio_decoder_ = nullptr;
  bool audio_codec_inited_ = false;
};

#endif