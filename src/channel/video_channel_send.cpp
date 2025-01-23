#include "video_channel_send.h"

#include "log.h"

VideoChannelSend::VideoChannelSend() {}

VideoChannelSend::~VideoChannelSend() {}

VideoChannelSend::VideoChannelSend(
    std::shared_ptr<IceAgent> ice_agent,
    std::shared_ptr<IOStatistics> ice_io_statistics)
    : ice_agent_(ice_agent), ice_io_statistics_(ice_io_statistics){};

void VideoChannelSend::Initialize(rtp::PAYLOAD_TYPE payload_type) {
  rtp_packetizer_ = RtpPacketizer::Create(payload_type);
  rtp_video_sender_ = std::make_unique<RtpVideoSender>(ice_io_statistics_);
  rtp_video_sender_->SetSendDataFunc(
      [this](const char* data, size_t size) -> int {
        if (!ice_agent_) {
          LOG_ERROR("ice_agent_ is nullptr");
          return -1;
        }

        auto ice_state = ice_agent_->GetIceState();

        if (ice_state != NICE_COMPONENT_STATE_CONNECTED &&
            ice_state != NICE_COMPONENT_STATE_READY) {
          LOG_ERROR("Ice is not connected, state = [{}]",
                    nice_component_state_to_string(ice_state));
          return -2;
        }

        ice_io_statistics_->UpdateVideoOutboundBytes((uint32_t)size);
        return ice_agent_->Send(data, size);
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

void VideoChannelSend::OnCongestionControlFeedback(
    int64_t recv_ts, const webrtc::rtcp::CongestionControlFeedback& feedback) {
  ++feedback_count_;
  std::optional<webrtc::TransportPacketsFeedback> feedback_msg =
      transport_feedback_adapter_.ProcessCongestionControlFeedback(
          feedback, webrtc::Timestamp::Micros(recv_ts));
  if (feedback_msg) {
    HandleTransportPacketsFeedback(*feedback_msg);
  }
}

void VideoChannelSend::HandleTransportPacketsFeedback(
    const webrtc::TransportPacketsFeedback& feedback) {
  // if (transport_is_ecn_capable_) {
  //   // If transport does not support ECN, packets should not be sent as
  //   ECT(1).
  //   // TODO: bugs.webrtc.org/42225697 - adapt to ECN feedback and continue to
  //   // send packets as ECT(1) if transport is ECN capable.
  //   transport_is_ecn_capable_ = false;
  //   LOG_INFO("Transport is {} ECN capable. Stop sending ECT(1)",
  //            (feedback.transport_supports_ecn ? "" : " not "));
  // }
  // if (controller_)
  //   PostUpdates(controller_->OnTransportPacketsFeedback(feedback));

  // // Only update outstanding data if any packet is first time acked.
  // UpdateCongestedState();
}