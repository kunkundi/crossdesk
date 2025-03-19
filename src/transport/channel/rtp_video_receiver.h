#ifndef _RTP_VIDEO_RECEIVER_H_
#define _RTP_VIDEO_RECEIVER_H_

#include <functional>
#include <map>
#include <queue>
#include <set>
#include <unordered_set>

#include "api/clock/clock.h"
#include "clock/system_clock.h"
#include "fec_decoder.h"
#include "io_statistics.h"
#include "nack_requester.h"
#include "receive_side_congestion_controller.h"
#include "received_frame.h"
#include "receiver_report.h"
#include "ringbuffer.h"
#include "rtcp_sender.h"
#include "rtp_packet_av1.h"
#include "rtp_packet_h264.h"
#include "rtp_rtcp_defines.h"
#include "rtp_statistics.h"
#include "sender_report.h"
#include "thread_base.h"

using namespace webrtc;

class RtpVideoReceiver : public ThreadBase,
                         public LossNotificationSender,
                         public KeyFrameRequestSender,
                         public NackSender {
 public:
  RtpVideoReceiver(std::shared_ptr<SystemClock> clock);
  RtpVideoReceiver(std::shared_ptr<SystemClock> clock,
                   std::shared_ptr<IOStatistics> io_statistics);
  virtual ~RtpVideoReceiver();

 public:
  void InsertRtpPacket(RtpPacket& rtp_packet);

  void SetSendDataFunc(std::function<int(const char*, size_t)> data_send_func);

  void SetOnReceiveCompleteFrame(
      std::function<void(const ReceivedFrame&)> on_receive_complete_frame) {
    on_receive_complete_frame_ = on_receive_complete_frame;
  }
  uint32_t GetSsrc() { return ssrc_; }
  uint32_t GetRemoteSsrc() { return remote_ssrc_; }
  void OnSenderReport(const SenderReport& sender_report);

 private:
  void ProcessAv1RtpPacket(RtpPacketAv1& rtp_packet_av1);
  bool CheckIsAv1FrameCompleted(RtpPacketAv1& rtp_packet_av1);

 private:
  void ProcessH264RtpPacket(RtpPacketH264& rtp_packet_h264);
  bool CheckIsH264FrameCompleted(RtpPacketH264& rtp_packet_h264);

 private:
  bool CheckIsTimeSendRR();
  int SendRtcpRR(ReceiverReport& rtcp_rr);

  void SendCombinedRtcpPacket(
      std::vector<std::unique_ptr<RtcpPacket>> rtcp_packets);

  void SendRemb(int64_t bitrate_bps, std::vector<uint32_t> ssrcs);

 private:
  bool Process() override;
  void RtcpThread();

 private:
  void SendNack(const std::vector<uint16_t>& nack_list,
                bool buffering_allowed) override;

  void SendRR();

  void RequestKeyFrame();

  void SendLossNotification(uint16_t last_decoded_seq_num,
                            uint16_t last_received_seq_num,
                            bool decodability_flag, bool buffering_allowed);

  void ReviseFrequencyAndJitter(int payload_type_frequency);

 private:
  std::map<uint16_t, RtpPacketH264> incomplete_h264_frame_list_;
  std::map<uint16_t, RtpPacketAv1> incomplete_av1_frame_list_;
  std::map<uint16_t, RtpPacket> incomplete_frame_list_;
  uint8_t* nv12_data_ = nullptr;
  std::function<void(const ReceivedFrame&)> on_receive_complete_frame_ =
      nullptr;
  uint32_t last_complete_frame_ts_ = 0;
  RingBuffer<ReceivedFrame> compelete_video_frame_queue_;

 private:
  std::unique_ptr<RtpStatistics> rtp_statistics_ = nullptr;
  std::shared_ptr<IOStatistics> io_statistics_ = nullptr;
  uint32_t last_recv_bytes_ = 0;
  uint32_t total_rtp_packets_recv_ = 0;
  uint32_t total_rtp_payload_recv_ = 0;

  uint32_t last_send_rtcp_rr_packet_ts_ = 0;
  std::function<int(const char*, size_t)> data_send_func_ = nullptr;

 private:
  bool fec_enable_ = false;
  FecDecoder fec_decoder_;
  uint64_t last_packet_ts_ = 0;
  // std::map<uint16_t, RtpPacket> incomplete_fec_frame_list_;
  // std::map<uint32_t, std::map<uint16_t, RtpPacket>> fec_source_symbol_list_;
  // std::map<uint32_t, std::map<uint16_t, RtpPacket>> fec_repair_symbol_list_;
  std::set<uint64_t> incomplete_fec_frame_list_;
  std::map<uint64_t, std::map<uint16_t, RtpPacket>> incomplete_fec_packet_list_;
  std::unordered_set<int> padding_sequence_numbers_;

 private:
  std::thread rtcp_thread_;
  std::mutex rtcp_mtx_;
  std::condition_variable rtcp_cv_;
  std::chrono::steady_clock::time_point last_send_rtcp_rr_ts_;
  std::atomic<bool> send_rtcp_rr_triggered_ = false;
  std::atomic<bool> rtcp_stop_ = false;
  int rtcp_rr_interval_ms_ = 5000;
  int rtcp_tcc_interval_ms_ = 200;

 private:
  uint32_t ssrc_ = 0;
  uint32_t remote_ssrc_ = 0;
  std::shared_ptr<webrtc::Clock> clock_;
  ReceiveSideCongestionController receive_side_congestion_controller_;
  RtcpFeedbackSenderInterface* active_remb_module_;

  std::unique_ptr<RtcpSender> rtcp_sender_;
  std::unique_ptr<NackRequester> nack_;

  uint8_t fraction_lost_ = 0;
  int32_t cumulative_lost_ = 0;
  uint32_t jitter_ = 0;
  uint16_t extended_high_seq_num_ = 0;
  uint32_t last_sr_ = 0;

  int32_t cumulative_loss_ = 0;
  int32_t last_report_cumulative_loss_ = 0;
  int32_t cumulative_loss_rtcp_offset_ = 0;
  std::optional<Timestamp> last_receive_time_;
  int last_payload_type_frequency_ = 0;
  uint16_t last_extended_high_seq_num_ = 0;
  uint32_t jitter_q4_ = 0;
  uint32_t last_received_timestamp_ = 0;

  uint32_t remote_ssrc = 0;
  uint32_t last_remote_ntp_timestamp = 0;
  uint32_t last_remote_rtp_timestamp = 0;
  uint32_t last_arrival_timestamp = 0;
  uint32_t last_arrival_ntp_timestamp = 0;
  uint32_t packets_sent = 0;
  uint32_t bytes_sent = 0;
  uint32_t reports_count = 0;

  uint8_t sequence_number_fir_ = 0;

 private:
  FILE* file_rtp_recv_ = nullptr;
  int64_t delta_ntp_internal_ms_;
};

#endif
