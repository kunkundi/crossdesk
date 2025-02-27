#include "rtp_video_receiver.h"

#include "api/ntp/ntp_time_util.h"
#include "common.h"
#include "log.h"
#include "nack.h"
#include "rtcp_sender.h"

// #define SAVE_RTP_RECV_STREAM

#define NV12_BUFFER_SIZE (1280 * 720 * 3 / 2)
#define RTCP_RR_INTERVAL 1000

RtpVideoReceiver::RtpVideoReceiver(std::shared_ptr<SystemClock> clock)
    : feedback_ssrc_(GenerateUniqueSsrc()),
      active_remb_module_(nullptr),
      receive_side_congestion_controller_(
          clock_,
          [this](std::vector<std::unique_ptr<RtcpPacket>> packets) {
            SendCombinedRtcpPacket(std::move(packets));
          },
          [this](int64_t bitrate_bps, std::vector<uint32_t> ssrcs) {
            SendRemb(bitrate_bps, ssrcs);
          }),
      rtcp_sender_(std::make_unique<RtcpSender>(
          [this](const uint8_t* buffer, size_t size) -> int {
            return data_send_func_((const char*)buffer, size);
          },
          1200)),
      nack_(std::make_unique<NackRequester>(clock_, this, this)),
      clock_(webrtc::Clock::GetWebrtcClockShared(clock)) {
  SetPeriod(std::chrono::milliseconds(5));
  // rtcp_thread_ = std::thread(&RtpVideoReceiver::RtcpThread, this);
}

RtpVideoReceiver::RtpVideoReceiver(std::shared_ptr<SystemClock> clock,
                                   std::shared_ptr<IOStatistics> io_statistics)
    : io_statistics_(io_statistics),
      feedback_ssrc_(GenerateUniqueSsrc()),
      receive_side_congestion_controller_(
          clock_,
          [this](std::vector<std::unique_ptr<RtcpPacket>> packets) {
            SendCombinedRtcpPacket(std::move(packets));
          },
          [this](int64_t bitrate_bps, std::vector<uint32_t> ssrcs) {
            SendRemb(bitrate_bps, ssrcs);
          }),
      rtcp_sender_(std::make_unique<RtcpSender>(
          [this](const uint8_t* buffer, size_t size) -> int {
            return data_send_func_((const char*)buffer, size);
          },
          1200)),
      nack_(std::make_unique<NackRequester>(clock_, this, this)),
      clock_(webrtc::Clock::GetWebrtcClockShared(clock)) {
  SetPeriod(std::chrono::milliseconds(5));
  // rtcp_thread_ = std::thread(&RtpVideoReceiver::RtcpThread, this);

#ifdef SAVE_RTP_RECV_STREAM
  file_rtp_recv_ = fopen("rtp_recv_stream.h264", "w+b");
  if (!file_rtp_recv_) {
    LOG_WARN("Fail to open rtp_recv_stream.h264");
  }
#endif
}

RtpVideoReceiver::~RtpVideoReceiver() {
  rtcp_stop_.store(true);
  rtcp_cv_.notify_all();
  if (rtcp_thread_.joinable()) {
    rtcp_thread_.join();
  }

  SSRCManager::Instance().DeleteSsrc(feedback_ssrc_);

  if (rtp_statistics_) {
    rtp_statistics_->Stop();
  }

#ifdef SAVE_RTP_RECV_STREAM
  if (file_rtp_recv_) {
    fflush(file_rtp_recv_);
    fclose(file_rtp_recv_);
    file_rtp_recv_ = nullptr;
  }
#endif
}

void RtpVideoReceiver::InsertRtpPacket(RtpPacket& rtp_packet) {
  if (!rtp_statistics_) {
    rtp_statistics_ = std::make_unique<RtpStatistics>();
    rtp_statistics_->Start();
  }

  remote_ssrc_ = rtp_packet.Ssrc();

#ifdef SAVE_RTP_RECV_STREAM
  fwrite((unsigned char*)rtp_packet.Payload(), 1, rtp_packet.PayloadSize(),
         file_rtp_recv_);
#endif

  webrtc::RtpPacketReceived rtp_packet_received;
  rtp_packet_received.Build(rtp_packet.Buffer().data(), rtp_packet.Size());
  rtp_packet_received.set_arrival_time(clock_->CurrentTime());
  rtp_packet_received.set_ecn(EcnMarking::kEct0);
  rtp_packet_received.set_recovered(false);
  rtp_packet_received.set_payload_type_frequency(0);
  receive_side_congestion_controller_.OnReceivedPacket(rtp_packet_received,
                                                       MediaType::VIDEO);

  nack_->OnReceivedPacket(rtp_packet.SequenceNumber());

  last_recv_bytes_ = (uint32_t)rtp_packet.PayloadSize();
  total_rtp_payload_recv_ += (uint32_t)rtp_packet.PayloadSize();
  total_rtp_packets_recv_++;

  if (rtp_statistics_) {
    rtp_statistics_->UpdateReceiveBytes(last_recv_bytes_);
  }

  if (io_statistics_) {
    io_statistics_->UpdateVideoInboundBytes(last_recv_bytes_);
    io_statistics_->IncrementVideoInboundRtpPacketCount();
    io_statistics_->UpdateVideoPacketLossCount(rtp_packet.SequenceNumber());
  }

  // if (CheckIsTimeSendRR()) {
  //   ReceiverReport rtcp_rr;
  //   RtcpReportBlock report;

  //   // auto duration = std::chrono::system_clock::now().time_since_epoch();
  //   // auto seconds =
  //   // std::chrono::duration_cast<std::chrono::seconds>(duration); uint32_t
  //   // seconds_u32 = static_cast<uint32_t>(
  //   // std::chrono::duration_cast<std::chrono::seconds>(duration).count());

  //   // uint32_t fraction_u32 = static_cast<uint32_t>(
  //   //     std::chrono::duration_cast<std::chrono::nanoseconds>(duration -
  //   //     seconds)
  //   //         .count());

  //   report.source_ssrc = 0x00;
  //   report.fraction_lost = 0;
  //   report.cumulative_lost = 0;
  //   report.extended_high_seq_num = 0;
  //   report.jitter = 0;
  //   report.lsr = 0;
  //   report.dlsr = 0;

  //   rtcp_rr.SetReportBlock(report);

  //   rtcp_rr.Encode();

  //   // SendRtcpRR(rtcp_rr);
  // }

  if (rtp_packet.PayloadType() == rtp::PAYLOAD_TYPE::AV1) {
    RtpPacketAv1 rtp_packet_av1;
    rtp_packet_av1.Build(rtp_packet.Buffer().data(), rtp_packet.Size());
    rtp_packet_av1.GetFrameHeaderInfo();
    ProcessAv1RtpPacket(rtp_packet_av1);
  } else {
    RtpPacketH264 rtp_packet_h264;
    if (rtp_packet_h264.Build(rtp_packet.Buffer().data(), rtp_packet.Size())) {
      rtp_packet_h264.GetFrameHeaderInfo();
      ProcessH264RtpPacket(rtp_packet_h264);
    } else {
      LOG_ERROR("Invalid h264 rtp packet");
    }
  }
}

void RtpVideoReceiver::ProcessH264RtpPacket(RtpPacketH264& rtp_packet_h264) {
  if (!fec_enable_) {
    if (rtp::PAYLOAD_TYPE::H264 == rtp_packet_h264.PayloadType()) {
      rtp::NAL_UNIT_TYPE nalu_type = rtp_packet_h264.NalUnitType();
      if (rtp::NAL_UNIT_TYPE::NALU == nalu_type) {
        compelete_video_frame_queue_.push(VideoFrame(
            rtp_packet_h264.Payload(), rtp_packet_h264.PayloadSize()));
      } else if (rtp::NAL_UNIT_TYPE::FU_A == nalu_type) {
        incomplete_h264_frame_list_[rtp_packet_h264.SequenceNumber()] =
            rtp_packet_h264;
        bool complete = CheckIsH264FrameCompleted(rtp_packet_h264);
        if (!complete) {
        }
      }
    }
  }
  //  else {
  //   if (rtp::PAYLOAD_TYPE::H264 == rtp_packet.PayloadType()) {
  //     if (rtp::NAL_UNIT_TYPE::NALU == rtp_packet.NalUnitType()) {
  //       compelete_video_frame_queue_.push(
  //           VideoFrame(rtp_packet.Payload(), rtp_packet.PayloadSize()));
  //     } else if (rtp::NAL_UNIT_TYPE::FU_A == rtp_packet.NalUnitType()) {
  //       incomplete_h264_frame_list_[rtp_packet.SequenceNumber()] =
  //       rtp_packet; bool complete = CheckIsH264FrameCompleted(rtp_packet); if
  //       (!complete) {
  //       }
  //     }
  //   } else if (rtp::PAYLOAD_TYPE::H264_FEC_SOURCE ==
  //   rtp_packet.PayloadType()) {
  //     if (last_packet_ts_ != rtp_packet.Timestamp()) {
  //       fec_decoder_.Init();
  //       fec_decoder_.ResetParams(rtp_packet.FecSourceSymbolNum());
  //       last_packet_ts_ = rtp_packet.Timestamp();
  //     }

  //     incomplete_fec_packet_list_[rtp_packet.Timestamp()]
  //                                [rtp_packet.SequenceNumber()] = rtp_packet;

  //     uint8_t** complete_frame = fec_decoder_.DecodeWithNewSymbol(
  //         (const char*)incomplete_fec_packet_list_[rtp_packet.Timestamp()]
  //                                                 [rtp_packet.SequenceNumber()]
  //                                                     .Payload(),
  //         rtp_packet.FecSymbolId());

  //     if (nullptr != complete_frame) {
  //       if (!nv12_data_) {
  //         nv12_data_ = new uint8_t[NV12_BUFFER_SIZE];
  //       }

  //       size_t complete_frame_size = 0;
  //       for (int index = 0; index < rtp_packet.FecSourceSymbolNum(); index++)
  //       {
  //         if (nullptr == complete_frame[index]) {
  //           LOG_ERROR("Invalid complete_frame[{}]", index);
  //         }
  //         memcpy(nv12_data_ + complete_frame_size, complete_frame[index],
  //         1400); complete_frame_size += 1400;
  //       }

  //       fec_decoder_.ReleaseSourcePackets(complete_frame);
  //       fec_decoder_.Release();
  //       LOG_ERROR("Release incomplete_fec_packet_list_");
  //       incomplete_fec_packet_list_.erase(rtp_packet.Timestamp());

  //       if (incomplete_fec_frame_list_.end() !=
  //           incomplete_fec_frame_list_.find(rtp_packet.Timestamp())) {
  //         incomplete_fec_frame_list_.erase(rtp_packet.Timestamp());
  //       }

  //       compelete_video_frame_queue_.push(
  //           VideoFrame(nv12_data_, complete_frame_size));
  //     } else {
  //       incomplete_fec_frame_list_.insert(rtp_packet.Timestamp());
  //     }
  //   } else if (rtp::PAYLOAD_TYPE::H264_FEC_REPAIR ==
  //   rtp_packet.PayloadType()) {
  //     if (incomplete_fec_frame_list_.end() ==
  //         incomplete_fec_frame_list_.find(rtp_packet.Timestamp())) {
  //       return;
  //     }

  //     if (last_packet_ts_ != rtp_packet.Timestamp()) {
  //       fec_decoder_.Init();
  //       fec_decoder_.ResetParams(rtp_packet.FecSourceSymbolNum());
  //       last_packet_ts_ = rtp_packet.Timestamp();
  //     }

  //     incomplete_fec_packet_list_[rtp_packet.Timestamp()]
  //                                [rtp_packet.SequenceNumber()] = rtp_packet;

  //     uint8_t** complete_frame = fec_decoder_.DecodeWithNewSymbol(
  //         (const char*)incomplete_fec_packet_list_[rtp_packet.Timestamp()]
  //                                                 [rtp_packet.SequenceNumber()]
  //                                                     .Payload(),
  //         rtp_packet.FecSymbolId());

  //     if (nullptr != complete_frame) {
  //       if (!nv12_data_) {
  //         nv12_data_ = new uint8_t[NV12_BUFFER_SIZE];
  //       }

  //       size_t complete_frame_size = 0;
  //       for (int index = 0; index < rtp_packet.FecSourceSymbolNum(); index++)
  //       {
  //         if (nullptr == complete_frame[index]) {
  //           LOG_ERROR("Invalid complete_frame[{}]", index);
  //         }
  //         memcpy(nv12_data_ + complete_frame_size, complete_frame[index],
  //         1400); complete_frame_size += 1400;
  //       }

  //       fec_decoder_.ReleaseSourcePackets(complete_frame);
  //       fec_decoder_.Release();
  //       incomplete_fec_packet_list_.erase(rtp_packet.Timestamp());

  //       compelete_video_frame_queue_.push(
  //           VideoFrame(nv12_data_, complete_frame_size));
  //     }
  //   }
  // }
}

void RtpVideoReceiver::ProcessAv1RtpPacket(RtpPacketAv1& rtp_packet_av1) {
  // LOG_ERROR("recv payload size = {}, sequence_number_ = {}",
  //           rtp_packet.PayloadSize(), rtp_packet.SequenceNumber());

  if (rtp::PAYLOAD_TYPE::AV1 == rtp_packet_av1.PayloadType()) {
    incomplete_av1_frame_list_[rtp_packet_av1.SequenceNumber()] =
        rtp_packet_av1;
    bool complete = CheckIsAv1FrameCompleted(rtp_packet_av1);
    if (!complete) {
    }
  }

  // std::vector<Obu> obus =
  //     ParseObus((uint8_t*)rtp_packet.Payload(), rtp_packet.PayloadSize());
  // for (int i = 0; i < obus.size(); i++) {
  //   LOG_ERROR("2 [{}|{}] Obu size = [{}], Obu type [{}]", i, obus.size(),
  //             obus[i].size_,
  //             ObuTypeToString((OBU_TYPE)ObuType(obus[i].header_)));
  // }
}

bool RtpVideoReceiver::CheckIsH264FrameCompleted(
    RtpPacketH264& rtp_packet_h264) {
  if (rtp_packet_h264.FuAEnd()) {
    uint16_t end_seq = rtp_packet_h264.SequenceNumber();
    while (end_seq--) {
      auto it = incomplete_h264_frame_list_.find(end_seq);
      if (it == incomplete_h264_frame_list_.end()) {
        // The last fragment has already received. If all fragments are in
        // order, then some fragments lost in tranmission and need to be
        // repaired using FEC
        return false;
      } else if (!it->second.FuAStart()) {
        continue;
      } else if (it->second.FuAStart()) {
        if (!nv12_data_) {
          nv12_data_ = new uint8_t[NV12_BUFFER_SIZE];
        }

        size_t complete_frame_size = 0;
        int frame_fragment_count = 0;
        for (uint16_t start = it->first;
             start <= rtp_packet_h264.SequenceNumber(); start++) {
          memcpy(nv12_data_ + complete_frame_size,
                 incomplete_h264_frame_list_[start].Payload(),
                 incomplete_h264_frame_list_[start].PayloadSize());

          complete_frame_size +=
              incomplete_h264_frame_list_[start].PayloadSize();
          incomplete_h264_frame_list_.erase(start);
          frame_fragment_count++;
        }
        compelete_video_frame_queue_.push(
            VideoFrame(nv12_data_, complete_frame_size));

        return true;
      } else {
        LOG_WARN("What happened?")
        return false;
      }
    }

    return true;
  }
  return false;
}

bool RtpVideoReceiver::CheckIsAv1FrameCompleted(RtpPacketAv1& rtp_packet_av1) {
  if (rtp_packet_av1.Av1FrameEnd()) {
    uint16_t end_seq = rtp_packet_av1.SequenceNumber();
    uint16_t start = end_seq;
    while (end_seq--) {
      auto it = incomplete_av1_frame_list_.find(end_seq);
      if (it == incomplete_av1_frame_list_.end()) {
        // The last fragment has already received. If all fragments are in
        // order, then some fragments lost in tranmission and need to be
        // repaired using FEC
        // return false;
      } else if (!it->second.Av1FrameStart()) {
        continue;
      } else if (it->second.Av1FrameStart()) {
        start = it->second.SequenceNumber();
        break;
      } else {
        LOG_WARN("What happened?")
        return false;
      }
    }

    if (start <= rtp_packet_av1.SequenceNumber()) {
      if (!nv12_data_) {
        nv12_data_ = new uint8_t[NV12_BUFFER_SIZE];
      }

      size_t complete_frame_size = 0;
      for (; start <= rtp_packet_av1.SequenceNumber(); start++) {
        const uint8_t* obu_frame = incomplete_av1_frame_list_[start].Payload();
        size_t obu_frame_size = incomplete_av1_frame_list_[start].PayloadSize();
        memcpy(nv12_data_ + complete_frame_size, obu_frame, obu_frame_size);

        complete_frame_size += obu_frame_size;
        incomplete_av1_frame_list_.erase(start);
      }

      compelete_video_frame_queue_.push(
          VideoFrame(nv12_data_, complete_frame_size));

      return true;
    }
  }
  return false;
}

void RtpVideoReceiver::SetSendDataFunc(
    std::function<int(const char*, size_t)> data_send_func) {
  data_send_func_ = data_send_func;
}

int RtpVideoReceiver::SendRtcpRR(ReceiverReport& rtcp_rr) {
  if (!data_send_func_) {
    LOG_ERROR("data_send_func_ is nullptr");
    return -1;
  }

  if (data_send_func_((const char*)rtcp_rr.Buffer(), rtcp_rr.Size())) {
    LOG_ERROR("Send RR failed");
    return -1;
  }

  return 0;
}

TimeDelta AtoToTimeDelta(uint16_t receive_info) {
  // receive_info
  // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  // |R|ECN|  Arrival time offset    |
  // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  const uint16_t ato = receive_info & 0x1FFF;
  if (ato == 0x1FFE) {
    return TimeDelta::PlusInfinity();
  }
  if (ato == 0x1FFF) {
    return TimeDelta::MinusInfinity();
  }
  return TimeDelta::Seconds(ato) / 1024;
}

void RtpVideoReceiver::SendCombinedRtcpPacket(
    std::vector<std::unique_ptr<RtcpPacket>> rtcp_packets) {
  if (!data_send_func_) {
    LOG_ERROR("data_send_func_ is nullptr");
  }

  // LOG_ERROR("Send combined rtcp packet");

  for (auto& rtcp_packet : rtcp_packets) {
    rtcp_packet->SetSenderSsrc(feedback_ssrc_);
    rtcp_sender_->AppendPacket(*rtcp_packet);
    rtcp_sender_->Send();
  }
}

void RtpVideoReceiver::SendRemb(int64_t bitrate_bps,
                                std::vector<uint32_t> ssrcs) {
  if (!active_remb_module_) {
    return;
  }

  // The Add* and Remove* methods above ensure that REMB is disabled on all
  // other modules, because otherwise, they will send REMB with stale info.
  active_remb_module_->SetRemb(bitrate_bps, std::move(ssrcs));
}

bool RtpVideoReceiver::CheckIsTimeSendRR() {
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

bool RtpVideoReceiver::Process() {
  if (!compelete_video_frame_queue_.isEmpty()) {
    VideoFrame video_frame;
    compelete_video_frame_queue_.pop(video_frame);
    if (on_receive_complete_frame_) {
      // auto now_complete_frame_ts =
      //     std::chrono::duration_cast<std::chrono::milliseconds>(
      //         std::chrono::system_clock::now().time_since_epoch())
      //         .count();
      // uint32_t duration = now_complete_frame_ts - last_complete_frame_ts_;
      // LOG_ERROR("Duration {}", duration);
      // last_complete_frame_ts_ = now_complete_frame_ts;

      on_receive_complete_frame_(video_frame);
      // #ifdef SAVE_RTP_RECV_STREAM
      //       fwrite((unsigned char*)video_frame.Buffer(), 1,
      //       video_frame.Size(),
      //              file_rtp_recv_);
      // #endif
    }
  }

  return true;
}

void RtpVideoReceiver::RtcpThread() {
  while (!rtcp_stop_.load()) {
    std::unique_lock<std::mutex> lock(rtcp_mtx_);
    if (rtcp_cv_.wait_for(
            lock, std::chrono::milliseconds(rtcp_tcc_interval_ms_),
            [&]() { return send_rtcp_rr_triggered_ || rtcp_stop_; })) {
      if (rtcp_stop_) break;
      send_rtcp_rr_triggered_ = false;
    } else {
      // LOG_ERROR("Send video tcc");
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - last_send_rtcp_rr_ts_)
                         .count();
      if (elapsed >= rtcp_rr_interval_ms_) {
        LOG_ERROR("Send video rr [{}]", (void*)this);
        last_send_rtcp_rr_ts_ = now;
      }
    }
  }
}

/******************************************************************************/

void RtpVideoReceiver::SendNack(const std::vector<uint16_t>& nack_list,
                                bool buffering_allowed) {
  if (!nack_list.empty()) {
    webrtc::rtcp::Nack nack;
    nack.SetSenderSsrc(feedback_ssrc_);
    nack.SetMediaSsrc(remote_ssrc_);
    nack.SetPacketIds(std::move(nack_list));

    rtcp_sender_->AppendPacket(nack);
    rtcp_sender_->Send();
  }
}

void RtpVideoReceiver::RequestKeyFrame() {}

void RtpVideoReceiver::SendLossNotification(uint16_t last_decoded_seq_num,
                                            uint16_t last_received_seq_num,
                                            bool decodability_flag,
                                            bool buffering_allowed) {}

inline uint32_t DivideRoundToNearest(int64_t dividend, int64_t divisor) {
  if (dividend < 0) {
    int64_t half_of_divisor = divisor / 2;
    int64_t quotient = dividend / divisor;
    int64_t remainder = dividend % divisor;
    if (-remainder > half_of_divisor) {
      --quotient;
    }
    return quotient;
  }

  int64_t half_of_divisor = (divisor - 1) / 2;
  int64_t quotient = dividend / divisor;
  int64_t remainder = dividend % divisor;
  if (remainder > half_of_divisor) {
    ++quotient;
  }
  return quotient;
}

void RtpVideoReceiver::OnSenderReport(const SenderReport& sender_report) {
  remote_ssrc = sender_report.SenderSsrc();
  last_remote_ntp_timestamp = sender_report.NtpTimestamp();
  last_remote_rtp_timestamp = sender_report.Timestamp();
  last_arrival_timestamp = clock_->CurrentTime().ms();
  last_arrival_ntp_timestamp = webrtc::CompactNtp(clock_->CurrentNtpTime());
  packets_sent = sender_report.SenderPacketCount();
  bytes_sent = sender_report.SenderOctetCount();
  reports_count++;

  LOG_WARN(
      "OnSenderReport remote_ssrc[{}], last_remote_ntp_timestamp[{}], "
      "last_remote_rtp_timestamp[{}], last_arrival_timestamp[{}], "
      "last_arrival_ntp_timestamp[{}], packets_sent[{}], bytes_sent[{}], "
      "reports_count[{}]",
      remote_ssrc, last_remote_ntp_timestamp, last_remote_rtp_timestamp,
      last_arrival_timestamp, last_arrival_ntp_timestamp, packets_sent,
      bytes_sent, reports_count);

  // last_sr_ = ((uint32_t)(ntp_time / 0x100000000) << 16) |
  //            ((uint32_t)(ntp_time % 0x100000000) >> 16);
  // last_delay_ = DivideRoundToNearest(
  //     (clock_->CurrentTime().us() - now_time) * 0x10000, 1000000);

  // LOG_WARN("OnSenderReport [{}][{}] {}", last_sr_, last_delay_, now_time);
}