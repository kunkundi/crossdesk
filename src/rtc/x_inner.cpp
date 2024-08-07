#include "x_inner.h"

#include <iostream>
#include <nlohmann/json.hpp>

#include "ice_agent.h"
#include "log.h"
#include "ws_transmission.h"
#include "x.h"

using nlohmann::json;

PeerPtr *CreatePeer(const Params *params) {
  PeerPtr *peer_ptr = new PeerPtr;
  peer_ptr->peer_connection = new PeerConnection();
  peer_ptr->pc_params.use_cfg_file = params->use_cfg_file;
  if (params->use_cfg_file) {
    peer_ptr->pc_params.cfg_path = params->cfg_path;
  } else {
    peer_ptr->pc_params.signal_server_ip = params->signal_server_ip;
    peer_ptr->pc_params.signal_server_port = params->signal_server_port;
    peer_ptr->pc_params.stun_server_ip = params->stun_server_ip;
    peer_ptr->pc_params.stun_server_port = params->stun_server_port;
    peer_ptr->pc_params.turn_server_ip = params->turn_server_ip;
    peer_ptr->pc_params.turn_server_port = params->turn_server_port;
    peer_ptr->pc_params.turn_server_username = params->turn_server_username;
    peer_ptr->pc_params.turn_server_password = params->turn_server_password;
    peer_ptr->pc_params.hardware_acceleration = params->hardware_acceleration;
    peer_ptr->pc_params.av1_encoding = params->av1_encoding;
  }
  peer_ptr->pc_params.on_receive_video_buffer = params->on_receive_video_buffer;
  peer_ptr->pc_params.on_receive_audio_buffer = params->on_receive_audio_buffer;
  peer_ptr->pc_params.on_receive_data_buffer = params->on_receive_data_buffer;
  peer_ptr->pc_params.on_signal_status = params->on_signal_status;
  peer_ptr->pc_params.on_connection_status = params->on_connection_status;
  peer_ptr->pc_params.net_status_report = params->net_status_report;
  peer_ptr->pc_params.user_data = params->user_data;

  return peer_ptr;
}

void DestroyPeer(PeerPtr *peer_ptr) {
  peer_ptr->peer_connection->Destroy();
  delete peer_ptr;
  peer_ptr = nullptr;
}

int Init(PeerPtr *peer_ptr, const char *user_id) {
  if (!peer_ptr) {
    LOG_ERROR("peer_ptr not created");
    return -1;
  }

  peer_ptr->peer_connection->Init(peer_ptr->pc_params, user_id);
  return 0;
}

int CreateConnection(PeerPtr *peer_ptr, const char *transmission_id,
                     const char *password) {
  if (!peer_ptr) {
    LOG_ERROR("peer_ptr not created");
    return -1;
  }

  LOG_INFO("CreateConnection [{}] with password [{}]", transmission_id,
           password);

  return peer_ptr->peer_connection->Create(transmission_id, password);
}

int JoinConnection(PeerPtr *peer_ptr, const char *transmission_id,
                   const char *password) {
  int ret = -1;
  if (!peer_ptr) {
    LOG_ERROR("peer_ptr not created");
    return -1;
  }

  ret = peer_ptr->peer_connection->Join(transmission_id, password);
  LOG_INFO("JoinConnection [{}] with password [{}]", transmission_id, password);
  return ret;
}

int LeaveConnection(PeerPtr *peer_ptr, const char *transmission_id) {
  if (!peer_ptr) {
    LOG_ERROR("peer_ptr not created");
    return -1;
  }

  peer_ptr->peer_connection->Leave(transmission_id);
  LOG_INFO("LeaveConnection");
  return 0;
}

int SendData(PeerPtr *peer_ptr, DATA_TYPE data_type, const char *data,
             size_t size) {
  if (!peer_ptr) {
    LOG_ERROR("peer_ptr not created");
    return -1;
  }

  if (DATA_TYPE::VIDEO == data_type) {
    peer_ptr->peer_connection->SendVideoData(data, size);
  } else if (DATA_TYPE::AUDIO == data_type) {
    peer_ptr->peer_connection->SendAudioData(data, size);
  } else if (DATA_TYPE::DATA == data_type) {
    peer_ptr->peer_connection->SendUserData(data, size);
  }
  return 0;
}