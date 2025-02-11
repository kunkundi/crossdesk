#include "video_channel_send.h"

#include "log.h"
#include "rtc_base/network/sent_packet.h"

VideoChannelSend::VideoChannelSend() {}

VideoChannelSend::~VideoChannelSend() {}

VideoChannelSend::VideoChannelSend(
    std::shared_ptr<webrtc::Clock> clock, std::shared_ptr<IceAgent> ice_agent,
    std::shared_ptr<IOStatistics> ice_io_statistics,
    std::function<void(const webrtc::RtpPacketToSend& packet)>
        on_sent_packet_func)
    : ice_agent_(ice_agent),
      ice_io_statistics_(ice_io_statistics),
      on_sent_packet_func_(on_sent_packet_func),
      clock_(clock){};

void VideoChannelSend::Initialize(rtp::PAYLOAD_TYPE payload_type) {
  rtp_video_sender_ = std::make_unique<RtpVideoSender>(ice_io_statistics_);
  rtp_packetizer_ =
      RtpPacketizer::Create(payload_type, rtp_video_sender_->GetSsrc());
  rtp_video_sender_->SetSendDataFunc(
      [this](const char* data, size_t size) -> int {
        if (!ice_agent_) {
          LOG_ERROR("ice_agent_ is nullptr");
          return -1;
        }

        auto ice_state = ice_agent_->GetIceState();

        if (ice_state != NICE_COMPONENT_STATE_CONNECTED &&
            ice_state != NICE_COMPONENT_STATE_READY) {
          // LOG_ERROR("Ice is not connected, state = [{}]",
          //           nice_component_state_to_string(ice_state));
          return -2;
        }

        ice_io_statistics_->UpdateVideoOutboundBytes((uint32_t)size);

        return ice_agent_->Send(data, size);
      });

  rtp_video_sender_->SetOnSentPacketFunc(
      [this](const webrtc::RtpPacketToSend& packet) -> void {
        on_sent_packet_func_(packet);
      });

  rtp_video_sender_->Start();
}

void VideoChannelSend::Destroy() {
  if (rtp_video_sender_) {
    rtp_video_sender_->Stop();
  }
}

int VideoChannelSend::SendVideo(char* data, size_t size) {
  if (rtp_video_sender_ && rtp_packetizer_) {
    std::vector<RtpPacket> rtp_packets =
        rtp_packetizer_->Build((uint8_t*)data, (uint32_t)size);
    rtp_video_sender_->Enqueue(rtp_packets);
  }

  return 0;
}
