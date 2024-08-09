#include "rtp_video_sender.h"

#include <chrono>

#include "log.h"

#define RTCP_SR_INTERVAL 1000

RtpVideoSender::RtpVideoSender() {}

RtpVideoSender::~RtpVideoSender() {
  if (rtp_statistics_) {
    rtp_statistics_->Stop();
  }
}

void RtpVideoSender::Enqueue(std::vector<RtpPacket>& rtp_packets) {
  if (!rtp_statistics_) {
    rtp_statistics_ = std::make_unique<RtpStatistics>();
    rtp_statistics_->Start();
  }

  for (auto& rtp_packet : rtp_packets) {
    rtp_packe_queue_.push(rtp_packet);
  }
}

void RtpVideoSender::SetSendDataFunc(
    std::function<int(const char*, size_t)> data_send_func) {
  data_send_func_ = data_send_func;
}

int RtpVideoSender::SendRtpPacket(RtpPacket& rtp_packet) {
  if (!data_send_func_) {
    LOG_ERROR("data_send_func_ is nullptr");
    return -1;
  }

  if (0 !=
      data_send_func_((const char*)rtp_packet.Buffer(), rtp_packet.Size())) {
    LOG_ERROR("Send rtp packet failed");
    return -1;
  }

  last_send_bytes_ += rtp_packet.Size();
  total_rtp_packets_sent_++;
  total_rtp_payload_sent_ += rtp_packet.PayloadSize();

  if (CheckIsTimeSendSR()) {
    RtcpSenderReport rtcp_sr;
    SenderInfo sender_info;
    RtcpReportBlock report;

    auto duration = std::chrono::system_clock::now().time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    uint32_t seconds_u32 = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(duration).count());

    uint32_t fraction_u32 = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds)
            .count());

    sender_info.sender_ssrc = 0x00;
    sender_info.ntp_ts_msw = (uint32_t)seconds_u32;
    sender_info.ntp_ts_lsw = (uint32_t)fraction_u32;
    sender_info.rtp_ts =
        std::chrono::system_clock::now().time_since_epoch().count() * 1000000;
    sender_info.sender_packet_count = total_rtp_packets_sent_;
    sender_info.sender_octet_count = total_rtp_payload_sent_;

    rtcp_sr.SetSenderInfo(sender_info);

    report.source_ssrc = 0x00;
    report.fraction_lost = 0;
    report.cumulative_lost = 0;
    report.extended_high_seq_num = 0;
    report.jitter = 0;
    report.lsr = 0;
    report.dlsr = 0;

    rtcp_sr.SetReportBlock(report);

    rtcp_sr.Encode();

    // SendRtcpSR(rtcp_sr);
  }

  return 0;
}

int RtpVideoSender::SendRtcpSR(RtcpSenderReport& rtcp_sr) {
  if (!data_send_func_) {
    LOG_ERROR("data_send_func_ is nullptr");
    return -1;
  }

  if (data_send_func_((const char*)rtcp_sr.Buffer(), rtcp_sr.Size())) {
    LOG_ERROR("Send SR failed");
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
  last_send_bytes_ = 0;

  for (size_t i = 0; i < 10; i++)
    if (!rtp_packe_queue_.isEmpty()) {
      RtpPacket rtp_packet;
      rtp_packe_queue_.pop(rtp_packet);
      SendRtpPacket(rtp_packet);
    }

  if (rtp_statistics_) {
    rtp_statistics_->UpdateSentBytes(last_send_bytes_);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  return true;
}