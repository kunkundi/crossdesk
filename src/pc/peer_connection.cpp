#include "peer_connection.h"

#include <regex>

#include "INIReader.h"
#include "common.h"
#include "log.h"
#include "nlohmann/json.hpp"

using nlohmann::json;

PeerConnection::PeerConnection() {}

PeerConnection::~PeerConnection() { user_data_ = nullptr; }

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
    cfg_enable_turn_ = reader.Get("enable turn", "turn_on", "false");

    std::regex regex("\n");

    signal_server_port_ = stoi(cfg_signal_server_port_);
    stun_server_port_ = stoi(cfg_stun_server_port_);
    turn_server_port_ = stoi(cfg_turn_server_port_);

    hardware_acceleration_ =
        cfg_hardware_acceleration_ == "true" ? true : false;
    av1_encoding_ = cfg_av1_encoding_ == "true" ? true : false;
    enable_turn_ = cfg_enable_turn_ == "true" ? true : false;

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
    enable_turn_ = params.enable_turn;

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

  on_receive_video_frame_ = params.on_receive_video_frame;

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
    } else if (WsStatus::WsServerClosed == ws_status) {
      ws_status_ = WsStatus::WsServerClosed;
      signal_status_ = SignalStatus::SignalServerClosed;
      on_signal_status_(SignalStatus::SignalServerClosed, user_data_);
    }
  };

  on_ice_status_change_ = [this](std::string ice_status,
                                 const std::string &user_id) {
    if ("connecting" == ice_status) {
      on_connection_status_(ConnectionStatus::Connecting, user_id.data(),
                            user_id.size(), user_data_);
    } else if ("gathering" == ice_status) {
      on_connection_status_(ConnectionStatus::Gathering, user_id.data(),
                            user_id.size(), user_data_);
    } else if ("disconnected" == ice_status) {
      on_connection_status_(ConnectionStatus::Disconnected, user_id.data(),
                            user_id.size(), user_data_);
    } else if ("connected" == ice_status) {
      // std::string transmission_id = std::string(user_id, user_id_size);
      // is_ice_transport_ready_[user_id] = true;
      // on_connection_status_(ConnectionStatus::Connected, user_id.data(),
      //                       user_id.size(), user_data_);
      // b_force_i_frame_ = true;
      LOG_INFO("Ice connected");
    } else if ("ready" == ice_status) {
      is_ice_transport_ready_[user_id] = true;
      b_force_i_frame_ = true;
      LOG_INFO("Ice ready");
      on_connection_status_(ConnectionStatus::Connected, user_id.data(),
                            user_id.size(), user_data_);
    } else if ("closed" == ice_status) {
      is_ice_transport_ready_[user_id] = false;
      LOG_INFO("Ice closed");
      on_connection_status_(ConnectionStatus::Closed, user_id.data(),
                            user_id.size(), user_data_);
    } else if ("failed" == ice_status) {
      is_ice_transport_ready_[user_id] = false;
      if (offer_peer_ && try_rejoin_with_turn_) {
        enable_turn_ = true;
        reliable_ice_ = false;
        LOG_INFO(
            "Ice failed, destroy ice agent and rereate it with TURN enabled");

        ReleaseAllIceTransmission();

        if (offer_peer_) {
          IceWorkMsg msg;
          msg.type = IceWorkMsg::Type::UserIdList;
          msg.transmission_id = remote_transmission_id_;
          msg.user_id_list = user_id_list_;
          PushIceWorkMsg(msg);
        }
      } else {
        LOG_INFO("Ice failed");
        on_connection_status_(ConnectionStatus::Failed, user_id.data(),
                              user_id.size(), user_data_);
      }
    } else {
      is_ice_transport_ready_[user_id] = false;
      LOG_INFO("Unknown ice state [{}]", ice_status);
    }
  };

  clock_ = std::make_shared<SystemClock>();
  ws_transport_ = std::make_shared<WsClient>(on_receive_ws_msg_, on_ws_status_);
  uri_ = "ws://" + cfg_signal_server_ip_ + ":" + cfg_signal_server_port_;
  if (ws_transport_) {
    ws_transport_->Connect(uri_);
  }

  StartIceWorker();

  // do {
  // } while (SignalStatus::SignalConnected != GetSignalStatus());

  LOG_INFO("[{}] Init finish", user_id);

  inited_ = true;
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

  local_transmission_id_ = transmission_id;
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

  offer_peer_ = true;
  password_ = password;
  leave_ = false;

  remote_transmission_id_ = transmission_id;
  ret = RequestTransmissionMemberList(remote_transmission_id_, password_);
  return ret;
}

int PeerConnection::NegotiationFailed() {
  if (SignalStatus::SignalConnected != GetSignalStatus()) {
    LOG_ERROR("Signal not connected");
    return -1;
  }

  json message = {{"type", "negotiation_failed"},
                  {"user_id", user_id_},
                  {"transmission_id", local_transmission_id_}};
  if (ws_transport_) {
    ws_transport_->Send(message.dump());
    LOG_INFO(
        "[{}] sends negotiation failed notification to [{}] for transmission "
        "id [{}]",
        user_id_, remote_user_id_, local_transmission_id_);
  }

  ReleaseAllIceTransmission();

  return 0;
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

  is_ice_transport_ready_[user_id_] = false;
  leave_ = true;

  ReleaseAllIceTransmission();
  return 0;
}

int PeerConnection::ReleaseAllIceTransmission() {
  for (auto &user_id_it : ice_transport_list_) {
    user_id_it.second->DestroyIceTransmission();
  }
  ice_transport_list_.clear();
  is_ice_transport_ready_.clear();
  return 0;
}

int PeerConnection::Destroy() {
  StopIceWorker();
  if (ws_transport_) {
    LOG_INFO("Close websocket")
    ws_transport_->Close();
  }

  return 0;
}

int PeerConnection::RequestTransmissionMemberList(
    const std::string &transmission_id, const std::string &password) {
  if (SignalStatus::SignalConnected != GetSignalStatus()) {
    LOG_ERROR("Signal not connected");
    return -1;
  }

  LOG_INFO("[{}] Request member list", user_id_);

  json message = {{"type", "query_user_id_list"},
                  {"transmission_id", transmission_id},
                  {"password", password}};

  if (ws_transport_) {
    ws_transport_->Send(message.dump());
  }
  return 0;
}

SignalStatus PeerConnection::GetSignalStatus() {
  std::lock_guard<std::mutex> l(signal_status_mutex_);
  return signal_status_;
}

int PeerConnection::SendVideoFrame(const XVideoFrame *video_frame) {
  if (ice_transport_list_.empty()) {
    return -1;
  }

  for (auto &ice_trans : ice_transport_list_) {
    if (!is_ice_transport_ready_[ice_trans.first]) {
      continue;
    }

    ice_trans.second->SendVideoFrame(video_frame);
  }

  return 0;
}

int PeerConnection::SendAudioFrame(const char *data, size_t size) {
  if (ice_transport_list_.empty()) {
    return -1;
  }

  for (auto &ice_trans : ice_transport_list_) {
    if (!is_ice_transport_ready_[ice_trans.first]) {
      continue;
    }
    ice_trans.second->SendAudioFrame(data, size);
  }

  return 0;
}

int PeerConnection::SendDataFrame(const char *data, size_t size) {
  for (auto &ice_trans : ice_transport_list_) {
    if (!is_ice_transport_ready_[ice_trans.first]) {
      continue;
    }
    ice_trans.second->SendDataFrame(data, size);
  }
  return 0;
}

int64_t PeerConnection::GetSystemTimeMicros() {
  if (clock_) {
    return clock_->CurrentTimeUs();
  }
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

        XNetTrafficStats net_traffic_stats;
        memset(&net_traffic_stats, 0, sizeof(net_traffic_stats));

        net_status_report_(user_id_.data(), user_id_.size(),
                           TraversalMode::UnknownMode, &net_traffic_stats,
                           user_id_.data(), user_id_.size(), user_data_);
        LOG_INFO("Login success with id [{}]", user_id_);
        signal_status_ = SignalStatus::SignalConnected;
        on_signal_status_(SignalStatus::SignalConnected, user_data_);
      } else if (j["status"].get<std::string>() == "fail") {
        LOG_WARN("Login failed with id [{}]", user_id_);
      }
      break;
    }
    case "transmission_id"_H: {
      if (j["status"].get<std::string>() == "success") {
        local_transmission_id_ = j["transmission_id"].get<std::string>();
        user_id_ = local_transmission_id_;
        LOG_INFO("Create transmission success with id [{}]",
                 local_transmission_id_);
      } else if (j["status"].get<std::string>() == "fail") {
        LOG_WARN("Create transmission failed with id [{}], due to [{}]",
                 local_transmission_id_,
                 j["reason"].get<std::string>().c_str());
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
                                transmission_id.data(), transmission_id.size(),
                                user_data_);
        } else if ("No such transmission id" == reason) {
          on_connection_status_(ConnectionStatus::NoSuchTransmissionId,
                                transmission_id.data(), transmission_id.size(),
                                user_data_);
        }
      } else {
        if (leave_) {
          break;
        }

        IceWorkMsg msg;
        msg.type = IceWorkMsg::Type::UserIdList;
        msg.transmission_id = transmission_id;
        msg.user_id_list = user_id_list_;
        PushIceWorkMsg(msg);
      }

      break;
    }
    case "user_leave_transmission"_H: {
      std::string user_id = j["user_id"];
      IceWorkMsg msg;
      msg.type = IceWorkMsg::Type::UserLeaveTransmission;
      msg.user_id = user_id;
      PushIceWorkMsg(msg);

      break;
    }
    case "offer"_H: {
      std::string transmission_id = j["transmission_id"].get<std::string>();
      std::string remote_user_id = j["remote_user_id"].get<std::string>();
      remote_user_id_ = remote_user_id;

      if (j.contains("sdp")) {
        std::string remote_sdp = j["sdp"].get<std::string>();
        LOG_INFO("[{}] receive offer from [{}]", user_id_, remote_user_id);

        IceWorkMsg msg;
        msg.type = IceWorkMsg::Type::Offer;
        msg.transmission_id = transmission_id;
        msg.remote_user_id = remote_user_id;
        msg.remote_sdp = remote_sdp;
        PushIceWorkMsg(msg);
        on_connection_status_(ConnectionStatus::Connecting,
                              remote_user_id.data(), remote_user_id.size(),
                              user_data_);
      } else {
        LOG_ERROR("Invalid offer msg");
      }

      break;
    }
    case "answer"_H: {
      std::string transmission_id = j["transmission_id"].get<std::string>();
      std::string remote_user_id = j["remote_user_id"].get<std::string>();
      remote_user_id_ = remote_user_id;

      if (j.contains("sdp")) {
        std::string remote_sdp = j["sdp"].get<std::string>();
        LOG_INFO("[{}] receive answer from [{}]", user_id_, remote_user_id);

        IceWorkMsg msg;
        msg.type = IceWorkMsg::Type::Answer;
        msg.transmission_id = transmission_id;
        msg.remote_user_id = remote_user_id;
        msg.remote_sdp = remote_sdp;
        PushIceWorkMsg(msg);
        on_connection_status_(ConnectionStatus::Connecting,
                              remote_user_id.data(), remote_user_id.size(),
                              user_data_);
      } else {
        LOG_ERROR("Invalid answer msg");
      }

      break;
    }
    case "new_candidate"_H: {
      std::string transmission_id = j["transmission_id"].get<std::string>();
      std::string new_candidate = j["sdp"].get<std::string>();
      std::string remote_user_id = j["remote_user_id"].get<std::string>();

      IceWorkMsg msg;
      msg.type = IceWorkMsg::Type::NewCandidate;
      msg.transmission_id = transmission_id;
      msg.remote_user_id = remote_user_id;
      msg.new_candidate = new_candidate;
      PushIceWorkMsg(msg);

      break;
    }
    default: {
      break;
    }
  }
}

void PeerConnection::StartIceWorker() {
  ice_worker_ = std::thread([this]() {
    while (true) {
      std::unique_lock<std::mutex> lck(ice_work_mutex_);
      while (ice_work_msg_queue_.empty() && ice_worker_running_) {
        ice_work_cv_.wait(lck, [this] {
          return !ice_work_msg_queue_.empty() || !ice_worker_running_;
        });
      }

      if (!ice_worker_running_) {
        break;
      }

      IceWorkMsg msg = ice_work_msg_queue_.front();
      ice_work_msg_queue_.pop();
      lck.unlock();
      ProcessIceWorkMsg(msg);
    }
    std::queue<IceWorkMsg> empty_queue;
    std::swap(ice_work_msg_queue_, empty_queue);
  });
}

void PeerConnection::StopIceWorker() {
  ice_worker_running_ = false;
  ice_work_cv_.notify_one();
  if (ice_worker_.joinable()) {
    ice_worker_.join();
  }
}

void PeerConnection::PushIceWorkMsg(const IceWorkMsg &msg) {
  std::lock_guard<std::mutex> lck(ice_work_mutex_);
  ice_work_msg_queue_.push(msg);
  ice_work_cv_.notify_one();
}

void PeerConnection::ProcessIceWorkMsg(const IceWorkMsg &msg) {
  switch (msg.type) {
    case IceWorkMsg::Type::Login: {
      break;
    }
    case IceWorkMsg::Type::UserIdList: {
      std::vector<std::string> user_id_list = msg.user_id_list;
      std::string transmission_id = msg.transmission_id;

      if (user_id_list.empty()) {
        LOG_WARN("Wait for host create transmission [{}]", transmission_id);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        RequestTransmissionMemberList(transmission_id, password_);
        break;
      }

      LOG_INFO("Transmission [{}] members: [", transmission_id);
      for (auto user_id : user_id_list) {
        LOG_INFO("{}", user_id);
      }
      LOG_INFO("]");

      for (auto &remote_user_id : user_id_list) {
        ice_transport_list_[remote_user_id] = std::make_shared<IceTransport>(
            clock_, true, transmission_id, user_id_, remote_user_id,
            ws_transport_, on_ice_status_change_, user_data_);

        ice_transport_list_[remote_user_id]->SetLocalCapabilities(
            hardware_acceleration_, trickle_ice_, reliable_ice_, enable_turn_,
            false, video_payload_types_, audio_payload_types_);

        ice_transport_list_[remote_user_id]->SetOnReceiveFunc(
            on_receive_video_frame_, on_receive_audio_buffer_,
            on_receive_data_buffer_);

        ice_transport_list_[remote_user_id]->SetOnReceiveNetStatusReportFunc(
            net_status_report_);

        ice_transport_list_[remote_user_id]->InitIceTransmission(
            cfg_stun_server_ip_, stun_server_port_, cfg_turn_server_ip_,
            turn_server_port_, cfg_turn_server_username_,
            cfg_turn_server_password_,
            av1_encoding_ ? rtp::PAYLOAD_TYPE::AV1 : rtp::PAYLOAD_TYPE::H264);
        ice_transport_list_[remote_user_id]->JoinTransmission();
      }

      break;
    }
    case IceWorkMsg::Type::UserLeaveTransmission: {
      std::string user_id = msg.user_id;
      LOG_INFO("[{}] Receive notification: user id [{}] leave transmission",
               (void *)this, user_id);
      auto user_id_it = ice_transport_list_.find(user_id);
      if (user_id_it != ice_transport_list_.end()) {
        user_id_it->second->DestroyIceTransmission();
        ice_transport_list_.erase(user_id_it);
        is_ice_transport_ready_[user_id] = false;
        LOG_INFO("Terminate transmission to user [{}]", user_id);
      }
      break;
    }
    case IceWorkMsg::Type::Offer: {
      std::string transmission_id = msg.transmission_id;
      std::string remote_user_id = msg.remote_user_id;
      if (ice_transport_list_.end() ==
          ice_transport_list_.find(remote_user_id)) {
        // Enable TURN for answer peer by default
        ice_transport_list_[remote_user_id] = std::make_shared<IceTransport>(
            clock_, false, transmission_id, user_id_, remote_user_id,
            ws_transport_, on_ice_status_change_, user_data_);

        ice_transport_list_[remote_user_id]->SetLocalCapabilities(
            hardware_acceleration_, trickle_ice_, reliable_ice_, enable_turn_,
            false, video_payload_types_, audio_payload_types_);

        ice_transport_list_[remote_user_id]->SetOnReceiveFunc(
            on_receive_video_frame_, on_receive_audio_buffer_,
            on_receive_data_buffer_);

        ice_transport_list_[remote_user_id]->SetOnReceiveNetStatusReportFunc(
            net_status_report_);

        ice_transport_list_[remote_user_id]->InitIceTransmission(
            cfg_stun_server_ip_, stun_server_port_, cfg_turn_server_ip_,
            turn_server_port_, cfg_turn_server_username_,
            cfg_turn_server_password_,
            av1_encoding_ ? rtp::PAYLOAD_TYPE::AV1 : rtp::PAYLOAD_TYPE::H264);
        ice_transport_list_[remote_user_id]->SetTransmissionId(transmission_id);
      }

      std::string remote_sdp = msg.remote_sdp;
      int ret = ice_transport_list_[remote_user_id]->SetRemoteSdp(remote_sdp);
      if (0 != ret) {
        NegotiationFailed();
        break;
      }

      if (trickle_ice_) {
        sdp_without_cands_ = remote_sdp;
        ice_transport_list_[remote_user_id]->SendAnswer();
      }
      ice_transport_list_[remote_user_id]->GatherCandidates();

      break;
    }
    case IceWorkMsg::Type::Answer: {
      std::string remote_user_id = msg.remote_user_id;
      std::string remote_sdp = msg.remote_sdp;
      if (ice_transport_list_.find(remote_user_id) !=
          ice_transport_list_.end()) {
        int ret = ice_transport_list_[remote_user_id]->SetRemoteSdp(remote_sdp);
        if (0 != ret) {
          Leave(remote_transmission_id_);
          break;
        }

        if (trickle_ice_) {
          sdp_without_cands_ = remote_sdp;
          ice_transport_list_[remote_user_id]->GatherCandidates();
        }
      }

      break;
    }
    case IceWorkMsg::Type::NewCandidate: {
      std::string transmission_id = msg.transmission_id;
      std::string new_candidate = msg.new_candidate;
      std::string remote_user_id = msg.remote_user_id;

      // LOG_INFO("[{}] receive new candidate from [{}]:[{}]", user_id_,
      //          remote_user_id, new_candidate);

      if (ice_transport_list_.find(remote_user_id) !=
          ice_transport_list_.end()) {
        ice_transport_list_[remote_user_id]->SetRemoteSdp(sdp_without_cands_ +
                                                          new_candidate);
      }
      break;
    }
    default: {
      break;
    }
  }
}