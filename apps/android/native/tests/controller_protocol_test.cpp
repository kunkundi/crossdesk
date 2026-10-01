#include "controller_protocol.h"
#include <cassert>
#include <cmath>
#include <limits>

int main() {
  using namespace crossdesk;
  for (int quality = -1; quality <= 3; ++quality)
    for (int rate : {0, 24, 30, 60, 120})
      for (int preference = -1; preference <= 3; ++preference) {
        const auto message = android_controller::VideoSettingsMessage(quality, rate, preference, UINT32_MAX);
        const bool valid = quality >= 0 && quality <= 2 && (rate == 30 || rate == 60) && preference >= 0 && preference <= 2;
        assert(message.empty() != valid);
        if (!valid) continue;
        // Decode the actual Android outgoing message with the desktop's wire parser.
        RemoteAction desktop{};
        assert(desktop.from_json(message) && desktop.type == video_settings);
        assert(desktop.vs.quality == quality && desktop.vs.frame_rate == rate && desktop.vs.preference == preference);
        assert(desktop.vs.request_id == UINT32_MAX && !desktop.vs.accepted);
        desktop.type = video_settings_status; desktop.vs.accepted = true;
        RemoteAction reply{}; assert(reply.from_json(desktop.to_json()));
        assert(reply.type == video_settings_status && reply.vs.accepted && reply.vs.request_id == UINT32_MAX);
      }
  MiniRtcNetTrafficStats stats{};
  stats.video_inbound_stats.bitrate = 8'000'000; stats.video_inbound_stats.loss_rate = .02f;
  stats.audio_inbound_stats.bitrate = 64'000; stats.audio_outbound_stats.bitrate = 128'000;
  stats.data_inbound_stats.bitrate = 2000; stats.data_outbound_stats.bitrate = 3000;
  stats.total_inbound_stats.bitrate = UINT32_MAX; stats.total_outbound_stats.bitrate = 131'000;
  stats.total_inbound_stats.loss_rate = 1.2f; stats.rtt_ms = 40; stats.srtp_active = true;
  auto json = android_controller::NetworkReport(stats, Relay);
  assert(json.at("mode") == 1 && json.at("srtp") == true && json.at("rtt") == 40);
  assert(json.at("video").at("inbound") == 8'000'000 && json.at("audio").at("outbound") == 128'000);
  assert(json.at("data").at("inbound") == 2000 && json.at("data").at("outbound") == 3000);
  assert(json.at("total").at("inbound").get<uint32_t>() == UINT32_MAX);
  assert(std::abs(json.at("total").at("loss").get<double>() - 1.2) < .001);
  stats.rtt_ms = -1; stats.srtp_active = false;
  json = android_controller::NetworkReport(stats, UnknownMode);
  assert(json.at("mode") == 2 && json.at("srtp") == false && json.at("rtt") == -1);
}
