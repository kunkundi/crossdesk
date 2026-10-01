#pragma once

#include <minirtc.h>
#include <nlohmann/json.hpp>
#include <remote_action.h>

namespace crossdesk::android_controller {
inline std::string VideoSettingsMessage(int quality, int frame_rate, int preference,
                                        uint32_t request_id) {
  RemoteAction action{};
  action.type = video_settings;
  action.vs = {quality, frame_rate, preference, request_id, false};
  return action.to_json();
}

inline nlohmann::json NetworkReport(const MiniRtcNetTrafficStats& stats, TraversalMode mode) {
  const auto traffic = [](const MiniRtcInboundStats& in, const MiniRtcOutboundStats& out) {
    return nlohmann::json{{"inbound", in.bitrate}, {"outbound", out.bitrate}, {"loss", in.loss_rate}};
  };
  return {{"rtt", stats.rtt_ms}, {"mode", mode}, {"srtp", stats.srtp_active},
    {"video", traffic(stats.video_inbound_stats, stats.video_outbound_stats)},
    {"audio", traffic(stats.audio_inbound_stats, stats.audio_outbound_stats)},
    {"data", traffic(stats.data_inbound_stats, stats.data_outbound_stats)},
    {"total", traffic(stats.total_inbound_stats, stats.total_outbound_stats)}};
}
}  // namespace crossdesk::android_controller
