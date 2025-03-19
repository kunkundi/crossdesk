#ifndef _RTP_VIDEO_SENDER_H_
#define _RTP_VIDEO_SENDER_H_

#include <functional>

#include "api/clock/clock.h"
#include "clock/system_clock.h"
#include "io_statistics.h"
#include "receiver_report.h"
#include "ringbuffer.h"
#include "rtp_packet.h"
#include "rtp_packet_history.h"
#include "rtp_packet_to_send.h"
#include "rtp_statistics.h"
#include "sender_report.h"
#include "thread_base.h"

class RtpVideoSender : public ThreadBase {
 public:
  RtpVideoSender();
  RtpVideoSender(std::shared_ptr<SystemClock> clock,
                 std::shared_ptr<IOStatistics> io_statistics);
  virtual ~RtpVideoSender();

 public:
  void Enqueue(std::vector<std::unique_ptr<RtpPacket>> &rtp_packets,
               int64_t captured_timestamp_us);
  void SetSendDataFunc(std::function<int(const char *, size_t)> data_send_func);
  void SetOnSentPacketFunc(
      std::function<void(const webrtc::RtpPacketToSend &)> on_sent_packet_func);
  void SetEnqueuePacketsFunc(
      std::function<
          void(std::vector<std::unique_ptr<webrtc::RtpPacketToSend>> &)>
          enqueue_packets_func);
  uint32_t GetSsrc() { return ssrc_; }
  void OnReceiverReport(const ReceiverReport &receiver_report);

  int64_t GetTransportSequenceNumber() { return transport_seq_; }

  void IncrementTransportSequenceNumber() { transport_seq_++; }

 private:
  int SendRtpPacket(
      std::unique_ptr<webrtc::RtpPacketToSend> rtp_packet_to_send);
  int SendRtcpSR(SenderReport &rtcp_sr);

  bool CheckIsTimeSendSR();

 private:
  bool Process() override;

 private:
  std::function<int(const char *, size_t)> data_send_func_ = nullptr;
  std::function<void(const webrtc::RtpPacketToSend &)> on_sent_packet_func_ =
      nullptr;
  std::function<void(std::vector<std::unique_ptr<webrtc::RtpPacketToSend>> &)>
      enqueue_packets_func_ = nullptr;
  RingBuffer<std::unique_ptr<webrtc::RtpPacketToSend>> rtp_packet_queue_;

 private:
  uint32_t ssrc_ = 0;
  std::shared_ptr<webrtc::Clock> clock_ = nullptr;
  std::unique_ptr<RtpStatistics> rtp_statistics_ = nullptr;
  std::shared_ptr<IOStatistics> io_statistics_ = nullptr;
  std::unique_ptr<RtpPacketHistory> rtp_packet_history_ = nullptr;
  uint32_t last_send_bytes_ = 0;
  uint32_t last_send_rtcp_sr_packet_ts_ = 0;
  uint32_t total_rtp_payload_sent_ = 0;
  uint32_t total_rtp_packets_sent_ = 0;

  uint32_t last_rtp_timestamp_ = 0;
  int64_t last_frame_capture_time_ = 0;

 private:
  int64_t transport_seq_ = 0;

 private:
  FILE *file_rtp_sent_ = nullptr;
};

#endif