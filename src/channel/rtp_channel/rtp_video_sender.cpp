#include "rtp_video_sender.h"

#include <chrono>

#include "api/clock/clock.h"
#include "common.h"
#include "log.h"

// #define SAVE_RTP_SENT_STREAM

#define RTCP_SR_INTERVAL 1000

RtpVideoSender::RtpVideoSender() {}

RtpVideoSender::RtpVideoSender(std::shared_ptr<SystemClock> clock,
                               std::shared_ptr<IOStatistics> io_statistics)
    : ssrc_(GenerateUniqueSsrc()),
      io_statistics_(io_statistics),
      rtp_packet_history_(std::make_unique<RtpPacketHistory>(clock_)),
      clock_(webrtc::Clock::GetWebrtcClockShared(clock)) {
  SetPeriod(std::chrono::milliseconds(5));
#ifdef SAVE_RTP_SENT_STREAM
  file_rtp_sent_ = fopen("rtp_sent_stream.h264", "w+b");
  if (!file_rtp_sent_) {
    LOG_WARN("Fail to open rtp_sent_stream.h264");
  }
#endif
}

RtpVideoSender::~RtpVideoSender() {
  if (rtp_statistics_) {
    rtp_statistics_->Stop();
  }

  SSRCManager::Instance().DeleteSsrc(ssrc_);

#ifdef SAVE_RTP_SENT_STREAM
  if (file_rtp_sent_) {
    fflush(file_rtp_sent_);
    fclose(file_rtp_sent_);
    file_rtp_sent_ = nullptr;
  }
#endif
}

void RtpVideoSender::Enqueue(
    std::vector<std::shared_ptr<RtpPacket>>& rtp_packets,
    int64_t capture_timestamp_ms) {
  if (!rtp_statistics_) {
    rtp_statistics_ = std::make_unique<RtpStatistics>();
    rtp_statistics_->Start();
  }

  for (auto& rtp_packet : rtp_packets) {
    std::shared_ptr<webrtc::RtpPacketToSend> rtp_packet_to_send =
        std::dynamic_pointer_cast<webrtc::RtpPacketToSend>(rtp_packet);
    rtp_packet_to_send->set_capture_time(
        webrtc::Timestamp::Millis(capture_timestamp_ms));
    rtp_packet_to_send->set_transport_sequence_number(transport_seq_++);
    rtp_packet_to_send->set_packet_type(webrtc::RtpPacketMediaType::kVideo);
    rtp_packet_queue_.push(std::move(rtp_packet_to_send));
  }
}

void RtpVideoSender::SetSendDataFunc(
    std::function<int(const char*, size_t)> data_send_func) {
  data_send_func_ = data_send_func;
}

void RtpVideoSender::SetOnSentPacketFunc(
    std::function<void(const webrtc::RtpPacketToSend&)> on_sent_packet_func) {
  on_sent_packet_func_ = on_sent_packet_func;
}

int RtpVideoSender::SendRtpPacket(
    std::shared_ptr<webrtc::RtpPacketToSend> rtp_packet_to_send) {
  if (!data_send_func_) {
    LOG_ERROR("data_send_func_ is nullptr");
    return -1;
  }

  if (on_sent_packet_func_) {
    on_sent_packet_func_(*rtp_packet_to_send);
    rtp_packet_history_->AddPacket(rtp_packet_to_send, clock_->CurrentTime());
  }

  last_rtp_timestamp_ = rtp_packet_to_send->capture_time().ms();

  int ret = data_send_func_((const char*)rtp_packet_to_send->Buffer().data(),
                            rtp_packet_to_send->Size());
  if (-2 == ret) {
    rtp_packet_queue_.clear();
    return -1;
  }

#ifdef SAVE_RTP_SENT_STREAM
  fwrite((unsigned char*)rtp_packet_to_send->Payload(), 1,
         x rtp_packet_to_send->PayloadSize(), file_rtp_sent_);
#endif

  last_send_bytes_ += (uint32_t)rtp_packet_to_send->Size();
  total_rtp_payload_sent_ += (uint32_t)rtp_packet_to_send->PayloadSize();
  total_rtp_packets_sent_++;

  if (io_statistics_) {
    io_statistics_->UpdateVideoOutboundBytes(last_send_bytes_);
    io_statistics_->IncrementVideoOutboundRtpPacketCount();
  }

  if (CheckIsTimeSendSR()) {
    SenderReport rtcp_sr;
    rtcp_sr.SetSenderSsrc(ssrc_);

    uint32_t rtp_timestamp =
        last_rtp_timestamp_ +
        ((clock_->CurrentTime().us() + 500) / 1000 - last_frame_capture_time_) *
            rtp::kVideoPayloadTypeFrequency;
    rtcp_sr.SetTimestamp(rtp_timestamp);
    rtcp_sr.SetNtpTimestamp((uint64_t)clock_->CurrentNtpTime());
    rtcp_sr.SetSenderPacketCount(total_rtp_packets_sent_);
    rtcp_sr.SetSenderOctetCount(total_rtp_payload_sent_);

    rtcp_sr.Build();
    SendRtcpSR(rtcp_sr);
  }

  return 0;
}

int RtpVideoSender::SendRtcpSR(SenderReport& rtcp_sr) {
  if (!data_send_func_) {
    LOG_ERROR("data_send_func_ is nullptr");
    return -1;
  }

  int ret = data_send_func_((const char*)rtcp_sr.Buffer(), rtcp_sr.Size());
  if (ret != 0) {
    LOG_ERROR("Send rtcp sr failed");
    return -1;
  }

  // LOG_ERROR("Send SR");

  return 0;
}

bool RtpVideoSender::CheckIsTimeSendSR() {
  uint32_t now_ts = static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  if (now_ts - last_send_rtcp_sr_packet_ts_ >= RTCP_SR_INTERVAL) {
    last_send_rtcp_sr_packet_ts_ = now_ts;
    return true;
  } else {
    return false;
  }
}

bool RtpVideoSender::Process() {
  bool pop_success = false;
  last_send_bytes_ = 0;

  for (size_t i = 0; i < 10; i++)
    if (!rtp_packet_queue_.isEmpty()) {
      std::shared_ptr<webrtc::RtpPacketToSend> rtp_packet_to_send;
      pop_success = rtp_packet_queue_.pop(rtp_packet_to_send);
      if (pop_success) {
        SendRtpPacket(rtp_packet_to_send);
      }
    }

  if (rtp_statistics_) {
    rtp_statistics_->UpdateSentBytes(last_send_bytes_);
  }

  return true;
}

void RtpVideoSender::OnReceiverReport(const ReceiverReport& receiver_report) {
  std::vector<RtcpReportBlock> reports = receiver_report.GetReportBlocks();

  for (auto r : reports) {
    LOG_WARN(
        "r_ssrc [{}], f_lost [{}], c_lost [{}], h_seq [{}], jitter [{}], "
        "lsr [{}], dlsr [{}] ",
        r.SourceSsrc(), r.FractionLost() / 255.0, r.CumulativeLost(),
        r.ExtendedHighSeqNum(), r.Jitter(), r.LastSr(), r.DelaySinceLastSr());
  }
}