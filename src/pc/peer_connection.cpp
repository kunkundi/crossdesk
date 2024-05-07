#include "peer_connection.h"

#include <regex>

#include "INIReader.h"
#include "common.h"
#include "log.h"
#include "nlohmann/json.hpp"

using nlohmann::json;

PeerConnection::PeerConnection() {}

PeerConnection::~PeerConnection() {
  if (nv12_data_) {
    delete nv12_data_;
    nv12_data_ = nullptr;
  }
}

int PeerConnection::Init(PeerConnectionParams params,
                         const std::string &user_id) {
  if (inited_) {
    LOG_INFO("Peer already inited");
    return 0;
  }
  // Todo: checkout user_id unique or not
  user_id_ = user_id;

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

  LOG_INFO("Read config success");

  signal_server_port_ = stoi(cfg_signal_server_port_);
  stun_server_port_ = stoi(cfg_stun_server_port_);
  turn_server_port_ = stoi(cfg_turn_server_port_);

  LOG_INFO("stun server ip [{}] port [{}]", cfg_stun_server_ip_,
           stun_server_port_);

  if (!cfg_turn_server_ip_.empty() && 0 != turn_server_port_ &&
      !cfg_turn_server_username_.empty() &&
      !cfg_turn_server_password_.empty()) {
    LOG_INFO("turn server ip [{}] port [{}] username [{}] password [{}]",
             cfg_turn_server_ip_, turn_server_port_, cfg_turn_server_username_,
             cfg_turn_server_password_);
  }

  hardware_acceleration_ = cfg_hardware_acceleration_ == "true" ? true : false;
  LOG_INFO("Hardware accelerated codec [{}]",
           hardware_acceleration_ ? "ON" : "OFF");

  av1_encoding_ = cfg_av1_encoding_ == "true" ? true : false;
  LOG_INFO("av1 encoding [{}]", av1_encoding_ ? "ON" : "OFF");

  on_receive_video_buffer_ = params.on_receive_video_buffer;
  on_receive_audio_buffer_ = params.on_receive_audio_buffer;
  on_receive_data_buffer_ = params.on_receive_data_buffer;
  on_signal_status_ = params.on_signal_status;
  on_connection_status_ = params.on_connection_status;

  on_receive_ws_msg_ = [this](const std::string &msg) { ProcessSignal(msg); };

  on_ws_status_ = [this](WsStatus ws_status) {
    if (WsStatus::WsOpening == ws_status) {
      signal_status_ = SignalStatus::SignalConnecting;
      on_signal_status_(SignalStatus::SignalConnecting);
    } else if (WsStatus::WsOpened == ws_status) {
      signal_status_ = SignalStatus::SignalConnected;
      on_signal_status_(SignalStatus::SignalConnected);
    } else if (WsStatus::WsFailed == ws_status) {
      signal_status_ = SignalStatus::SignalFailed;
      on_signal_status_(SignalStatus::SignalFailed);
    } else if (WsStatus::WsClosed == ws_status) {
      signal_status_ = SignalStatus::SignalClosed;
      on_signal_status_(SignalStatus::SignalClosed);
    } else if (WsStatus::WsReconnecting == ws_status) {
      signal_status_ = SignalStatus::SignalReconnecting;
      on_signal_status_(SignalStatus::SignalReconnecting);
    }
  };

  on_receive_video_ = [this](const char *data, size_t size, const char *user_id,
                             size_t user_id_size) {
    int num_frame_returned = video_decoder_->Decode(
        (uint8_t *)data, size,
        [this, user_id, user_id_size](VideoFrame video_frame) {
          if (on_receive_video_buffer_) {
            LOG_ERROR("Receive video, size {}", video_frame.Size());
            on_receive_video_buffer_((const char *)video_frame.Buffer(),
                                     video_frame.Size(), user_id, user_id_size);
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
                                     user_id_size);
          }
        });
  };

  on_receive_data_ = [this](const char *data, size_t size, const char *user_id,
                            size_t user_id_size) {
    if (on_receive_data_buffer_) {
      on_receive_data_buffer_(data, size, user_id, user_id_size);
    }
  };

  on_ice_status_change_ = [this](std::string ice_status) {
    if ("connecting" == ice_status) {
      on_connection_status_(ConnectionStatus::Connecting);
    } else if ("disconnected" == ice_status) {
      on_connection_status_(ConnectionStatus::Disconnected);
    } else if ("ready" == ice_status) {
      ice_ready_ = true;
      on_connection_status_(ConnectionStatus::Connected);
      b_force_i_frame_ = true;
      LOG_INFO("Ice finish");
    } else if ("closed" == ice_status) {
      ice_ready_ = false;
      on_connection_status_(ConnectionStatus::Closed);
      LOG_INFO("Ice closed");
    } else {
      ice_ready_ = false;
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

  if (0 != CreateVideoCodec(hardware_acceleration_)) {
    LOG_ERROR("Create video codec failed");
    return -1;
  }

  if (0 != CreateAudioCodec()) {
    LOG_ERROR("Create audio codec failed");
    return -1;
  }

  inited_ = true;
  return 0;
}

int PeerConnection::CreateVideoCodec(bool hardware_acceleration) {
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
    LOG_INFO("Only support software encoding for AV1");
  } else {
    video_encoder_ =
        VideoEncoderFactory::CreateVideoEncoder(hardware_acceleration_, false);
    if (hardware_acceleration_ && !video_encoder_) {
      LOG_WARN(
          "Hardware accelerated encoder not available, use default software "
          "encoder");
      video_encoder_ = VideoEncoderFactory::CreateVideoEncoder(false, false);
      if (!video_encoder_) {
        LOG_ERROR(
            "Hardware accelerated encoder and software encoder both not "
            "available");
        return -1;
      }
    }
  }
  if (0 != video_encoder_->Init()) {
    video_encoder_ = VideoEncoderFactory::CreateVideoEncoder(false, false);
    if (!video_encoder_ || 0 != video_encoder_->Init()) {
      LOG_ERROR("Encoder init failed");
      return -1;
    }
  }

  if (av1_encoding_) {
    video_decoder_ = VideoDecoderFactory::CreateVideoDecoder(false, true);
    LOG_INFO("Only support software decoding for AV1");
  } else {
    video_decoder_ =
        VideoDecoderFactory::CreateVideoDecoder(hardware_acceleration_, false);
    if (hardware_acceleration_ && !video_decoder_) {
      LOG_WARN(
          "Hardware accelerated decoder not available, use default software "
          "decoder");
      video_decoder_ = VideoDecoderFactory::CreateVideoDecoder(false, false);
      if (!video_decoder_) {
        LOG_ERROR(
            "Hardware accelerated decoder and software decoder both not "
            "available");
        return -1;
      }
    }
  }
  if (0 != video_decoder_->Init()) {
    video_decoder_ = VideoDecoderFactory::CreateVideoDecoder(false, false);
    if (!video_decoder_ || video_decoder_->Init()) {
      LOG_ERROR("Decoder init failed");
      return -1;
    }
  }
  return 0;
}

int PeerConnection::CreateAudioCodec() {
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

  return 0;
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

int PeerConnection::Leave() {
  if (SignalStatus::SignalConnected != GetSignalStatus()) {
    LOG_ERROR("Signal not connected");
    return -1;
  }

  json message = {{"type", "leave_transmission"},
                  {"user_id", user_id_},
                  {"transmission_id", transmission_id_}};
  if (ws_transport_) {
    ws_transport_->Send(message.dump());
    LOG_INFO("[{}] sends leave transmission [{}] notification ", user_id_,
             transmission_id_);
  }

  ice_ready_ = false;
  leave_ = true;

  for (auto &user_id_it : ice_transmission_list_) {
    user_id_it.second->DestroyIceTransmission();
  }

  return 0;
}

void PeerConnection::ProcessSignal(const std::string &signal) {
  auto j = json::parse(signal);
  std::string type = j["type"];
  LOG_INFO("signal type: {}", type);
  switch (HASH_STRING_PIECE(type.c_str())) {
    case "ws_connection_id"_H: {
      ws_connection_id_ = j["ws_connection_id"].get<unsigned int>();
      LOG_INFO("Receive local peer websocket connection id [{}]",
               ws_connection_id_);
      break;
    }
    case "transmission_id"_H: {
      if (j["status"].get<std::string>() == "success") {
        transmission_id_ = j["transmission_id"].get<std::string>();
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
          on_connection_status_(ConnectionStatus::IncorrectPassword);
        } else if ("No such transmission id" == reason) {
          on_connection_status_(ConnectionStatus::NoSuchTransmissionId);
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

        for (auto &remote_user_id : user_id_list_) {
          if (remote_user_id == user_id_) {
            continue;
          }
          ice_transmission_list_[remote_user_id] =
              std::make_unique<IceTransmission>(true, transmission_id, user_id_,
                                                remote_user_id, ws_transport_,
                                                on_ice_status_change_);

          ice_transmission_list_[remote_user_id]->SetOnReceiveVideoFunc(
              on_receive_video_);
          ice_transmission_list_[remote_user_id]->SetOnReceiveAudioFunc(
              on_receive_audio_);
          ice_transmission_list_[remote_user_id]->SetOnReceiveDataFunc(
              on_receive_data_);

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

        if (std::string::npos != user_id.find("S-")) {
          LOG_INFO("Server leaves, try to rejoin transmission");

          RequestTransmissionMemberList(transmission_id_, password_);
        }
      }
      break;
    }
    case "offer"_H: {
      std::string remote_sdp = j["sdp"].get<std::string>();

      if (remote_sdp.empty()) {
        LOG_INFO("Invalid remote sdp");
      } else {
        std::string transmission_id = j["transmission_id"].get<std::string>();
        std::string sdp = j["sdp"].get<std::string>();
        std::string remote_user_id = j["remote_user_id"].get<std::string>();
        LOG_INFO("[{}] receive offer from [{}]", user_id_, remote_user_id);

        ice_transmission_list_[remote_user_id] =
            std::make_unique<IceTransmission>(false, transmission_id, user_id_,
                                              remote_user_id, ws_transport_,
                                              on_ice_status_change_);

        ice_transmission_list_[remote_user_id]->SetOnReceiveVideoFunc(
            on_receive_video_);
        ice_transmission_list_[remote_user_id]->SetOnReceiveAudioFunc(
            on_receive_audio_);
        ice_transmission_list_[remote_user_id]->SetOnReceiveDataFunc(
            on_receive_data_);

        ice_transmission_list_[remote_user_id]->InitIceTransmission(
            cfg_stun_server_ip_, stun_server_port_, cfg_turn_server_ip_,
            turn_server_port_, cfg_turn_server_username_,
            cfg_turn_server_password_,
            av1_encoding_ ? RtpPacket::AV1 : RtpPacket::H264);

        ice_transmission_list_[remote_user_id]->SetTransmissionId(
            transmission_id_);

        ice_transmission_list_[remote_user_id]->SetRemoteSdp(remote_sdp);

        ice_transmission_list_[remote_user_id]->GatherCandidates();

        on_connection_status_(ConnectionStatus::Connecting);
      }
      break;
    }
    case "answer"_H: {
      std::string remote_sdp = j["sdp"].get<std::string>();
      if (remote_sdp.empty()) {
        LOG_INFO("remote_sdp is empty");
      } else {
        std::string transmission_id = j["transmission_id"].get<std::string>();
        std::string sdp = j["sdp"].get<std::string>();
        std::string remote_user_id = j["remote_user_id"].get<std::string>();

        LOG_INFO("[{}] receive answer from [{}]", user_id_, remote_user_id);

        if (ice_transmission_list_.find(remote_user_id) !=
            ice_transmission_list_.end()) {
          ice_transmission_list_[remote_user_id]->SetRemoteSdp(remote_sdp);
        }

        on_connection_status_(ConnectionStatus::Connecting);
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

  LOG_INFO("Request member list");

  json message = {{"type", "query_user_id_list"},
                  {"transmission_id", transmission_id_},
                  {"password", password}};

  if (ws_transport_) {
    ws_transport_->Send(message.dump());
  }
  return 0;
}

int PeerConnection::Destroy() { return 0; }

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
          LOG_ERROR("Send frame size: [{}]", size);
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