#include "rtcp_sender.h"

RtcpSender::RtcpSender(std::shared_ptr<webrtc::Clock> clock, uint32_t ssrc)
    : clock_(clock), ssrc_(ssrc) {}

RtcpSender::~RtcpSender() {}

void RtcpSender::BuildSR(const RtcpContext& ctx, PacketSender& sender) {
  // The timestamp of this RTCP packet should be estimated as the timestamp of
  // the frame being captured at this moment. We are calculating that
  // timestamp as the last frame's timestamp + the time since the last frame
  // was captured.
  int rtp_rate = rtp_clock_rates_khz_[last_payload_type_];
  if (rtp_rate <= 0) {
    rtp_rate =
        (audio_ ? kBogusRtpRateForAudioRtcp : kVideoPayloadTypeFrequency) /
        1000;
  }
  // Round now_us_ to the closest millisecond, because Ntp time is rounded
  // when converted to milliseconds,
  uint32_t rtp_timestamp =
      timestamp_offset_ + last_rtp_timestamp_ +
      ((ctx.now_.us() + 500) / 1000 - last_frame_capture_time_->ms()) *
          rtp_rate;

  rtcp::SenderReport report;
  report.SetSenderSsrc(ssrc_);
  report.SetNtp(env_.clock().ConvertTimestampToNtpTime(ctx.now_));
  report.SetRtpTimestamp(rtp_timestamp);
  report.SetPacketCount(ctx.feedback_state_.packets_sent);
  report.SetOctetCount(ctx.feedback_state_.media_bytes_sent);
  report.SetReportBlocks(CreateReportBlocks(ctx.feedback_state_));
  sender.AppendPacket(report);
}

void RtcpSender::BuildRR(const RtcpContext& ctx, PacketSender& sender) {
  rtcp::ReceiverReport report;
  report.SetSenderSsrc(ssrc_);
  report.SetReportBlocks(CreateReportBlocks(ctx.feedback_state_));
  if (method_ == RtcpMode::kCompound || !report.report_blocks().empty()) {
    sender.AppendPacket(report);
  }
}