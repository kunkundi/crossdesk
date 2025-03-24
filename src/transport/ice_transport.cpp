#include "ice_transport.h"

#include <chrono>
#include <map>
#include <nlohmann/json.hpp>
#include <thread>

#include "common.h"
#include "log.h"

using nlohmann::json;

IceTransport::IceTransport(
    std::shared_ptr<SystemClock> clock, bool offer_peer,
    std::string &transmission_id, std::string &user_id,
    std::string &remote_user_id, std::shared_ptr<WsClient> ice_ws_transmission,
    std::function<void(std::string, const std::string &)> on_ice_status_change,
    void *user_data)
    : clock_(clock),
      offer_peer_(offer_peer),
      transmission_id_(transmission_id),
      user_id_(user_id),
      remote_user_id_(remote_user_id),
      ice_ws_transport_(ice_ws_transmission),
      on_ice_status_change_(on_ice_status_change),
      user_data_(user_data) {}

IceTransport::~IceTransport() {}

int IceTransport::SetLocalCapabilities(bool hardware_acceleration,
                                       bool use_trickle_ice,
                                       bool use_reliable_ice, bool enable_turn,
                                       bool force_turn,
                                       std::vector<int> &video_payload_types,
                                       std::vector<int> &audio_payload_types) {
  hardware_acceleration_ = hardware_acceleration;
  use_trickle_ice_ = use_trickle_ice;
  use_reliable_ice_ = use_reliable_ice;
  enable_turn_ = enable_turn;
  force_turn_ = force_turn;
  support_video_payload_types_ = video_payload_types;
  support_audio_payload_types_ = audio_payload_types;
  support_data_payload_types_ = {rtp::PAYLOAD_TYPE::DATA};
  return 0;
}

int IceTransport::InitIceTransmission(
    std::string &stun_ip, int stun_port, std::string &turn_ip, int turn_port,
    std::string &turn_username, std::string &turn_password,
    rtp::PAYLOAD_TYPE video_codec_payload_type) {
  ice_transport_controller_ = std::make_shared<IceTransportController>(clock_);

  ice_agent_ = std::make_unique<IceAgent>(
      offer_peer_, use_trickle_ice_, use_reliable_ice_, enable_turn_,
      force_turn_, stun_ip, stun_port, turn_ip, turn_port, turn_username,
      turn_password);

  ice_io_statistics_ = std::make_unique<IOStatistics>(
      [this](const IOStatistics::NetTrafficStats &net_traffic_stats) {
        if (on_receive_net_status_report_) {
          XNetTrafficStats xnet_traffic_stats;
          memcpy(&xnet_traffic_stats, &net_traffic_stats,
                 sizeof(XNetTrafficStats));
          on_receive_net_status_report_(
              user_id_.data(), user_id_.size(), TraversalMode(traversal_type_),
              &xnet_traffic_stats, remote_user_id_.data(),
              remote_user_id_.size(), user_data_);
        }
      });

  ice_agent_->CreateIceAgent(
      [](NiceAgent *agent, guint stream_id, guint component_id,
         NiceComponentState state, gpointer user_ptr) {
        static_cast<IceTransport *>(user_ptr)->OnIceStateChange(
            agent, stream_id, component_id, state, user_ptr);
      },
      [](NiceAgent *agent, guint stream_id, guint component_id,
         gchar *foundation, gpointer user_ptr) {
        static_cast<IceTransport *>(user_ptr)->OnNewLocalCandidate(
            agent, stream_id, component_id, foundation, user_ptr);
      },
      [](NiceAgent *agent, guint stream_id, gpointer user_ptr) {
        static_cast<IceTransport *>(user_ptr)->OnGatheringDone(agent, stream_id,
                                                               user_ptr);
      },
      [](NiceAgent *agent, guint stream_id, guint component_id,
         const char *lfoundation, const char *rfoundation, gpointer user_ptr) {
        static_cast<IceTransport *>(user_ptr)->OnNewSelectedPair(
            agent, stream_id, component_id, lfoundation, rfoundation, user_ptr);
      },
      [](NiceAgent *agent, guint stream_id, guint component_id, guint size,
         gchar *buffer, gpointer user_ptr) {
        static_cast<IceTransport *>(user_ptr)->OnReceiveBuffer(
            agent, stream_id, component_id, size, buffer, user_ptr);
      },
      this);

  return 0;
}

void IceTransport::OnIceStateChange(NiceAgent *agent, guint stream_id,
                                    guint component_id,
                                    NiceComponentState state,
                                    gpointer user_ptr) {
  if (!is_closed_) {
    LOG_INFO("[{}->{}] state_change: {}", user_id_, remote_user_id_,
             nice_component_state_to_string(state));
    state_ = state;

    if (state == NICE_COMPONENT_STATE_READY ||
        state == NICE_COMPONENT_STATE_CONNECTED) {
      ice_io_statistics_->Start();
      if (ice_transport_controller_) {
        ice_transport_controller_->UpdateNetworkAvaliablity(true);
      }
    }

    on_ice_status_change_(nice_component_state_to_string(state),
                          remote_user_id_);
  }
}

void IceTransport::OnNewLocalCandidate(NiceAgent *agent, guint stream_id,
                                       guint component_id, gchar *foundation,
                                       gpointer user_ptr) {
  if (use_trickle_ice_) {
    GSList *cands =
        nice_agent_get_local_candidates(agent, stream_id, component_id);
    NiceCandidate *cand;
    for (GSList *i = cands; i; i = i->next) {
      cand = (NiceCandidate *)i->data;
      if (g_strcmp0(cand->foundation, foundation) == 0) {
        new_local_candidate_ =
            nice_agent_generate_local_candidate_sdp(agent, cand);

        json message = {{"type", "new_candidate"},
                        {"transmission_id", transmission_id_},
                        {"user_id", user_id_},
                        {"remote_user_id", remote_user_id_},
                        {"sdp", new_local_candidate_}};

        if (ice_ws_transport_) {
          ice_ws_transport_->Send(message.dump());
        }
      }
    }

    g_slist_free_full(cands, (GDestroyNotify)nice_candidate_free);
  }
}

void IceTransport::OnGatheringDone(NiceAgent *agent, guint stream_id,
                                   gpointer user_ptr) {
  LOG_INFO("[{}->{}] gather_done", user_id_, remote_user_id_);

  if (!use_trickle_ice_) {
    if (offer_peer_) {
      SendOffer();
    } else {
      SendAnswer();
    }
  }
}

void IceTransport::OnNewSelectedPair(NiceAgent *agent, guint stream_id,
                                     guint component_id,
                                     const char *lfoundation,
                                     const char *rfoundation,
                                     gpointer user_ptr) {
  LOG_INFO("new selected pair: [{}] [{}]", lfoundation, rfoundation);
  NiceCandidate *local = nullptr;
  NiceCandidate *remote = nullptr;
  nice_agent_get_selected_pair(agent, stream_id, component_id, &local, &remote);
  if (local->type == NICE_CANDIDATE_TYPE_RELAYED &&
      remote->type == NICE_CANDIDATE_TYPE_RELAYED) {
    LOG_INFO("Traversal using relay server");
    traversal_type_ = TraversalType::TRelay;
  } else {
    LOG_INFO("Traversal using p2p");
    traversal_type_ = TraversalType::TP2P;
  }
  XNetTrafficStats net_traffic_stats;
  memset(&net_traffic_stats, 0, sizeof(net_traffic_stats));

  on_receive_net_status_report_(user_id_.data(), user_id_.size(),
                                TraversalMode(traversal_type_),
                                &net_traffic_stats, remote_user_id_.data(),
                                remote_user_id_.size(), user_data_);
}

void IceTransport::OnReceiveBuffer(NiceAgent *agent, guint stream_id,
                                   guint component_id, guint size,
                                   gchar *buffer, gpointer user_ptr) {
  if (!is_closed_) {
    if (CheckIsRtpPacket(buffer, size)) {
      if (CheckIsVideoPacket(buffer, size) && ice_transport_controller_) {
        ice_transport_controller_->OnReceiveVideoRtpPacket(buffer, size);
      } else if (CheckIsAudioPacket(buffer, size) &&
                 ice_transport_controller_) {
        ice_transport_controller_->OnReceiveAudioRtpPacket(buffer, size);
      } else if (CheckIsDataPacket(buffer, size) && ice_transport_controller_) {
        ice_transport_controller_->OnReceiveDataRtpPacket(buffer, size);
      }
    } else if (CheckIsRtcpPacket(buffer, size)) {
      // LOG_ERROR("Rtcp packet [{}]", (uint8_t)(buffer[1]));
      RtcpPacketInfo rtcp_packet_info;
      ParseRtcpPacket((const uint8_t *)buffer, size, &rtcp_packet_info);
    } else {
      LOG_ERROR("Unknown packet");
    }
  }
}

bool IceTransport::ParseRtcpPacket(const uint8_t *buffer, size_t size,
                                   RtcpPacketInfo *rtcp_packet_info) {
  RtcpCommonHeader rtcp_block;
  // If a sender report is received but no DLRR, we need to reset the
  // roundTripTime stat according to the standard, see
  // https://www.w3.org/TR/webrtc-stats/#dom-rtcremoteoutboundrtpstreamstats-roundtriptime
  struct RtcpReceivedBlock {
    bool sender_report = false;
    bool dlrr = false;
  };
  // For each remote SSRC we store if we've received a sender report or a DLRR
  // block.
  bool valid = true;
  if (!rtcp_block.Parse(buffer, size)) {
    valid = false;
    return valid;
  }

  switch (rtcp_block.type()) {
    case RtcpPacket::RtcpPayloadType::SR:
      valid = HandleSenderReport(rtcp_block, rtcp_packet_info);
      // received_blocks[rtcp_packet_info->remote_ssrc].sender_report = true;
      break;
    case RtcpPacket::RtcpPayloadType::RR:
      valid = HandleReceiverReport(rtcp_block, rtcp_packet_info);
      break;
    case RtpFeedback::kPacketType:
      switch (rtcp_block.fmt()) {
        case webrtc::rtcp::CongestionControlFeedback::kFeedbackMessageType:
          valid = HandleCongestionControlFeedback(rtcp_block, rtcp_packet_info);
          break;
        case webrtc::rtcp::Nack::kFeedbackMessageType:
          valid = HandleNack(rtcp_block, rtcp_packet_info);
          break;
        case webrtc::rtcp::Fir::kFeedbackMessageType:
          valid = HandleFir(rtcp_block, rtcp_packet_info);
          break;
        default:
          break;
      }
      break;

    // case rtcp::Psfb::kPacketType:
    //   switch (rtcp_block.fmt()) {
    //     case rtcp::Pli::kFeedbackMessageType:
    //       valid = HandlePli(rtcp_block, rtcp_packet_info);
    //       break;
    //     case rtcp::Fir::kFeedbackMessageType:
    //       valid = HandleFir(rtcp_block, rtcp_packet_info);
    //       break;
    //     case rtcp::Psfb::kAfbMessageType:
    //       HandlePsfbApp(rtcp_block, rtcp_packet_info);
    //       break;
    //     default:
    //       ++num_skipped_packets_;
    //       break;
    //   }
    //   break;
    default:
      break;
  }

  // if (num_skipped_packets_ > 0) {
  //   const Timestamp now = env_.clock().CurrentTime();
  //   if (now - last_skipped_packets_warning_ >= kMaxWarningLogInterval) {
  //     last_skipped_packets_warning_ = now;
  //     RTC_LOG(LS_WARNING)
  //         << num_skipped_packets_
  //         << " RTCP blocks were skipped due to being malformed or of "
  //            "unrecognized/unsupported type, during the past "
  //         << kMaxWarningLogInterval << " period.";
  //   }
  // }

  // if (!valid) {
  //   ++num_skipped_packets_;
  //   return false;
  // }

  // for (const auto &rb : received_blocks) {
  //   if (rb.second.sender_report && !rb.second.dlrr) {
  //     auto rtt_stats = non_sender_rtts_.find(rb.first);
  //     if (rtt_stats != non_sender_rtts_.end()) {
  //       rtt_stats->second.Invalidate();
  //     }
  //   }
  // }

  // if (packet_type_counter_observer_) {
  //   packet_type_counter_observer_->RtcpPacketTypesCounterUpdated(
  //       local_media_ssrc(), packet_type_counter_);
  // }

  return valid;
}

void IceTransport::HandleReportBlock(const RtcpReportBlock &rtcp_report_block,
                                     RtcpPacketInfo *packet_information,
                                     uint32_t remote_ssrc) {
  int64_t now = clock_->CurrentTime();

  RtcpReportBlock report_block_data;

  int64_t now_ntp = clock_->ConvertToNtpTime(now);
  // Number of seconds since 1900 January 1 00:00 GMT (see
  // https://tools.ietf.org/html/rfc868).
  report_block_data.SetReportBlock(remote_ssrc, rtcp_report_block,
                                   clock_->NtpToUtc(now_ntp), now);

  uint32_t send_time_ntp = rtcp_report_block.LastSr();
  if (send_time_ntp != 0) {
    uint32_t delay_ntp = rtcp_report_block.DelaySinceLastSr();
    // Local NTP time.
    constexpr uint64_t kNtpFractionalUnit = 0x100000000;
    uint32_t seconds = now_ntp / kNtpFractionalUnit;
    uint32_t fractions = now_ntp % kNtpFractionalUnit;
    uint32_t receive_time_ntp = (seconds << 16) | (fractions >> 16);
    // RTT in 1/(2^16) seconds.
    uint32_t rtt_ntp = receive_time_ntp - delay_ntp - send_time_ntp;
    // Convert to 1/1000 seconds (milliseconds).
    int64_t rtt = static_cast<int64_t>((rtt_ntp * 1000) / (1 << 16));
    report_block_data.AddRoundTripTimeSample(rtt);
    packet_information->rtt = rtt;
  }

  packet_information->report_block_datas.push_back(report_block_data);
}

bool IceTransport::HandleSenderReport(const RtcpCommonHeader &rtcp_block,
                                      RtcpPacketInfo *rtcp_packet_info) {
  SenderReport sender_report;
  if (!sender_report.Parse(rtcp_block)) {
    return false;
  }

  if (ice_transport_controller_) {
    ice_transport_controller_->OnSenderReport(sender_report);
  }
  return true;
}

bool IceTransport::HandleReceiverReport(const RtcpCommonHeader &rtcp_block,
                                        RtcpPacketInfo *rtcp_packet_info) {
  ReceiverReport receiver_report;
  if (!receiver_report.Parse(rtcp_block)) {
    return false;
  }

  const uint32_t remote_ssrc = receiver_report.SenderSsrc();
  rtcp_packet_info->remote_ssrc = remote_ssrc;

  for (const RtcpReportBlock &rtcp_report_block :
       receiver_report.GetReportBlocks()) {
    HandleReportBlock(rtcp_report_block, rtcp_packet_info, remote_ssrc);
  }

  if (ice_transport_controller_) {
    ice_transport_controller_->OnReceiverReport(
        rtcp_packet_info->report_block_datas);
  }
  return true;
}

bool IceTransport::HandleCongestionControlFeedback(
    const RtcpCommonHeader &rtcp_block, RtcpPacketInfo *rtcp_packet_info) {
  webrtc::rtcp::CongestionControlFeedback feedback;
  if (!feedback.Parse(rtcp_block) || feedback.packets().empty()) {
    return false;
  }
  // uint32_t first_media_source_ssrc = feedback.packets()[0].ssrc;
  // if (first_media_source_ssrc == local_media_ssrc() ||
  //     registered_ssrcs_.contains(first_media_source_ssrc)) {
  //   rtcp_packet_info->congestion_control_feedback.emplace(std::move(feedback));
  // }

  if (ice_transport_controller_) {
    ice_transport_controller_->OnCongestionControlFeedback(feedback);
  }
  return true;
}

bool IceTransport::HandleNack(const RtcpCommonHeader &rtcp_block,
                              RtcpPacketInfo *rtcp_packet_info) {
  webrtc::rtcp::Nack nack;
  if (!nack.Parse(rtcp_block)) {
    return false;
  }

  if (ice_transport_controller_) {
    ice_transport_controller_->OnReceiveNack(nack.packet_ids());
    return true;
  }

  return false;
}

bool IceTransport::HandleFir(const RtcpCommonHeader &rtcp_block,
                             RtcpPacketInfo *rtcp_packet_info) {
  webrtc::rtcp::Fir fir;
  if (!fir.Parse(rtcp_block)) {
    return false;
  }

  if (ice_transport_controller_) {
    ice_transport_controller_->FullIntraRequest();
    return true;
  }

  return false;
}

int IceTransport::DestroyIceTransmission() {
  LOG_INFO("[{}->{}] Destroy ice transmission", user_id_, remote_user_id_);
  is_closed_ = true;

  if (on_ice_status_change_) {
    on_ice_status_change_("closed", remote_user_id_);
  }

  if (ice_io_statistics_) {
    ice_io_statistics_->Stop();
  }

  if (ice_transport_controller_) {
    ice_transport_controller_->Destroy();
  }

  return ice_agent_->DestroyIceAgent();
}

int IceTransport::SetTransmissionId(const std::string &transmission_id) {
  transmission_id_ = transmission_id;

  return 0;
}

int IceTransport::JoinTransmission() {
  LOG_INFO("[{}] Join transmission", user_id_);

  if (use_trickle_ice_) {
    SendOffer();
  } else {
    GatherCandidates();
  }
  return 0;
}

int IceTransport::GatherCandidates() {
  int ret = ice_agent_->GatherCandidates();
  if (ret < 0) {
    LOG_ERROR("Gather candidates failed");
  }
  return 0;
}

int IceTransport::SetRemoteSdp(const std::string &remote_sdp) {
  std::string media_stream_sdp = GetRemoteCapabilities(remote_sdp);
  if (media_stream_sdp.empty()) {
    LOG_ERROR("Set remote sdp failed due to negotiation failed");
    return -1;
  }

  ice_agent_->SetRemoteSdp(media_stream_sdp.c_str());
  // LOG_INFO("[{}] set remote sdp", user_id_);

  remote_ice_username_ = GetIceUsername(media_stream_sdp);
  return 0;
}

int IceTransport::SendOffer() {
  local_sdp_ = ice_agent_->GenerateLocalSdp();
  AppendLocalCapabilitiesToOffer(local_sdp_);
  json message = {{"type", "offer"},
                  {"transmission_id", transmission_id_},
                  {"user_id", user_id_},
                  {"remote_user_id", remote_user_id_},
                  {"sdp", local_sdp_.c_str()}};
  // LOG_INFO("Send offer with sdp:\n[\n{}]", local_sdp_.c_str());
  if (ice_ws_transport_) {
    ice_ws_transport_->Send(message.dump());
    LOG_INFO("[{}->{}] send offer", user_id_, remote_user_id_);
  }
  return 0;
}

int IceTransport::SendAnswer() {
  local_sdp_ = ice_agent_->GenerateLocalSdp();
  AppendLocalCapabilitiesToAnswer(local_sdp_);
  json message = {{"type", "answer"},
                  {"transmission_id", transmission_id_},
                  {"user_id", user_id_},
                  {"remote_user_id", remote_user_id_},
                  {"sdp", local_sdp_.c_str()}};
  // LOG_INFO("Send answer with sdp:\n[\n{}]", local_sdp_.c_str());
  if (ice_ws_transport_) {
    ice_ws_transport_->Send(message.dump());
    LOG_INFO("[{}->{}] send answer", user_id_, remote_user_id_);
  }

  return 0;
}

int IceTransport::AppendLocalCapabilitiesToOffer(
    const std::string &remote_sdp) {
  std::string preferred_video_pt;
  std::string to_replace = "ICE/SDP";
  std::string video_capabilities = "UDP/TLS/RTP/SAVPF ";
  std::string audio_capabilities = "UDP/TLS/RTP/SAVPF 111";
  std::string data_capabilities = "UDP/TLS/RTP/SAVPF 127";

  switch (video_codec_payload_type_) {
    case rtp::PAYLOAD_TYPE::H264: {
      preferred_video_pt = std::to_string(rtp::PAYLOAD_TYPE::H264);
      video_capabilities += preferred_video_pt + " 97 98 99";
      break;
    }
    case rtp::PAYLOAD_TYPE::AV1: {
      preferred_video_pt = std::to_string(rtp::PAYLOAD_TYPE::AV1);
      video_capabilities += preferred_video_pt + " 96 97 98";
      break;
    }
    default: {
      preferred_video_pt = std::to_string(rtp::PAYLOAD_TYPE::H264);
      video_capabilities += preferred_video_pt + " 97 98 99";
      break;
    }
  }

  std::size_t video_start = remote_sdp.find("m=video");
  std::size_t video_end = remote_sdp.find("\n", video_start);
  std::size_t audio_start = remote_sdp.find("m=audio");
  std::size_t audio_end = remote_sdp.find("\n", audio_start);
  std::size_t data_start = remote_sdp.find("m=data");
  std::size_t data_end = remote_sdp.find("\n", data_start);

  size_t pos = 0;
  if (video_start != std::string::npos && video_end != std::string::npos) {
    if ((pos = local_sdp_.find(to_replace, video_start)) != std::string::npos) {
      local_sdp_.replace(pos, to_replace.length(), video_capabilities);
      pos += video_capabilities.length();
    }
  }

  if (audio_start != std::string::npos && audio_end != std::string::npos) {
    if ((pos = local_sdp_.find(to_replace, audio_start)) != std::string::npos) {
      local_sdp_.replace(pos, to_replace.length(), audio_capabilities);
      pos += audio_capabilities.length();
    }
  }

  if (data_start != std::string::npos && data_end != std::string::npos) {
    if ((pos = local_sdp_.find(to_replace, data_start)) != std::string::npos) {
      local_sdp_.replace(pos, to_replace.length(), data_capabilities);
      pos += data_capabilities.length();
    }
  }

  return 0;
}

int IceTransport::AppendLocalCapabilitiesToAnswer(
    const std::string &remote_sdp) {
  std::string negotiated_video_pt;
  std::string negotiated_audio_pt;
  std::string negotiated_data_pt;
  std::string to_replace = "ICE/SDP";
  std::string protocol = "UDP/TLS/RTP/SAVPF ";
  negotiated_video_pt = protocol + std::to_string(negotiated_video_pt_);
  negotiated_audio_pt = protocol + std::to_string(negotiated_audio_pt_);
  negotiated_data_pt = protocol + std::to_string(negotiated_data_pt_);

  std::size_t video_start = remote_sdp.find("m=video");
  std::size_t video_end = remote_sdp.find("\n", video_start);

  size_t pos = 0;
  if (video_start != std::string::npos && video_end != std::string::npos) {
    if ((pos = local_sdp_.find(to_replace, video_start)) != std::string::npos) {
      local_sdp_.replace(pos, to_replace.length(), negotiated_video_pt);
      pos += negotiated_video_pt.length();
    }
  }

  std::size_t audio_start = remote_sdp.find("m=audio");
  std::size_t audio_end = remote_sdp.find("\n", audio_start);

  if (audio_start != std::string::npos && audio_end != std::string::npos) {
    if ((pos = local_sdp_.find(to_replace, audio_start)) != std::string::npos) {
      local_sdp_.replace(pos, to_replace.length(), negotiated_audio_pt);
      pos += negotiated_audio_pt.length();
    }
  }

  std::size_t data_start = remote_sdp.find("m=data");
  std::size_t data_end = remote_sdp.find("\n", data_start);

  if (data_start != std::string::npos && data_end != std::string::npos) {
    if ((pos = local_sdp_.find(to_replace, data_start)) != std::string::npos) {
      local_sdp_.replace(pos, to_replace.length(), negotiated_data_pt);
      pos += negotiated_data_pt.length();
    }
  }

  return 0;
}

std::string IceTransport::GetRemoteCapabilities(const std::string &remote_sdp) {
  std::string media_stream_sdp;
  std::size_t video_start = remote_sdp.find("m=video");
  std::size_t video_end = remote_sdp.find("m=audio");
  std::size_t audio_start = video_end;
  std::size_t audio_end = remote_sdp.find("m=data");
  std::size_t data_start = audio_end;
  std::size_t data_end = remote_sdp.find("a=candidate");
  std::size_t candidate_start = data_end;

  if (!remote_capabilities_got_) {
    if (!NegotiateVideoPayloadType(remote_sdp)) {
      return std::string();
    }
    if (!NegotiateAudioPayloadType(remote_sdp)) {
      return std::string();
    }
    if (!NegotiateDataPayloadType(remote_sdp)) {
      return std::string();
    }

    if (ice_transport_controller_) {
      ice_transport_controller_->Create(
          remote_user_id_, negotiated_video_pt_, hardware_acceleration_,
          ice_agent_, ice_io_statistics_, on_receive_video_, on_receive_audio_,
          on_receive_data_, user_data_);
      ice_transport_controller_->Start();
    }

    remote_capabilities_got_ = true;
  }

  if ((video_start != std::string::npos && video_end != std::string::npos) ||
      (audio_start != std::string::npos && audio_end != std::string::npos) ||
      (data_start != std::string::npos && data_end != std::string::npos)) {
    if (video_start != std::string::npos && video_end != std::string::npos) {
      media_stream_sdp =
          remote_sdp.substr(video_start, video_end - video_start);
    } else if (audio_start != std::string::npos &&
               audio_end != std::string::npos) {
      media_stream_sdp =
          remote_sdp.substr(audio_start, audio_end - audio_start);
    } else {
      media_stream_sdp = remote_sdp.substr(data_start, data_end - data_start);
    }

    if (candidate_start != std::string::npos) {
      media_stream_sdp += remote_sdp.substr(candidate_start);
    }
  } else {
    return remote_sdp;
  }

  return media_stream_sdp;
}

bool IceTransport::NegotiateVideoPayloadType(const std::string &remote_sdp) {
  std::string remote_video_capabilities;
  std::string local_video_capabilities;
  std::string remote_prefered_video_pt;

  std::size_t start =
      remote_sdp.find("m=video") + std::string("m=video").length();
  if (start != std::string::npos) {
    std::size_t end = remote_sdp.find("\n", start);
    std::string::size_type pos1 = remote_sdp.find(' ', start);
    std::string::size_type pos2 = remote_sdp.find(' ', pos1 + 1);
    std::string::size_type pos3 = remote_sdp.find(' ', pos2 + 1);
    if (end != std::string::npos && pos1 != std::string::npos &&
        pos2 != std::string::npos && pos3 != std::string::npos) {
      remote_video_capabilities = remote_sdp.substr(pos3 + 1, end - pos3 - 1);
    }
  }
  LOG_INFO("remote video capabilities [{}]", remote_video_capabilities.c_str());

  for (size_t index = 0; index < support_video_payload_types_.size(); ++index) {
    if (index == support_video_payload_types_.size() - 1) {
      local_video_capabilities +=
          std::to_string(support_video_payload_types_[index]);
    } else {
      local_video_capabilities +=
          std::to_string(support_video_payload_types_[index]) + " ";
    }
  }
  LOG_INFO("local video capabilities [{}]", local_video_capabilities.c_str());

  std::size_t prefered_pt_start = 0;

  while (prefered_pt_start <= remote_video_capabilities.length()) {
    std::size_t prefered_pt_end =
        remote_video_capabilities.find(' ', prefered_pt_start);
    if (prefered_pt_end != std::string::npos) {
      remote_prefered_video_pt =
          remote_video_capabilities.substr(0, prefered_pt_end);
    } else {
      remote_prefered_video_pt = remote_video_capabilities;
    }

    remote_prefered_video_pt_ =
        (rtp::PAYLOAD_TYPE)(atoi(remote_prefered_video_pt.c_str()));

    bool is_support_this_video_pt =
        std::find(support_video_payload_types_.begin(),
                  support_video_payload_types_.end(),
                  remote_prefered_video_pt_) !=
        support_video_payload_types_.end();

    if (is_support_this_video_pt) {
      negotiated_video_pt_ = (rtp::PAYLOAD_TYPE)remote_prefered_video_pt_;
      break;
    } else {
      if (prefered_pt_end != std::string::npos) {
        prefered_pt_start = prefered_pt_end + 1;
      } else {
        break;
      }
    }
  }

  if (negotiated_video_pt_ == rtp::PAYLOAD_TYPE::UNDEFINED) {
    LOG_ERROR("Negotiate video pt failed");
    return false;
  } else {
    LOG_INFO("negotiated video pt [{}]", (int)negotiated_video_pt_);
    return true;
  }
}

bool IceTransport::NegotiateAudioPayloadType(const std::string &remote_sdp) {
  std::string remote_audio_capabilities;
  std::string remote_prefered_audio_pt;

  std::size_t start =
      remote_sdp.find("m=audio") + std::string("m=audio").length();
  if (start != std::string::npos) {
    std::size_t end = remote_sdp.find("\n", start);
    std::string::size_type pos1 = remote_sdp.find(' ', start);
    std::string::size_type pos2 = remote_sdp.find(' ', pos1 + 1);
    std::string::size_type pos3 = remote_sdp.find(' ', pos2 + 1);
    if (end != std::string::npos && pos1 != std::string::npos &&
        pos2 != std::string::npos && pos3 != std::string::npos) {
      remote_audio_capabilities = remote_sdp.substr(pos3 + 1, end - pos3 - 1);
    }
  }
  LOG_INFO("remote audio capabilities [{}]", remote_audio_capabilities.c_str());

  std::size_t prefered_pt_start = 0;

  while (prefered_pt_start <= remote_audio_capabilities.length()) {
    std::size_t prefered_pt_end =
        remote_audio_capabilities.find(' ', prefered_pt_start);
    if (prefered_pt_end != std::string::npos) {
      remote_prefered_audio_pt =
          remote_audio_capabilities.substr(0, prefered_pt_end);
    } else {
      remote_prefered_audio_pt = remote_audio_capabilities;
    }

    remote_prefered_audio_pt_ =
        (rtp::PAYLOAD_TYPE)(atoi(remote_prefered_audio_pt.c_str()));

    bool is_support_this_audio_pt =
        std::find(support_audio_payload_types_.begin(),
                  support_audio_payload_types_.end(),
                  remote_prefered_audio_pt_) !=
        support_audio_payload_types_.end();

    if (is_support_this_audio_pt) {
      negotiated_audio_pt_ = (rtp::PAYLOAD_TYPE)remote_prefered_audio_pt_;
      break;
    } else {
      if (prefered_pt_end != std::string::npos) {
        prefered_pt_start = prefered_pt_end + 1;
      } else {
        break;
      }
    }
  }

  if (negotiated_audio_pt_ == rtp::PAYLOAD_TYPE::UNDEFINED) {
    LOG_ERROR("Negotiate audio pt failed");
    return false;
  } else {
    LOG_INFO("negotiated audio pt [{}]", (int)negotiated_audio_pt_);
    return true;
  }
}

bool IceTransport::NegotiateDataPayloadType(const std::string &remote_sdp) {
  std::string remote_data_capabilities;
  std::string remote_prefered_data_pt;

  std::size_t start =
      remote_sdp.find("m=data") + std::string("m=data").length();
  if (start != std::string::npos) {
    std::size_t end = remote_sdp.find("\n", start);
    std::string::size_type pos1 = remote_sdp.find(' ', start);
    std::string::size_type pos2 = remote_sdp.find(' ', pos1 + 1);
    std::string::size_type pos3 = remote_sdp.find(' ', pos2 + 1);
    if (end != std::string::npos && pos1 != std::string::npos &&
        pos2 != std::string::npos && pos3 != std::string::npos) {
      remote_data_capabilities = remote_sdp.substr(pos3 + 1, end - pos3 - 1);
    }
  }
  LOG_INFO("remote data capabilities [{}]", remote_data_capabilities.c_str());

  std::size_t prefered_pt_start = 0;

  while (prefered_pt_start <= remote_data_capabilities.length()) {
    std::size_t prefered_pt_end =
        remote_data_capabilities.find(' ', prefered_pt_start);
    if (prefered_pt_end != std::string::npos) {
      remote_prefered_data_pt =
          remote_data_capabilities.substr(0, prefered_pt_end);
    } else {
      remote_prefered_data_pt = remote_data_capabilities;
    }

    remote_prefered_data_pt_ =
        (rtp::PAYLOAD_TYPE)(atoi(remote_prefered_data_pt.c_str()));

    bool is_support_this_data_pt =
        std::find(support_data_payload_types_.begin(),
                  support_data_payload_types_.end(),
                  remote_prefered_data_pt_) !=
        support_data_payload_types_.end();

    if (is_support_this_data_pt) {
      negotiated_data_pt_ = (rtp::PAYLOAD_TYPE)remote_prefered_data_pt_;
      break;
    } else {
      if (prefered_pt_end != std::string::npos) {
        prefered_pt_start = prefered_pt_end + 1;
      } else {
        break;
      }
    }
  }

  if (negotiated_data_pt_ == rtp::PAYLOAD_TYPE::UNDEFINED) {
    LOG_ERROR("Negotiate data pt failed");
    return false;
  } else {
    LOG_INFO("negotiated data pt [{}]", (int)negotiated_data_pt_);
    return true;
  }
}

std::vector<rtp::PAYLOAD_TYPE> IceTransport::GetNegotiatedCapabilities() {
  return {negotiated_video_pt_, negotiated_audio_pt_, negotiated_data_pt_};
}

int IceTransport::SendVideoFrame(const XVideoFrame *video_frame) {
  if (state_ != NICE_COMPONENT_STATE_CONNECTED &&
      state_ != NICE_COMPONENT_STATE_READY) {
    LOG_ERROR("Ice is not connected, state = [{}]",
              nice_component_state_to_string(state_));
    return -2;
  }

  if (ice_transport_controller_) {
    return ice_transport_controller_->SendVideo(video_frame);
  }

  return -1;
}

int IceTransport::SendAudioFrame(const char *data, size_t size) {
  if (state_ != NICE_COMPONENT_STATE_CONNECTED &&
      state_ != NICE_COMPONENT_STATE_READY) {
    LOG_ERROR("Ice is not connected, state = [{}]",
              nice_component_state_to_string(state_));
    return -2;
  }

  if (ice_transport_controller_) {
    return ice_transport_controller_->SendAudio(data, size);
  }

  return -1;
}

int IceTransport::SendDataFrame(const char *data, size_t size) {
  if (state_ != NICE_COMPONENT_STATE_CONNECTED &&
      state_ != NICE_COMPONENT_STATE_READY) {
    LOG_ERROR("Ice is not connected, state = [{}]",
              nice_component_state_to_string(state_));
    return -2;
  }

  if (ice_transport_controller_) {
    return ice_transport_controller_->SendData(data, size);
  }

  return -1;
}

uint8_t IceTransport::CheckIsRtpPacket(const char *buffer, size_t size) {
  if (size < 2) {
    return 0;
  }

  uint8_t payload_type = buffer[1] & 0x7F;
  if (payload_type == 96 || payload_type == 99 || payload_type == 111 ||
      payload_type == 127) {
    return payload_type;
  } else if (payload_type == 95 || payload_type == 98 || payload_type == 110 ||
             payload_type == 126) {
    return payload_type;
  } else {
    return 0;
  }
}

uint8_t IceTransport::CheckIsRtcpPacket(const char *buffer, size_t size) {
  if (size < 2) {
    return 0;
  }

  uint8_t payload_type = buffer[1];
  if (payload_type >= 192 && payload_type <= 223) {
    return payload_type;
    ;
  }

  return 0;
}

uint8_t IceTransport::CheckIsVideoPacket(const char *buffer, size_t size) {
  if (size < 4) {
    return 0;
  }

  uint8_t v = (buffer[0] >> 6) & 0x03;
  if (2 != v) {
    return 0;
  }

  uint8_t pt = buffer[1] & 0x7F;
  if (rtp::PAYLOAD_TYPE::H264 == pt || (rtp::PAYLOAD_TYPE::H264 - 1) == pt ||
      rtp::PAYLOAD_TYPE::H264_FEC_SOURCE == pt ||
      (rtp::PAYLOAD_TYPE::H264_FEC_SOURCE - 1) == pt ||
      rtp::PAYLOAD_TYPE::H264_FEC_REPAIR == pt ||
      (rtp::PAYLOAD_TYPE::H264_FEC_REPAIR - 1) == pt ||
      rtp::PAYLOAD_TYPE::AV1 == pt || (rtp::PAYLOAD_TYPE::AV1 - 1) == pt) {
    return pt;
  } else {
    return 0;
  }
}

uint8_t IceTransport::CheckIsAudioPacket(const char *buffer, size_t size) {
  if (size < 4) {
    return 0;
  }

  uint8_t v = (buffer[0] >> 6) & 0x03;
  if (2 != v) {
    return 0;
  }

  uint8_t pt = buffer[1] & 0x7F;
  if (rtp::PAYLOAD_TYPE::OPUS == pt) {
    return pt;
  } else {
    return 0;
  }
}

uint8_t IceTransport::CheckIsDataPacket(const char *buffer, size_t size) {
  if (size < 4) {
    return 0;
  }

  uint8_t v = (buffer[0] >> 6) & 0x03;
  if (2 != v) {
    return 0;
  }

  uint8_t pt = buffer[1] & 0x7F;
  if (rtp::PAYLOAD_TYPE::DATA == pt) {
    return pt;
  } else {
    return 0;
  }
}
