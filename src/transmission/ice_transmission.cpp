#include "ice_transmission.h"

#include <chrono>
#include <map>
#include <nlohmann/json.hpp>
#include <thread>

#include "common.h"
#include "ikcp.h"
#include "log.h"

using nlohmann::json;

IceTransmission::IceTransmission(
    bool trickle_ice, bool offer_peer, std::string &transmission_id,
    std::string &user_id, std::string &remote_user_id,
    std::shared_ptr<WsTransmission> ice_ws_transmission,
    std::function<void(std::string)> on_ice_status_change)
    : trickle_ice_(trickle_ice),
      offer_peer_(offer_peer),
      transmission_id_(transmission_id),
      user_id_(user_id),
      remote_user_id_(remote_user_id),
      ice_ws_transport_(ice_ws_transmission),
      on_ice_status_change_(on_ice_status_change) {}

IceTransmission::~IceTransmission() {
  if (rtp_video_sender_) {
    rtp_video_sender_->Stop();
  }

  if (rtp_audio_sender_) {
    rtp_audio_sender_->Stop();
  }

  if (rtp_data_sender_) {
    rtp_data_sender_->Stop();
  }
}

int IceTransmission::InitIceTransmission(
    std::string &stun_ip, int stun_port, std::string &turn_ip, int turn_port,
    std::string &turn_username, std::string &turn_password,
    RtpPacket::PAYLOAD_TYPE video_codec_payload_type) {
  video_rtp_codec_ = std::make_unique<RtpCodec>(video_codec_payload_type);
  audio_rtp_codec_ = std::make_unique<RtpCodec>(RtpPacket::PAYLOAD_TYPE::OPUS);
  data_rtp_codec_ = std::make_unique<RtpCodec>(RtpPacket::PAYLOAD_TYPE::DATA);

  rtp_video_receiver_ = std::make_unique<RtpVideoReceiver>();
  rtp_video_receiver_->SetSendDataFunc(
      [this](const char *data, size_t size) -> int {
        if (!ice_agent_) {
          LOG_ERROR("ice_agent_ is nullptr");
          return -1;
        }

        if (state_ != NICE_COMPONENT_STATE_READY) {
          return -2;
        }

        return ice_agent_->Send(data, size);
      });
  rtp_video_receiver_->SetOnReceiveCompleteFrame(
      [this](VideoFrame &video_frame) -> void {
        // LOG_ERROR("OnReceiveCompleteFrame {}", video_frame.Size());
        on_receive_video_((const char *)video_frame.Buffer(),
                          video_frame.Size(), remote_user_id_.data(),
                          remote_user_id_.size());
      });

  rtp_video_receiver_->Start();

  rtp_video_sender_ = std::make_unique<RtpVideoSender>();
  rtp_video_sender_->SetSendDataFunc(
      [this](const char *data, size_t size) -> int {
        if (!ice_agent_) {
          LOG_ERROR("ice_agent_ is nullptr");
          return -1;
        }

        if (state_ != NICE_COMPONENT_STATE_READY) {
          return -2;
        }

        return ice_agent_->Send(data, size);
      });

  rtp_video_sender_->Start();

  rtp_audio_receiver_ = std::make_unique<RtpAudioReceiver>();
  rtp_audio_receiver_->SetOnReceiveData(
      [this](const char *data, size_t size) -> void {
        on_receive_audio_(data, size, remote_user_id_.data(),
                          remote_user_id_.size());
      });

  rtp_audio_sender_ = std::make_unique<RtpAudioSender>();
  rtp_audio_sender_->SetSendDataFunc(
      [this](const char *data, size_t size) -> int {
        if (!ice_agent_) {
          LOG_ERROR("ice_agent_ is nullptr");
          return -1;
        }

        if (state_ != NICE_COMPONENT_STATE_READY) {
          return -2;
        }

        return ice_agent_->Send(data, size);
      });

  rtp_audio_sender_->Start();

  rtp_data_sender_ = std::make_unique<RtpDataSender>();
  rtp_data_sender_->SetSendDataFunc(
      [this](const char *data, size_t size) -> int {
        if (!ice_agent_) {
          LOG_ERROR("ice_agent_ is nullptr");
          return -1;
        }

        if (state_ != NICE_COMPONENT_STATE_READY) {
          return -2;
        }

        return ice_agent_->Send(data, size);
      });

  rtp_data_sender_->Start();

  rtp_data_receiver_ = std::make_unique<RtpDataReceiver>();
  rtp_data_receiver_->SetSendDataFunc(
      [this](const char *data, size_t size) -> int {
        if (!ice_agent_) {
          LOG_ERROR("ice_agent_ is nullptr");
          return -1;
        }

        if (state_ != NICE_COMPONENT_STATE_READY) {
          return -2;
        }

        return ice_agent_->Send(data, size);
      });
  rtp_data_receiver_->SetOnReceiveData(
      [this](const char *data, size_t size) -> void {
        on_receive_data_(data, size, remote_user_id_.data(),
                         remote_user_id_.size());
      });

  ice_agent_ = std::make_unique<IceAgent>(trickle_ice_, offer_peer_, stun_ip,
                                          stun_port, turn_ip, turn_port,
                                          turn_username, turn_password);

  ice_agent_->CreateIceAgent(
      [](NiceAgent *agent, guint stream_id, guint component_id,
         NiceComponentState state, gpointer user_ptr) {
        if (user_ptr) {
          IceTransmission *ice_transmission_obj =
              static_cast<IceTransmission *>(user_ptr);
          LOG_INFO("[{}->{}] state_change: {}", ice_transmission_obj->user_id_,
                   ice_transmission_obj->remote_user_id_,
                   nice_component_state_to_string(state));
          ice_transmission_obj->state_ = state;
          ice_transmission_obj->on_ice_status_change_(
              nice_component_state_to_string(state));
        } else {
          LOG_INFO("state_change: {}", nice_component_state_to_string(state));
        }
      },
      [](NiceAgent *agent, guint stream_id, guint component_id,
         gchar *foundation, gpointer user_ptr) {
        if (user_ptr) {
          IceTransmission *ice_transmission_obj =
              static_cast<IceTransmission *>(user_ptr);

          GSList *cands =
              nice_agent_get_local_candidates(agent, stream_id, component_id);
          NiceCandidate *cand;
          for (GSList *i = cands; i; i = i->next) {
            cand = (NiceCandidate *)i->data;
            if (g_strcmp0(cand->foundation, foundation) == 0) {
              ice_transmission_obj->new_local_candidate_ =
                  nice_agent_generate_local_candidate_sdp(agent, cand);

              json message = {
                  {"type", "new_candidate"},
                  {"transmission_id", ice_transmission_obj->transmission_id_},
                  {"user_id", ice_transmission_obj->user_id_},
                  {"remote_user_id", ice_transmission_obj->remote_user_id_},
                  {"sdp", ice_transmission_obj->new_local_candidate_}};
              // LOG_INFO("[{}] Send new candidate to [{}]]:[{}]",
              //          ice_transmission_obj->user_id_,
              //          ice_transmission_obj->remote_user_id_,
              //          ice_transmission_obj->new_local_candidate_);

              if (ice_transmission_obj->ice_ws_transport_) {
                ice_transmission_obj->ice_ws_transport_->Send(message.dump());
              }
            }
          }

          g_slist_free_full(cands, (GDestroyNotify)nice_candidate_free);
        }
      },
      [](NiceAgent *agent, guint stream_id, gpointer user_ptr) {
        // non-trickle
        if (user_ptr) {
          IceTransmission *ice_transmission_obj =
              static_cast<IceTransmission *>(user_ptr);
          LOG_INFO("[{}] gather_done", ice_transmission_obj->user_id_);

          if (!ice_transmission_obj->trickle_ice_) {
            if (ice_transmission_obj->offer_peer_) {
              ice_transmission_obj->SendOffer();
            } else {
              ice_transmission_obj->SendAnswer();
            }
          }
        }
      },
      [](NiceAgent *agent, guint stream_id, guint component_id,
         const char *lfoundation, const char *rfoundation, gpointer user_ptr) {
        LOG_INFO("new selected pair: [{}] [{}]", lfoundation, rfoundation);
        NiceCandidate *local = nullptr;
        NiceCandidate *remote = nullptr;
        nice_agent_get_selected_pair(agent, stream_id, component_id, &local,
                                     &remote);
        if (user_ptr) {
          IceTransmission *ice_transmission_obj =
              static_cast<IceTransmission *>(user_ptr);
          if (local->type == NICE_CANDIDATE_TYPE_RELAYED &&
              remote->type == NICE_CANDIDATE_TYPE_RELAYED) {
            LOG_INFO("Traversal using relay server");
            ice_transmission_obj->traversal_type_ = TraversalType::TRelay;
          } else {
            LOG_INFO("Traversal using p2p");
            ice_transmission_obj->traversal_type_ = TraversalType::TP2P;
          }
          ice_transmission_obj->on_receive_net_status_report_(
              ice_transmission_obj->traversal_type_, 0, 0, nullptr);
        }
      },
      [](NiceAgent *agent, guint stream_id, guint component_id, guint size,
         gchar *buffer, gpointer user_ptr) {
        if (user_ptr) {
          IceTransmission *ice_transmission_obj =
              static_cast<IceTransmission *>(user_ptr);
          if (ice_transmission_obj) {
            if (ice_transmission_obj->CheckIsVideoPacket(buffer, size)) {
              RtpPacket packet((uint8_t *)buffer, size);
              ice_transmission_obj->rtp_video_receiver_->InsertRtpPacket(
                  packet);
            } else if (ice_transmission_obj->CheckIsAudioPacket(buffer, size)) {
              RtpPacket packet((uint8_t *)buffer, size);
              ice_transmission_obj->rtp_audio_receiver_->InsertRtpPacket(
                  packet);
            } else if (ice_transmission_obj->CheckIsDataPacket(buffer, size)) {
              RtpPacket packet((uint8_t *)buffer, size);
              ice_transmission_obj->rtp_data_receiver_->InsertRtpPacket(packet);
            } else if (ice_transmission_obj->CheckIsRtcpPacket(buffer, size)) {
              // LOG_ERROR("Rtcp packet [{}]", (uint8_t)(buffer[1]));
            }
          }
        }
      },
      this);
  return 0;
}

int IceTransmission::DestroyIceTransmission() {
  LOG_INFO("[{}->{}] Destroy ice transmission", user_id_, remote_user_id_);
  if (on_ice_status_change_) {
    on_ice_status_change_("closed");
  }
  return ice_agent_->DestroyIceAgent();
}

int IceTransmission::SetTransmissionId(const std::string &transmission_id) {
  transmission_id_ = transmission_id;

  return 0;
}

int IceTransmission::JoinTransmission() {
  LOG_INFO("[{}] Join transmission", user_id_);

  if (trickle_ice_) {
    SendOffer();
  } else {
    GatherCandidates();
  }
  return 0;
}

int IceTransmission::GatherCandidates() {
  int ret = ice_agent_->GatherCandidates();
  if (ret < 0) {
    LOG_ERROR("Gather candidates failed");
  }
  return 0;
}

int IceTransmission::SetRemoteSdp(const std::string &remote_sdp) {
  ice_agent_->SetRemoteSdp(remote_sdp.c_str());
  LOG_INFO("[{}] set remote sdp", user_id_);
  remote_ice_username_ = GetIceUsername(remote_sdp);
  return 0;
}

int IceTransmission::SendOffer() {
  json message = {{"type", "offer"},
                  {"transmission_id", transmission_id_},
                  {"user_id", user_id_},
                  {"remote_user_id", remote_user_id_},
                  {"sdp", trickle_ice_ ? ice_agent_->GetLocalStreamSdp()
                                       : ice_agent_->GenerateLocalSdp()}};
  // LOG_INFO("Send offer with sdp:[{}]", message.dump());

  if (ice_ws_transport_) {
    ice_ws_transport_->Send(message.dump());
    LOG_INFO("[{}->{}] send offer", user_id_, remote_user_id_);
  }
  return 0;
}

int IceTransmission::SendAnswer() {
  json message = {{"type", "answer"},
                  {"transmission_id", transmission_id_},
                  {"user_id", user_id_},
                  {"remote_user_id", remote_user_id_},
                  {"sdp", trickle_ice_ ? ice_agent_->GetLocalStreamSdp()
                                       : ice_agent_->GenerateLocalSdp()}};
  // LOG_INFO("Send answer with sdp:[{}]", message.dump());
  if (ice_ws_transport_) {
    ice_ws_transport_->Send(message.dump());
    LOG_INFO("[{}->{}] send answer", user_id_, remote_user_id_);
  }

  return 0;
}

int IceTransmission::SendData(DATA_TYPE type, const char *data, size_t size) {
  if (NiceComponentState::NICE_COMPONENT_STATE_READY == state_) {
    std::vector<RtpPacket> packets;

    if (DATA_TYPE::VIDEO == type) {
      if (rtp_video_sender_) {
        if (video_rtp_codec_) {
          video_rtp_codec_->Encode((uint8_t *)data, size, packets);
        }
        rtp_video_sender_->Enqueue(packets);
      }
    } else if (DATA_TYPE::AUDIO == type) {
      if (rtp_audio_sender_) {
        if (audio_rtp_codec_) {
          audio_rtp_codec_->Encode((uint8_t *)data, size, packets);
          rtp_audio_sender_->Enqueue(packets);
        }
      }
    } else if (DATA_TYPE::DATA == type) {
      if (rtp_data_sender_) {
        if (data_rtp_codec_) {
          data_rtp_codec_->Encode((uint8_t *)data, size, packets);
          rtp_data_sender_->Enqueue(packets);
        }
      }
    }
  }
  return 0;
}

int IceTransmission::SendVideoData(VideoFrameType frame_type, const char *data,
                                   size_t size) {
  if (NiceComponentState::NICE_COMPONENT_STATE_READY == state_) {
    std::vector<RtpPacket> packets;

    if (rtp_video_sender_) {
      if (video_rtp_codec_) {
        video_rtp_codec_->Encode(
            static_cast<RtpCodec::VideoFrameType>(frame_type), (uint8_t *)data,
            size, packets);
      }
      rtp_video_sender_->Enqueue(packets);
    }
  }
  return 0;
}

uint8_t IceTransmission::CheckIsRtcpPacket(const char *buffer, size_t size) {
  if (size < 4) {
    return 0;
  }

  uint8_t pt = buffer[1];

  switch (pt) {
    case RtcpHeader::PAYLOAD_TYPE::SR: {
      return pt;
    }
    case RtcpHeader::PAYLOAD_TYPE::RR: {
      return pt;
    }
    case RtcpHeader::PAYLOAD_TYPE::SDES: {
      return pt;
    }
    case RtcpHeader::PAYLOAD_TYPE::BYE: {
      return pt;
    }
    case RtcpHeader::PAYLOAD_TYPE::APP: {
      return pt;
    }
    default: {
      return 0;
    }
  }
}

uint8_t IceTransmission::CheckIsVideoPacket(const char *buffer, size_t size) {
  if (size < 4) {
    return 0;
  }

  uint8_t pt = buffer[1] & 0x7F;
  if (RtpPacket::PAYLOAD_TYPE::H264 == pt ||
      RtpPacket::PAYLOAD_TYPE::H264_FEC_SOURCE == pt ||
      RtpPacket::PAYLOAD_TYPE::H264_FEC_REPAIR == pt ||
      RtpPacket::PAYLOAD_TYPE::AV1 == pt) {
    return pt;
  } else {
    return 0;
  }
}

uint8_t IceTransmission::CheckIsAudioPacket(const char *buffer, size_t size) {
  if (size < 4) {
    return 0;
  }

  uint8_t pt = buffer[1] & 0x7F;
  if (RtpPacket::PAYLOAD_TYPE::OPUS == pt) {
    return pt;
  } else {
    return 0;
  }
}

uint8_t IceTransmission::CheckIsDataPacket(const char *buffer, size_t size) {
  if (size < 4) {
    return 0;
  }

  uint8_t pt = buffer[1] & 0x7F;
  if (RtpPacket::PAYLOAD_TYPE::DATA == pt) {
    return pt;
  } else {
    return 0;
  }
}
