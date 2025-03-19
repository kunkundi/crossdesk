#include "video_channel_send.h"

#include "log.h"
#include "rtc_base/network/sent_packet.h"

VideoChannelSend::VideoChannelSend() {}

VideoChannelSend::~VideoChannelSend() {}

VideoChannelSend::VideoChannelSend(
    std::shared_ptr<SystemClock> clock, std::shared_ptr<IceAgent> ice_agent,
    std::shared_ptr<PacketSender> packet_sender,
    std::shared_ptr<IOStatistics> ice_io_statistics,
    std::function<void(const webrtc::RtpPacketToSend& packet)>
        on_sent_packet_func)
    : ice_agent_(ice_agent),
      packet_sender_(packet_sender),
      ice_io_statistics_(ice_io_statistics),
      on_sent_packet_func_(on_sent_packet_func),
      delta_ntp_internal_ms_(clock->CurrentNtpInMilliseconds() -
                             clock->CurrentTimeMs()),
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

void VideoChannelSend::SetEnqueuePacketsFunc(
    std::function<void(std::vector<std::unique_ptr<webrtc::RtpPacketToSend>>&)>
        enqueue_packets_func) {
  rtp_video_sender_->SetEnqueuePacketsFunc(enqueue_packets_func);
}

std::vector<std::unique_ptr<RtpPacket>> VideoChannelSend::GeneratePadding(
    uint32_t payload_size, int64_t captured_timestamp_us) {
  if (rtp_packetizer_) {
    return rtp_packetizer_->BuildPadding(payload_size, captured_timestamp_us,
                                         true);
  }
  return std::vector<std::unique_ptr<RtpPacket>>{};
}

void VideoChannelSend::Destroy() {
  if (rtp_video_sender_) {
    rtp_video_sender_->Stop();
  }
}

int VideoChannelSend::SendVideo(std::shared_ptr<EncodedFrame> encoded_frame) {
  if (rtp_video_sender_ && rtp_packetizer_) {
    int64_t rtp_timestamp =
        delta_ntp_internal_ms_ +
        static_cast<uint32_t>(encoded_frame->CapturedTimestamp() / 1000);
    std::vector<std::unique_ptr<RtpPacket>> rtp_packets =
        rtp_packetizer_->Build((uint8_t*)encoded_frame->Buffer(),
                               (uint32_t)encoded_frame->Size(), rtp_timestamp,
                               true);
    packet_sender_->EnqueueRtpPacket(std::move(rtp_packets), rtp_timestamp);
  }

  return 0;
}
