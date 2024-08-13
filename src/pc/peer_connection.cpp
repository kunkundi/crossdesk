#include "peer_connection.h"

#include <regex>

#include "INIReader.h"
#include "common.h"
#include "log.h"
#include "nlohmann/json.hpp"
#include "nvcodec_api.h"

using nlohmann::json;

PeerConnection::PeerConnection() {}

PeerConnection::~PeerConnection() {
  if (nv12_data_) {
    delete nv12_data_;
    nv12_data_ = nullptr;
  }

  video_codec_inited_ = false;
  audio_codec_inited_ = false;
}

int PeerConnection::Init(PeerConnectionParams params,
                         const std::string &user_id) {
  if (inited_) {
    LOG_INFO("Peer already inited");
    return 0;
  }
  // Todo: checkout user_id unique or not
  user_id_ = user_id;

  if (params.use_cfg_file) {
    INIReader reader(params.cfg_path);
    cfg_signal_server_ip_ = reader.Get("signal server", "ip", "-1");
    cfg_signal_server_port_ = reader.Get("signal server", "port", "-1");
    cfg_stun_server_ip_ = reader.Get("stun server", "ip", "-1");
    cfg_stun_server_port_ = reader.Get("stun server", "port", "-1");
    cfg_turn_server_ip_ = reader.Get("turn server", "ip", "");
    cfg_turn_server_port_ = reader.Get("turn server", "port", "-1");
    cfg_turn_server_username_ = reader.Get("turn server", "username", "");
    cfg_turn_server_password_ = reader.Get("turn server", "password", "");
    cfg_hardware_acceleration_ =
        reader.Get("hardware acceleration", "turn_on", "false");
    cfg_av1_encoding_ = reader.Get("av1 encoding", "turn_on", "false");

    std::regex regex("\n");

    signal_server_port_ = stoi(cfg_signal_server_port_);
    stun_server_port_ = stoi(cfg_stun_server_port_);
    turn_server_port_ = stoi(cfg_turn_server_port_);

    hardware_acceleration_ =
        cfg_hardware_acceleration_ == "true" ? true : false;
    av1_encoding_ = cfg_av1_encoding_ == "true" ? true : false;

  } else {
    cfg_signal_server_ip_ = params.signal_server_ip;
    signal_server_port_ = params.signal_server_port;
    cfg_stun_server_ip_ = params.stun_server_ip;
    stun_server_port_ = params.stun_server_port;
    cfg_turn_server_ip_ = params.turn_server_ip;
    turn_server_port_ = params.turn_server_port;
    cfg_turn_server_username_ = params.turn_server_username;
    cfg_turn_server_password_ = params.turn_server_password;
    hardware_acceleration_ = params.hardware_acceleration;
    av1_encoding_ = params.av1_encoding;

    cfg_signal_server_port_ = std::to_string(signal_server_port_);
    cfg_stun_server_port_ = std::to_string(stun_server_port_);
    cfg_turn_server_port_ = std::to_string(turn_server_port_);
  }

  LOG_INFO("Read config success, use configure file [{}]", params.use_cfg_file);

  LOG_INFO("Signal server ip [{}] port [{}]", cfg_signal_server_ip_,
           cfg_signal_server_port_);

  LOG_INFO("Stun server ip [{}] port [{}]", cfg_stun_server_ip_,
           cfg_stun_server_port_);

  if (!cfg_turn_server_ip_.empty() && 0 != turn_server_port_ &&
      !cfg_turn_server_username_.empty() &&
      !cfg_turn_server_password_.empty()) {
    LOG_INFO("Turn server ip [{}] port [{}] username [{}] password [{}]",
             cfg_turn_server_ip_, turn_server_port_, cfg_turn_server_username_,
             cfg_turn_server_password_);
  }

  LOG_INFO("Hardware accelerated codec [{}]",
           hardware_acceleration_ ? "ON" : "OFF");
  LOG_INFO("Video format [{}]", av1_encoding_ ? "AV1" : "H.264");

  on_receive_video_buffer_ = params.on_receive_video_buffer;
  on_receive_audio_buffer_ = params.on_receive_audio_buffer;
  on_receive_data_buffer_ = params.on_receive_data_buffer;
  on_signal_status_ = params.on_signal_status;
  on_connection_status_ = params.on_connection_status;
  net_status_report_ = params.net_status_report;
  user_data_ = params.user_data;

  on_receive_ws_msg_ = [this](const std::string &msg) { ProcessSignal(msg); };

  on_ws_status_ = [this](WsStatus ws_status) {
    if (WsStatus::WsOpening == ws_status) {
      ws_status_ = WsStatus::WsOpening;
      signal_status_ = SignalStatus::SignalConnecting;
      on_signal_status_(SignalStatus::SignalConnecting, user_data_);
    } else if (WsStatus::WsOpened == ws_status) {
      ws_status_ = WsStatus::WsOpened;
      LOG_INFO("Login to signal server");
      Login();
    } else if (WsStatus::WsFailed == ws_status) {
      ws_status_ = WsStatus::WsFailed;
      signal_status_ = SignalStatus::SignalFailed;
      on_signal_status_(SignalStatus::SignalFailed, user_data_);
    } else if (WsStatus::WsClosed == ws_status) {
      ws_status_ = WsStatus::WsClosed;
      signal_status_ = SignalStatus::SignalClosed;
      on_signal_status_(SignalStatus::SignalClosed, user_data_);
    } else if (WsStatus::WsReconnecting == ws_status) {
      ws_status_ = WsStatus::WsReconnecting;
      signal_status_ = SignalStatus::SignalReconnecting;
      on_signal_status_(SignalStatus::SignalReconnecting, user_data_);
    }
  };

  on_receive_video_ = [this](const char *data, size_t size, const char *user_id,
                             size_t user_id_size) {
    int num_frame_returned = video_decoder_->Decode(
        (uint8_t *)data, size,
        [this, user_id, user_id_size](VideoFrame video_frame) {
          if (on_receive_video_buffer_) {
            on_receive_video_buffer_((const char *)video_frame.Buffer(),
                                     video_frame.Size(), user_id, user_id_size,
                                     user_data_);
          }
        });
  };

  on_receive_audio_ = [this](const char *data, size_t size, const char *user_id,
                             size_t user_id_size) {
    int num_frame_returned = audio_decoder_->Decode(
        (uint8_t *)data, size,
        [this, user_id, user_id_size](uint8_t *data, int size) {
          if (on_receive_audio_buffer_) {
            on_receive_audio_buffer_((const char *)data, size, user_id,
                                     user_id_size, user_data_);
          }
        });
  };

  on_receive_data_ = [this](const char *data, size_t size, const char *user_id,
                            size_t user_id_size) {
    if (on_receive_data_buffer_) {
      on_receive_data_buffer_(data, size, user_id, user_id_size, user_data_);
    }
  };

  on_ice_status_change_ = [this](std::string ice_status) {
    if ("connecting" == ice_status) {
      on_connection_status_(ConnectionStatus::Connecting, user_data_);
    } else if ("disconnected" == ice_status) {
      on_connection_status_(ConnectionStatus::Disconnected, user_data_);
    } else if ("ready" == ice_status) {
      ice_ready_ = true;
      on_connection_status_(ConnectionStatus::Connected, user_data_);
      b_force_i_frame_ = true;
      LOG_INFO("Ice finish");
    } else if ("closed" == ice_status) {
      ice_ready_ = false;
      on_connection_status_(ConnectionStatus::Closed, user_data_);
      LOG_INFO("Ice closed");
    } else {
      ice_ready_ = false;
    }
  };

  on_net_status_report_ = [this](int TransmissionId,
                                 IceTransmission::TraversalType mode,
                                 const unsigned short send,
                                 const unsigned short receive, void *user_ptr) {
    if (net_status_report_) {
      net_status_report_(atoi(transmission_id_.c_str()), TraversalMode(mode),
                         send, receive, user_data_);
    }
  };

  ws_transport_ =
      std::make_shared<WsTransmission>(on_receive_ws_msg_, on_ws_status_);
  uri_ = "ws://" + cfg_signal_server_ip_ + ":" + cfg_signal_server_port_;
  if (ws_transport_) {
    ws_transport_->Connect(uri_);
  }

  // do {
  // } while (SignalStatus::SignalConnected != GetSignalStatus());

  nv12_data_ = new char[1280 * 720 * 3 / 2];

  LOG_INFO("[{}] Init finish", user_id);

  // if (0 != CreateVideoCodec(hardware_acceleration_)) {
  //   LOG_ERROR("Create video codec failed");
  //   return -1;
  // }

  // if (0 != CreateAudioCodec()) {
  //   LOG_ERROR("Create audio codec failed");
  //   return -1;
  // }

  inited_ = true;
  return 0;
}

int PeerConnection::CreateVideoCodec(bool hardware_acceleration) {
  if (video_codec_inited_) {
    return 0;
  }

  hardware_acceleration_ = hardware_acceleration;
#ifdef __APPLE__
  if (hardware_acceleration_) {
    hardware_acceleration_ = false;
    LOG_WARN(
        "MacOS not support hardware acceleration, use default software codec");
  }
#else
#endif

  if (av1_encoding_) {
    video_encoder_ = VideoEncoderFactory::CreateVideoEncoder(false, true);
    video_decoder_ = VideoDecoderFactory::CreateVideoDecoder(false, true);
    LOG_WARN("Only support software codec for AV1");
  } else {
    if (hardware_acceleration_) {
      if (0 == LoadNvCodecDll()) {
        video_encoder_ = VideoEncoderFactory::CreateVideoEncoder(true, false);
        video_decoder_ = VideoDecoderFactory::CreateVideoDecoder(true, false);
      } else {
        LOG_WARN(
            "Hardware accelerated codec not available, use default software "
            "codec");
        video_encoder_ = VideoEncoderFactory::CreateVideoEncoder(false, false);
        video_decoder_ = VideoDecoderFactory::CreateVideoDecoder(false, false);
      }
    }
  }

  if (!video_encoder_) {
    video_encoder_ = VideoEncoderFactory::CreateVideoEncoder(false, false);
    LOG_ERROR("Create encoder failed, try to use software H.264 encoder");
  }
  if (!video_encoder_ || 0 != video_encoder_->Init()) {
    LOG_ERROR("Encoder init failed");
    return -1;
  }

  if (!video_decoder_) {
    video_decoder_ = VideoDecoderFactory::CreateVideoDecoder(false, false);
    LOG_ERROR("Create decoder failed, try to use software H.264 decoder");
  }
  if (!video_decoder_ || video_decoder_->Init()) {
    LOG_ERROR("Decoder init failed");
    return -1;
  }

  video_codec_inited_ = true;
  return 0;
}

int PeerConnection::CreateAudioCodec() {
  if (audio_codec_inited_) {
    return 0;
  }

  audio_encoder_ = std::make_unique<AudioEncoder>(AudioEncoder(48000, 1, 480));
  if (!audio_encoder_ || 0 != audio_encoder_->Init()) {
    LOG_ERROR("Audio encoder init failed");
    return -1;
  }

  audio_decoder_ = std::make_unique<AudioDecoder>(AudioDecoder(48000, 1, 480));
  if (!audio_decoder_ || 0 != audio_decoder_->Init()) {
    LOG_ERROR("Audio decoder init failed");
    return -1;
  }

  audio_codec_inited_ = true;
  return 0;
}

int PeerConnection::Login() {
  if (WsStatus::WsOpened != ws_status_) {
    LOG_ERROR("Websocket not opened");
    return -1;
  }

  int ret = 0;

  json message = {{"type", "login"}, {"user_id", user_id_}};

  if (ws_transport_) {
    ws_transport_->Send(message.dump());
    LOG_INFO("[{}] send login request to signal server", user_id_);
  }
  return ret;
}

int PeerConnection::Create(const std::string &transmission_id,
                           const std::string &password) {
  if (SignalStatus::SignalConnected != GetSignalStatus()) {
    LOG_ERROR("Signal not connected");
    return -1;
  }

  int ret = 0;

  password_ = password;

  json message = {{"type", "create_transmission"},
                  {"user_id", user_id_},
                  {"transmission_id", transmission_id},
                  {"password", password}};

  if (ws_transport_) {
    ws_transport_->Send(message.dump());
    LOG_INFO("Send create transmission request, transmission_id [{}]",
             transmission_id);
  }
  return ret;
}

int PeerConnection::Join(const std::string &transmission_id,
                         const std::string &password) {
  if (SignalStatus::SignalConnected != GetSignalStatus()) {
    LOG_ERROR("Signal not connected");
    return -1;
  }

  int ret = 0;

  password_ = password;
  leave_ = false;

  transmission_id_ = transmission_id;
  ret = RequestTransmissionMemberList(transmission_id_, password);
  return ret;
}

int PeerConnection::Leave(const std::string &transmission_id) {
  if (SignalStatus::SignalConnected != GetSignalStatus()) {
    LOG_ERROR("Signal not connected");
    return -1;
  }

  json message = {{"type", "leave_transmission"},
                  {"user_id", user_id_},
                  {"transmission_id", transmission_id}};
  if (ws_transport_) {
    ws_transport_->Send(message.dump());
    LOG_INFO("[{}] sends leave transmission [{}] notification ", user_id_,
             transmission_id);
  }

  ice_ready_ = false;
  leave_ = true;

  for (auto &user_id_it : ice_transmission_list_) {
    user_id_it.second->DestroyIceTransmission();
  }

  ice_transmission_list_.clear();

  return 0;
}

void PeerConnection::ProcessSignal(const std::string &signal) {
  auto j = json::parse(signal);
  std::string type = j["type"];
  // LOG_INFO("signal type: {}", type);
  switch (HASH_STRING_PIECE(type.c_str())) {
    case "login"_H: {
      if (j["status"].get<std::string>() == "success") {
        user_id_ = j["user_id"].get<std::string>();
        net_status_report_(atoi(user_id_.c_str()), TraversalMode::UnknownMode,
                           0, 0, user_data_);
        LOG_INFO("Login success with id [{}]", user_id_);
        signal_status_ = SignalStatus::SignalConnected;
        on_signal_status_(SignalStatus::SignalConnected, user_data_);
      } else if (j["status"].get<std::string>() == "fail") {
        LOG_WARN("Login failed with id [{}]", transmission_id_);
      }
      break;
    }
    case "transmission_id"_H: {
      if (j["status"].get<std::string>() == "success") {
        transmission_id_ = j["transmission_id"].get<std::string>();
        user_id_ = transmission_id_;
        LOG_INFO("Create transmission success with id [{}]", transmission_id_);
      } else if (j["status"].get<std::string>() == "fail") {
        LOG_WARN("Create transmission failed with id [{}], due to [{}]",
                 transmission_id_, j["reason"].get<std::string>().c_str());
      }
      break;
    }
    case "user_id_list"_H: {
      user_id_list_ = j["user_id_list"];

      std::string transmission_id = j["transmission_id"].get<std::string>();
      std::string status = j["status"].get<std::string>();
      if (status == "failed") {
        std::string reason = j["reason"].get<std::string>();
        LOG_ERROR("{}", reason);
        if ("Incorrect password" == reason) {
          on_connection_status_(ConnectionStatus::IncorrectPassword,
                                user_data_);
        } else if ("No such transmission id" == reason) {
          on_connection_status_(ConnectionStatus::NoSuchTransmissionId,
                                user_data_);
        }
      } else {
        if (leave_) {
          break;
        }

        if (user_id_list_.empty()) {
          LOG_WARN("Wait for host create transmission [{}]", transmission_id);
          std::this_thread::sleep_for(std::chrono::seconds(1));
          RequestTransmissionMemberList(transmission_id, password_);
          break;
        }

        LOG_INFO("Transmission [{}] members: [", transmission_id);
        for (auto user_id : user_id_list_) {
          LOG_INFO("{}", user_id);
        }
        LOG_INFO("]");

        if (0 != CreateVideoCodec(hardware_acceleration_)) {
          LOG_ERROR("Create video codec failed");
        }

        if (0 != CreateAudioCodec()) {
          LOG_ERROR("Create audio codec failed");
        }

        for (auto &remote_user_id : user_id_list_) {
          // if (remote_user_id == user_id_) {
          //   continue;
          // }
          ice_transmission_list_[remote_user_id] =
              std::make_unique<IceTransmission>(
                  trickle_ice_, true, transmission_id, user_id_, remote_user_id,
                  ws_transport_, on_ice_status_change_);

          ice_transmission_list_[remote_user_id]->SetOnReceiveVideoFunc(
              on_receive_video_);
          ice_transmission_list_[remote_user_id]->SetOnReceiveAudioFunc(
              on_receive_audio_);
          ice_transmission_list_[remote_user_id]->SetOnReceiveDataFunc(
              on_receive_data_);
          ice_transmission_list_[remote_user_id]
              ->SetOnReceiveNetStatusReportFunc(on_net_status_report_);

          ice_transmission_list_[remote_user_id]->InitIceTransmission(
              cfg_stun_server_ip_, stun_server_port_, cfg_turn_server_ip_,
              turn_server_port_, cfg_turn_server_username_,
              cfg_turn_server_password_,
              av1_encoding_ ? RtpPacket::AV1 : RtpPacket::H264);
          ice_transmission_list_[remote_user_id]->JoinTransmission();
        }

        // on_connection_status_(ConnectionStatus::Connecting);
      }

      break;
    }
    case "user_leave_transmission"_H: {
      std::string user_id = j["user_id"];
      LOG_INFO("Receive notification: user id [{}] leave transmission",
               user_id);
      auto user_id_it = ice_transmission_list_.find(user_id);
      if (user_id_it != ice_transmission_list_.end()) {
        user_id_it->second->DestroyIceTransmission();
        ice_transmission_list_.erase(user_id_it);
        ice_ready_ = false;
        LOG_INFO("Terminate transmission to user [{}]", user_id);
      }
      break;
    }
    case "offer"_H: {
      std::string transmission_id = j["transmission_id"].get<std::string>();
      std::string remote_user_id = j["remote_user_id"].get<std::string>();
      LOG_INFO("[{}] receive offer from [{}]", user_id_, remote_user_id);

      if (0 != CreateVideoCodec(hardware_acceleration_)) {
        LOG_ERROR("Create video codec failed");
      }

      if (0 != CreateAudioCodec()) {
        LOG_ERROR("Create audio codec failed");
      }

      ice_transmission_list_[remote_user_id] =
          std::make_unique<IceTransmission>(
              trickle_ice_, false, transmission_id, user_id_, remote_user_id,
              ws_transport_, on_ice_status_change_);

      ice_transmission_list_[remote_user_id]->SetOnReceiveVideoFunc(
          on_receive_video_);
      ice_transmission_list_[remote_user_id]->SetOnReceiveAudioFunc(
          on_receive_audio_);
      ice_transmission_list_[remote_user_id]->SetOnReceiveDataFunc(
          on_receive_data_);
      ice_transmission_list_[remote_user_id]->SetOnReceiveNetStatusReportFunc(
          on_net_status_report_);

      ice_transmission_list_[remote_user_id]->InitIceTransmission(
          cfg_stun_server_ip_, stun_server_port_, cfg_turn_server_ip_,
          turn_server_port_, cfg_turn_server_username_,
          cfg_turn_server_password_,
          av1_encoding_ ? RtpPacket::AV1 : RtpPacket::H264);
      ice_transmission_list_[remote_user_id]->SetTransmissionId(
          transmission_id_);

      if (j.contains("sdp")) {
        on_connection_status_(ConnectionStatus::Connecting, user_data_);
        std::string remote_sdp = j["sdp"].get<std::string>();
        ice_transmission_list_[remote_user_id]->SetRemoteSdp(remote_sdp);
        if (trickle_ice_) {
          sdp_without_cands_ = remote_sdp;
          ice_transmission_list_[remote_user_id]->SendAnswer();
        }
        ice_transmission_list_[remote_user_id]->GatherCandidates();
      } else {
        LOG_ERROR("Invalid offer msg");
      }

      break;
    }
    case "answer"_H: {
      on_connection_status_(ConnectionStatus::Connecting, user_data_);
      std::string transmission_id = j["transmission_id"].get<std::string>();
      std::string remote_user_id = j["remote_user_id"].get<std::string>();
      LOG_INFO("[{}] receive answer from [{}]", user_id_, remote_user_id);

      if (j.contains("sdp")) {
        std::string remote_sdp = j["sdp"].get<std::string>();
        if (ice_transmission_list_.find(remote_user_id) !=
            ice_transmission_list_.end()) {
          ice_transmission_list_[remote_user_id]->SetRemoteSdp(remote_sdp);
          if (trickle_ice_) {
            sdp_without_cands_ = remote_sdp;
            ice_transmission_list_[remote_user_id]->GatherCandidates();
          }
        }
      } else {
        LOG_ERROR("Invalid answer msg");
      }

      break;
    }
    case "new_candidate"_H: {
      std::string transmission_id = j["transmission_id"].get<std::string>();
      std::string new_candidate = j["sdp"].get<std::string>();
      std::string remote_user_id = j["remote_user_id"].get<std::string>();

      // LOG_INFO("[{}] receive new candidate from [{}]:[{}]", user_id_,
      //          remote_user_id, new_candidate);

      if (ice_transmission_list_.find(remote_user_id) !=
          ice_transmission_list_.end()) {
        ice_transmission_list_[remote_user_id]->SetRemoteSdp(
            sdp_without_cands_ + new_candidate);
      }
      break;
    }
    default: {
      break;
    }
  }
}

int PeerConnection::RequestTransmissionMemberList(
    const std::string &transmission_id, const std::string &password) {
  if (SignalStatus::SignalConnected != GetSignalStatus()) {
    LOG_ERROR("Signal not connected");
    return -1;
  }

  LOG_INFO("[{}] Request member list", user_id_);

  json message = {{"type", "query_user_id_list"},
                  {"transmission_id", transmission_id_},
                  {"password", password}};

  if (ws_transport_) {
    ws_transport_->Send(message.dump());
  }
  return 0;
}

int PeerConnection::Destroy() {
  if (ws_transport_) {
    ws_transport_->Close(0, "destroy");
  }

  ice_transmission_list_.clear();
  if (nv12_data_) {
    delete nv12_data_;
    nv12_data_ = nullptr;
  }

  if (hardware_acceleration_) {
    ReleaseNvCodecDll();
  }
  return 0;
}

SignalStatus PeerConnection::GetSignalStatus() {
  std::lock_guard<std::mutex> l(signal_status_mutex_);
  return signal_status_;
}

int PeerConnection::SendVideoData(const char *data, size_t size) {
  if (!ice_ready_) {
    return -1;
  }

  if (ice_transmission_list_.empty()) {
    return -1;
  }

  if (b_force_i_frame_) {
    video_encoder_->ForceIdr();
    LOG_INFO("Force I frame");
    b_force_i_frame_ = false;
  }

  int ret = video_encoder_->Encode(
      (uint8_t *)data, size,
      [this](char *encoded_frame, size_t size,
             VideoEncoder::VideoFrameType frame_type) -> int {
        for (auto &ice_trans : ice_transmission_list_) {
          // LOG_ERROR("Send frame size: [{}]", size);
          // ice_trans.second->SendData(IceTransmission::DATA_TYPE::VIDEO,
          //                            encoded_frame, size);
          ice_trans.second->SendVideoData(
              static_cast<IceTransmission::VideoFrameType>(frame_type),
              encoded_frame, size);
        }
        return 0;
      });

  if (0 != ret) {
    LOG_ERROR("Encode failed");
    return -1;
  }

  return 0;
}

int PeerConnection::SendAudioData(const char *data, size_t size) {
  if (!ice_ready_) {
    return -1;
  }

  if (ice_transmission_list_.empty()) {
    return -1;
  }

  int ret = audio_encoder_->Encode(
      (uint8_t *)data, size,
      [this](char *encoded_audio_buffer, size_t size) -> int {
        for (auto &ice_trans : ice_transmission_list_) {
          // LOG_ERROR("opus frame size: [{}]", size);
          ice_trans.second->SendData(IceTransmission::DATA_TYPE::AUDIO,
                                     encoded_audio_buffer, size);
        }
        return 0;
      });

  return 0;
}

int PeerConnection::SendUserData(const char *data, size_t size) {
  for (auto &ice_trans : ice_transmission_list_) {
    ice_trans.second->SendData(IceTransmission::DATA_TYPE::DATA, data, size);
  }
  return 0;
}