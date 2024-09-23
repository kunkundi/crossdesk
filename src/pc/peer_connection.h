#ifndef _PEER_CONNECTION_H_
#define _PEER_CONNECTION_H_

#include <iostream>
#include <map>
#include <mutex>

#include "audio_decoder.h"
#include "audio_encoder.h"
#include "ice_transmission.h"
#include "video_decoder_factory.h"
#include "video_encoder_factory.h"
#include "ws_client.h"
#include "x.h"

typedef void (*OnReceiveBuffer)(const char *, size_t, const char *,
                                const size_t, void *);

typedef void (*OnReceiveVideoFrame)(const XVideoFrame *video_frame,
                                    const char *, const size_t, void *);

typedef void (*OnSignalStatus)(SignalStatus, void *);

typedef void (*OnConnectionStatus)(ConnectionStatus, void *);

typedef void (*NetStatusReport)(int, TraversalMode, const unsigned short,
                                const unsigned short, void *);

typedef struct {
  bool use_cfg_file;
  const char *cfg_path;

  const char *signal_server_ip;
  int signal_server_port;
  const char *stun_server_ip;
  int stun_server_port;
  const char *turn_server_ip;
  int turn_server_port;
  const char *turn_server_username;
  const char *turn_server_password;
  bool hardware_acceleration;
  bool av1_encoding;
  bool enable_turn;

  OnReceiveBuffer on_receive_video_buffer;
  OnReceiveBuffer on_receive_audio_buffer;
  OnReceiveBuffer on_receive_data_buffer;

  OnReceiveVideoFrame on_receive_video_frame;

  OnSignalStatus on_signal_status;
  OnConnectionStatus on_connection_status;
  NetStatusReport net_status_report;
  void *user_data;
} PeerConnectionParams;

struct IceWorkMsg {
  enum Type {
    Login = 0,
    UserIdList,
    UserLeaveTransmission,
    Offer,
    Answer,
    NewCandidate,
    Destroy
  };

  Type type;
  std::vector<std::string> user_id_list;
  std::string user_id;
  std::string transmission_id;
  std::string remote_user_id;
  std::string new_candidate;
  std::string remote_sdp;
};

class PeerConnection {
 public:
  PeerConnection();
  ~PeerConnection();

 public:
  int Init(PeerConnectionParams params, const std::string &user_id);

  int Create(const std::string &transmission_id = "",
             const std::string &password = "");

  int Join(const std::string &transmission_id,
           const std::string &password = "");

  int Leave(const std::string &transmission_id);

  int Destroy();

  SignalStatus GetSignalStatus();

  int SendVideoData(const char *data, size_t size);
  int SendAudioData(const char *data, size_t size);
  int SendUserData(const char *data, size_t size);

  int SendVideoData(const XVideoFrame *video_frame);

 private:
  int Login();

  int CreateVideoCodec(bool av1, bool hardware_acceleration);
  int CreateAudioCodec();

  void ProcessSignal(const std::string &signal);

  int RequestTransmissionMemberList(const std::string &transmission_id,
                                    const std::string &password);

  int NegotiationFailed();

 private:
  void StartIceWorker();
  void StopIceWorker();
  void ProcessIceWorkMsg(const IceWorkMsg &msg);
  void PushIceWorkMsg(const IceWorkMsg &msg);

 private:
  std::string uri_ = "";
  std::string cfg_signal_server_ip_;
  std::string cfg_signal_server_port_;
  std::string cfg_stun_server_ip_;
  std::string cfg_stun_server_port_;
  std::string cfg_turn_server_ip_;
  std::string cfg_turn_server_port_;
  std::string cfg_turn_server_username_;
  std::string cfg_turn_server_password_;
  std::string cfg_hardware_acceleration_;
  std::string cfg_av1_encoding_;
  std::string cfg_enable_turn_;
  int signal_server_port_ = 0;
  int stun_server_port_ = 0;
  int turn_server_port_ = 0;
  bool hardware_acceleration_ = false;
  bool av1_encoding_ = false;
  bool enable_turn_ = false;
  bool trickle_ice_ = true;
  TraversalMode mode_ = TraversalMode::P2P;
  bool try_rejoin_with_turn_ = true;
  std::vector<int> video_payload_types_ = {RtpPacket::PAYLOAD_TYPE::H264,
                                           RtpPacket::PAYLOAD_TYPE::AV1};
  std::vector<int> audio_payload_types_ = {RtpPacket::PAYLOAD_TYPE::OPUS};

 private:
  std::shared_ptr<WsClient> ws_transport_ = nullptr;
  std::function<void(const std::string &)> on_receive_ws_msg_ = nullptr;
  std::function<void(WsStatus)> on_ws_status_ = nullptr;
  unsigned int ws_connection_id_ = 0;
  bool offer_peer_ = false;
  std::string user_id_ = "";
  std::string remote_user_id_ = "";
  std::string local_transmission_id_ = "";
  std::string remote_transmission_id_ = "";
  std::vector<std::string> user_id_list_;
  WsStatus ws_status_ = WsStatus::WsClosed;
  SignalStatus signal_status_ = SignalStatus::SignalClosed;
  std::mutex signal_status_mutex_;
  std::atomic<bool> leave_{false};
  std::string sdp_without_cands_ = "";

 private:
  std::map<std::string, std::unique_ptr<IceTransmission>>
      ice_transmission_list_;
  std::function<void(const char *, size_t, const char *, size_t)>
      on_receive_video_ = nullptr;
  std::function<void(const char *, size_t, const char *, size_t)>
      on_receive_audio_ = nullptr;
  std::function<void(const char *, size_t, const char *, size_t)>
      on_receive_data_ = nullptr;
  std::function<void(std::string)> on_ice_status_change_ = nullptr;
  std::function<void(int, IceTransmission::TraversalType, const unsigned short,
                     const unsigned short, void *)>
      on_net_status_report_ = nullptr;
  bool ice_ready_ = false;

  OnReceiveBuffer on_receive_video_buffer_;
  OnReceiveBuffer on_receive_audio_buffer_;
  OnReceiveBuffer on_receive_data_buffer_;

  OnReceiveVideoFrame on_receive_video_frame_;

  OnSignalStatus on_signal_status_;
  OnConnectionStatus on_connection_status_;
  NetStatusReport net_status_report_;
  void *user_data_;

  char *nv12_data_ = nullptr;
  bool inited_ = false;
  std::string password_;

 private:
  std::unique_ptr<VideoEncoder> video_encoder_ = nullptr;
  std::unique_ptr<VideoDecoder> video_decoder_ = nullptr;
  bool hardware_accelerated_encode_ = false;
  bool hardware_accelerated_decode_ = false;
  bool b_force_i_frame_ = false;
  bool video_codec_inited_ = false;
  bool load_nvcodec_dll_success = false;

 private:
  std::unique_ptr<AudioEncoder> audio_encoder_ = nullptr;
  std::unique_ptr<AudioDecoder> audio_decoder_ = nullptr;
  bool audio_codec_inited_ = false;

 private:
  std::thread ice_worker_;
  std::atomic<bool> ice_worker_running_{true};
  std::queue<IceWorkMsg> ice_work_msg_queue_;
  std::condition_variable ice_work_cv_;
  std::mutex ice_work_mutex_;
};

#endif