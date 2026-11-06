/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-02
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MOBILE_CONTROLLER_PROTOCOL_H_
#define _MOBILE_CONTROLLER_PROTOCOL_H_

#include <minirtc.h>
#include <nlohmann/json.hpp>
#include <remote_action.h>
#include <cstring>

namespace crossdesk::mobile_controller {
inline std::string PrivacyMessage(int flag, bool block_local_input) {
  if (flag < 0 || flag > 3) return {};
  RemoteAction action{};
  action.type = privacy_command;
  action.pc = {static_cast<PrivacyCommandFlag>(flag), block_local_input};
  return action.to_json();
}
inline std::string SecureAttentionMessage() {
  RemoteAction action{};
  action.type = service_command;
  action.c.flag = send_sas;
  return action.to_json();
}
inline std::string VideoSettingsMessage(int quality, int frame_rate, int preference,
                                        uint32_t request_id) {
  RemoteAction action{};
  action.type = video_settings;
  action.vs = {quality, frame_rate, preference, request_id, false};
  return action.to_json();
}

inline nlohmann::json NetworkReport(const MiniRtcNetTrafficStats& stats, MiniRtcTraversalMode mode) {
  const auto traffic = [](const MiniRtcInboundStats& in, const MiniRtcOutboundStats& out) {
    return nlohmann::json{{"inbound", in.bitrate}, {"outbound", out.bitrate}, {"loss", in.loss_rate}};
  };
  return {{"rtt", stats.rtt_ms}, {"mode", mode}, {"srtp", stats.srtp_active},
    {"decoder", std::string(stats.video_decoder, strnlen(stats.video_decoder, sizeof(stats.video_decoder)))},
    {"video", traffic(stats.video_inbound_stats, stats.video_outbound_stats)},
    {"audio", traffic(stats.audio_inbound_stats, stats.audio_outbound_stats)},
    {"data", traffic(stats.data_inbound_stats, stats.data_outbound_stats)},
    {"total", traffic(stats.total_inbound_stats, stats.total_outbound_stats)}};
}
}  // namespace crossdesk::mobile_controller

#endif
