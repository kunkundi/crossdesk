#ifndef _RTP_DATA_RECEIVER_H_
#define _RTP_DATA_RECEIVER_H_

#include <functional>

#include "io_statistics.h"
#include "receiver_report.h"
#include "rtp_packet.h"
#include "rtp_statistics.h"
#include "sender_report.h"
class RtpDataReceiver {
 public:
  RtpDataReceiver();
  RtpDataReceiver(std::shared_ptr<IOStatistics> io_statistics);
  ~RtpDataReceiver();

 public:
  void InsertRtpPacket(RtpPacket& rtp_packet);

  void SetSendDataFunc(std::function<int(const char*, size_t)> data_send_func);

  void SetOnReceiveData(
      std::function<void(const char*, size_t)> on_receive_data) {
    on_receive_data_ = on_receive_data;
  }

  uint32_t GetSsrc() { return ssrc_; }
  uint32_t GetRemoteSsrc() { return remote_ssrc_; }
  void OnSenderReport(const SenderReport& sender_report) {}

 private:
  bool CheckIsTimeSendRR();
  int SendRtcpRR(ReceiverReport& rtcp_rr);

 private:
  std::function<void(const char*, size_t)> on_receive_data_ = nullptr;
  uint32_t last_complete_frame_ts_ = 0;

 private:
  std::unique_ptr<RtpStatistics> rtp_statistics_ = nullptr;
  std::shared_ptr<IOStatistics> io_statistics_ = nullptr;
  uint32_t last_recv_bytes_ = 0;
  uint32_t total_rtp_payload_recv_ = 0;
  uint32_t total_rtp_packets_recv_ = 0;

  uint32_t ssrc_ = 0;
  uint32_t remote_ssrc_ = 0;
  uint32_t last_send_rtcp_rr_packet_ts_ = 0;
  std::function<int(const char*, size_t)> data_send_func_ = nullptr;

  uint32_t last_sr_ = 0;
  uint32_t last_delay_ = 0;
};

#endif