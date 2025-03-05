#include "video_channel_send.h"

#include "log.h"
#include "rtc_base/network/sent_packet.h"

VideoChannelSend::VideoChannelSend() {}

VideoChannelSend::~VideoChannelSend() {}

VideoChannelSend::VideoChannelSend(
    std::shared_ptr<SystemClock> clock, std::shared_ptr<IceAgent> ice_agent,
    std::shared_ptr<IOStatistics> ice_io_statistics,
    std::function<void(const webrtc::RtpPacketToSend& packet)>
        on_sent_packet_func)
    : ice_agent_(ice_agent),
      ice_io_statistics_(ice_io_statistics),
      on_sent_packet_func_(on_sent_packet_func),
      clock_(clock){};

void VideoChannelSend::Initialize(rtp::PAYLOAD_TYPE payload_type) {
  rtp_video_sender_ =
      std::make_unique<RtpVideoSender>(clock_, ice_io_statistics_);
  rtp_packetizer_ =
      RtpPacketizer::Create(payload_type, rtp_video_sender_->GetSsrc());
  rtp_video_sender_->SetSendDataFunc(
      [this](const char* data, size_t size) -> int {
        if (!ice_agent_) {
          LOG_ERROR("ice_agent_ is nullptr");
          return -1;
        }

        auto ice_state = ice_agent_->GetIceState();

        if (ICE_STATE_DESTROYED == ice_state) {
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

int VideoChannelSend::SendVideo(
    std::shared_ptr<VideoFrameWrapper> encoded_frame) {
  if (rtp_video_sender_ && rtp_packetizer_) {
    std::vector<std::shared_ptr<RtpPacket>> rtp_packets =
        rtp_packetizer_->Build((uint8_t*)encoded_frame->Buffer(),
                               (uint32_t)encoded_frame->Size(),
                               encoded_frame->CaptureTimestamp(), true);
    rtp_video_sender_->Enqueue(rtp_packets, encoded_frame->CaptureTimestamp());
  }

  return 0;
}
