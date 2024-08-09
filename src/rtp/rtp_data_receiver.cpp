#include "rtp_data_receiver.h"

#define RTCP_RR_INTERVAL 1000

RtpDataReceiver::RtpDataReceiver() {}

RtpDataReceiver::~RtpDataReceiver() {
  if (rtp_statistics_) {
    rtp_statistics_->Stop();
  }
}

void RtpDataReceiver::InsertRtpPacket(RtpPacket& rtp_packet) {
  if (!rtp_statistics_) {
    rtp_statistics_ = std::make_unique<RtpStatistics>();
    rtp_statistics_->Start();
  }

  if (rtp_statistics_) {
    rtp_statistics_->UpdateReceiveBytes(rtp_packet.Size());
  }

  if (CheckIsTimeSendRR()) {
    RtcpReceiverReport rtcp_rr;
    RtcpReportBlock report;

    // auto duration = std::chrono::system_clock::now().time_since_epoch();
    // auto seconds =
    // std::chrono::duration_cast<std::chrono::seconds>(duration); uint32_t
    // seconds_u32 = static_cast<uint32_t>(
    //     std::chrono::duration_cast<std::chrono::seconds>(duration).count());

    // uint32_t fraction_u32 = static_cast<uint32_t>(
    //     std::chrono::duration_cast<std::chrono::nanoseconds>(duration -
    //     seconds)
    //         .count());

    report.source_ssrc = 0x00;
    report.fraction_lost = 0;
    report.cumulative_lost = 0;
    report.extended_high_seq_num = 0;
    report.jitter = 0;
    report.lsr = 0;
    report.dlsr = 0;

    rtcp_rr.SetReportBlock(report);

    rtcp_rr.Encode();

    SendRtcpRR(rtcp_rr);
  }

  if (on_receive_data_) {
    on_receive_data_((const char*)rtp_packet.Payload(),
                     rtp_packet.PayloadSize());
  }
}

void RtpDataReceiver::SetSendDataFunc(
    std::function<int(const char*, size_t)> data_send_func) {
  data_send_func_ = data_send_func;
}

int RtpDataReceiver::SendRtcpRR(RtcpReceiverReport& rtcp_rr) {
  if (!data_send_func_) {
    LOG_ERROR("data_send_func_ is nullptr");
    return -1;
  }

  if (data_send_func_((const char*)rtcp_rr.Buffer(), rtcp_rr.Size())) {
    LOG_ERROR("Send RR failed");
    return -1;
  }

  // LOG_ERROR("Send RR");

  return 0;
}

bool RtpDataReceiver::CheckIsTimeSendRR() {
  uint32_t now_ts = static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  if (now_ts - last_send_rtcp_rr_packet_ts_ >= RTCP_RR_INTERVAL) {
    last_send_rtcp_rr_packet_ts_ = now_ts;
    return true;
  } else {
    return false;
  }
}