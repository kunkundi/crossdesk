#include "video_channel_receive.h"

#include "log.h"

VideoChannelReceive::VideoChannelReceive() {}

VideoChannelReceive::VideoChannelReceive(
    std::shared_ptr<SystemClock> clock, std::shared_ptr<IceAgent> ice_agent,
    std::shared_ptr<IOStatistics> ice_io_statistics,
    std::function<void(VideoFrame &)> on_receive_complete_frame)
    : ice_agent_(ice_agent),
      ice_io_statistics_(ice_io_statistics),
      on_receive_complete_frame_(on_receive_complete_frame),
      clock_(clock) {}

VideoChannelReceive::~VideoChannelReceive() {}

void VideoChannelReceive::Initialize(rtp::PAYLOAD_TYPE payload_type) {
  rtp_video_receiver_ =
      std::make_unique<RtpVideoReceiver>(clock_, ice_io_statistics_);
  rtp_video_receiver_->SetOnReceiveCompleteFrame(
      [this](VideoFrame &video_frame) -> void {
        on_receive_complete_frame_(video_frame);
      });

  rtp_video_receiver_->SetSendDataFunc([this](const char *data,
                                              size_t size) -> int {
    if (!ice_agent_) {
      LOG_ERROR("ice_agent_ is nullptr");
      return -1;
    }

    auto ice_state = ice_agent_->GetIceState();

    if (ICE_STATE_NULLPTR == ice_state || ICE_STATE_DESTROYED == ice_state) {
      LOG_ERROR("Ice is not connected, state = [{}]", (int)ice_state);
      return -2;
    }

    ice_io_statistics_->UpdateVideoOutboundBytes((uint32_t)size);
    return ice_agent_->Send(data, size);
  });

  rtp_video_receiver_->Start();
}

void VideoChannelReceive::Destroy() {
  if (rtp_video_receiver_) {
    rtp_video_receiver_->Stop();
  }
}

int VideoChannelReceive::OnReceiveRtpPacket(const char *data, size_t size) {
  if (ice_io_statistics_) {
    ice_io_statistics_->UpdateVideoInboundBytes((uint32_t)size);
  }

  if (rtp_video_receiver_) {
    RtpPacket rtp_packet;
    rtp_packet.Build((uint8_t *)data, (uint32_t)size);
    rtp_video_receiver_->InsertRtpPacket(rtp_packet);
  }

  return 0;
}
