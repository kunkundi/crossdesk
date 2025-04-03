#include "rtp_video_receiver.h"

#include "api/ntp/ntp_time_util.h"
#include "common.h"
#include "fir.h"
#include "log.h"
#include "nack.h"
#include "rtcp_sender.h"

// #define SAVE_RTP_RECV_STREAM

#define NV12_BUFFER_SIZE (1280 * 720 * 3 / 2)
#define RTCP_RR_INTERVAL 1000
#define MAX_WAIT_TIME_MS 200     // 20ms
#define NACK_UPDATE_INTERVAL 20  // 20ms

RtpVideoReceiver::RtpVideoReceiver(std::shared_ptr<SystemClock> clock)
    : ssrc_(GenerateUniqueSsrc()),
      active_remb_module_(nullptr),
      is_running_(true),
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
      delta_ntp_internal_ms_(clock->CurrentNtpInMilliseconds() -
                             clock->CurrentTimeMs()),
      clock_(webrtc::Clock::GetWebrtcClockShared(clock)) {
  SetPeriod(std::chrono::milliseconds(5));
  SetThreadName("RtpVideoReceiver");
  rtcp_thread_ = std::thread(&RtpVideoReceiver::RtcpThread, this);
}

RtpVideoReceiver::RtpVideoReceiver(std::shared_ptr<SystemClock> clock,
                                   std::shared_ptr<IOStatistics> io_statistics)
    : io_statistics_(io_statistics),
      ssrc_(GenerateUniqueSsrc()),
      is_running_(true),
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
  SetThreadName("RtpVideoReceiver");
  rtcp_thread_ = std::thread(&RtpVideoReceiver::RtcpThread, this);

#ifdef SAVE_RTP_RECV_STREAM
  file_rtp_recv_ = fopen("rtp_recv_stream.h264", "w+b");
  if (!file_rtp_recv_) {
    LOG_WARN("Fail to open rtp_recv_stream.h264");
  }
#endif
}

RtpVideoReceiver::~RtpVideoReceiver() {
  StopRtcp();

  SSRCManager::Instance().DeleteSsrc(ssrc_);

  delete[] nv12_data_;

#ifdef SAVE_RTP_RECV_STREAM
  if (file_rtp_recv_) {
    fflush(file_rtp_recv_);
    fclose(file_rtp_recv_);
    file_rtp_recv_ = nullptr;
  }
#endif
}

void RtpVideoReceiver::InsertRtpPacket(RtpPacket& rtp_packet) {
  webrtc::RtpPacketReceived rtp_packet_received;
  rtp_packet_received.Build(rtp_packet.Buffer().data(), rtp_packet.Size());
  rtp_packet_received.set_arrival_time(clock_->CurrentTime());
  rtp_packet_received.set_ecn(EcnMarking::kEct0);
  rtp_packet_received.set_recovered(false);
  rtp_packet_received.set_payload_type_frequency(kVideoPayloadTypeFrequency);

  webrtc::Timestamp now = clock_->CurrentTime();
  remote_ssrc_ = rtp_packet.Ssrc();
  uint16_t sequence_number = rtp_packet.SequenceNumber();
  --cumulative_loss_;
  if (!last_receive_time_.has_value()) {
    last_extended_high_seq_num_ = sequence_number - 1;
    extended_high_seq_num_ = sequence_number - 1;
  }

  cumulative_loss_ += sequence_number - extended_high_seq_num_;
  extended_high_seq_num_ = sequence_number;

  if (rtp_packet_received.Timestamp() != last_received_timestamp_) {
    webrtc::TimeDelta receive_diff = now - *last_receive_time_;
    uint32_t receive_diff_rtp =
        (receive_diff * rtp_packet_received.payload_type_frequency())
            .seconds<uint32_t>();
    int32_t time_diff_samples =
        receive_diff_rtp -
        (rtp_packet_received.Timestamp() - last_received_timestamp_);

    ReviseFrequencyAndJitter(rtp_packet_received.payload_type_frequency());

    // lib_jingle sometimes deliver crazy jumps in TS for the same stream.
    // If this happens, don't update jitter value. Use 5 secs video frequency
    // as the threshold.
    if (time_diff_samples < 5 * kVideoPayloadTypeFrequency &&
        time_diff_samples > -5 * kVideoPayloadTypeFrequency) {
      // Note we calculate in Q4 to avoid using float.
      int32_t jitter_diff_q4 = (std::abs(time_diff_samples) << 4) - jitter_q4_;
      jitter_q4_ += ((jitter_diff_q4 + 8) >> 4);
    }

    jitter_ = jitter_q4_ >> 4;
  }

  last_received_timestamp_ = rtp_packet_received.Timestamp();
  last_receive_time_ = now;

#ifdef SAVE_RTP_RECV_STREAM
  fwrite((unsigned char*)rtp_packet.Payload(), 1, rtp_packet.PayloadSize(),
         file_rtp_recv_);
#endif

  last_recv_bytes_ = (uint32_t)rtp_packet.PayloadSize();
  total_rtp_payload_recv_ += (uint32_t)rtp_packet.PayloadSize();
  total_rtp_packets_recv_++;

  if (io_statistics_) {
    io_statistics_->UpdateVideoInboundBytes(last_recv_bytes_);
    io_statistics_->IncrementVideoInboundRtpPacketCount();
    io_statistics_->UpdateVideoPacketLossCount(rtp_packet.SequenceNumber());
  }

  uint32_t now_ts = static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  CheckIsTimeUpdateNack(now_ts);

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

  if (rtp_packet.PayloadType() == rtp::PAYLOAD_TYPE::AV1 ||
      rtp_packet.PayloadType() == rtp::PAYLOAD_TYPE::AV1 - 1) {
    RtpPacketAv1 rtp_packet_av1;
    rtp_packet_av1.Build(rtp_packet.Buffer().data(), rtp_packet.Size());
    rtp_packet_av1.GetFrameHeaderInfo();
    ProcessAv1RtpPacket(rtp_packet_av1);
  } else if (rtp_packet.PayloadType() == rtp::PAYLOAD_TYPE::H264 ||
             rtp_packet.PayloadType() == rtp::PAYLOAD_TYPE::H264 - 1 ||
             rtp_packet.PayloadType() == rtp::PAYLOAD_TYPE::RTX) {
    RtpPacketH264 rtp_packet_h264;
    if (rtp_packet_h264.Build(rtp_packet.Buffer().data(), rtp_packet.Size())) {
      rtp_packet_h264.GetFrameHeaderInfo();
      if (rtp_packet.PayloadType() != rtp::PAYLOAD_TYPE::RTX) {
        receive_side_congestion_controller_.OnReceivedPacket(
            rtp_packet_received, MediaType::VIDEO);
        nack_->OnReceivedPacket(rtp_packet.SequenceNumber(), false);
      } else {
        nack_->OnReceivedPacket(rtp_packet_h264.GetOsn(), true);
      }
    }
    ProcessH264RtpPacket(rtp_packet_h264);

    // std::unique_ptr<RtpPacketH264> rtp_packet_h264 =
    //     std::make_unique<RtpPacketH264>();
    // if (rtp_packet.Buffer().data() != nullptr && rtp_packet.Size() > 0 &&
    //     rtp_packet_h264->Build(rtp_packet.Buffer().data(),
    //     rtp_packet.Size())) {
    //   rtp_packet_h264->GetFrameHeaderInfo();

    //   if (rtp_packet.PayloadType() == rtp::PAYLOAD_TYPE::RTX) {
    //     size_t osn_offset = rtp_packet.HeaderSize();
    //     uint16_t osn = rtp_packet.Buffer().data()[osn_offset] << 8 |
    //                    rtp_packet.Buffer().data()[osn_offset + 1];
    //     nack_->OnReceivedPacket(osn, true);
    //   } else {
    //     receive_side_congestion_controller_.OnReceivedPacket(
    //         rtp_packet_received, MediaType::VIDEO);
    //     nack_->OnReceivedPacket(rtp_packet.SequenceNumber(), false);
    //   }

    //   rtp::NAL_UNIT_TYPE nalu_type = rtp_packet_h264->NalUnitType();
    //   if (rtp::NAL_UNIT_TYPE::NALU == nalu_type) {
    //     ReceivedFrame received_frame(rtp_packet_h264->Payload(),
    //                                  rtp_packet_h264->PayloadSize());
    //     received_frame.SetReceivedTimestamp(clock_->CurrentTime().us());
    //     received_frame.SetCapturedTimestamp(
    //         (static_cast<int64_t>(rtp_packet_h264->Timestamp()) /
    //              rtp::kMsToRtpTimestamp -
    //          delta_ntp_internal_ms_) *
    //         1000);
    //     compelete_video_frame_queue_.push(received_frame);
    //   } else if (rtp::NAL_UNIT_TYPE::FU_A == nalu_type) {
    //     std::vector<std::unique_ptr<RtpPacketH264>> complete_frame =
    //         h264_frame_assembler_.InsertPacket(std::move(rtp_packet_h264));
    //     if (!complete_frame.empty()) {
    //       uint8_t* nv12_data_ = new uint8_t[NV12_BUFFER_SIZE];
    //       uint8_t* dest = nv12_data_;
    //       size_t complete_frame_size = 0;
    //       for (auto& frame : complete_frame) {
    //         memcpy(dest, frame->Payload(), frame->PayloadSize());
    //         dest += frame->PayloadSize();
    //         complete_frame_size += frame->PayloadSize();
    //       }

    //       ReceivedFrame received_frame(nv12_data_, complete_frame_size);
    //       received_frame.SetReceivedTimestamp(clock_->CurrentTime().us());
    //       received_frame.SetCapturedTimestamp(
    //           (static_cast<int64_t>(complete_frame[0]->Timestamp()) /
    //                rtp::kMsToRtpTimestamp -
    //            delta_ntp_internal_ms_) *
    //           1000);
    //       compelete_video_frame_queue_.push(received_frame);

    //       delete[] nv12_data_;
    //     }
    //   }
    // }
  }
}

void RtpVideoReceiver::ProcessH264RtpPacket(RtpPacketH264& rtp_packet_h264) {
  if (!fec_enable_) {
    rtp::NAL_UNIT_TYPE nalu_type = rtp_packet_h264.NalUnitType();
    if (rtp::NAL_UNIT_TYPE::NALU == nalu_type) {
      // ReceivedFrame received_frame(rtp_packet_h264.Payload(),
      //                              rtp_packet_h264.PayloadSize());
      // received_frame.SetReceivedTimestamp(clock_->CurrentTime().us());
      // received_frame.SetCapturedTimestamp(
      //     (static_cast<int64_t>(rtp_packet_h264.Timestamp()) /
      //          rtp::kMsToRtpTimestamp -
      //      delta_ntp_internal_ms_) *
      //     1000);
      // compelete_video_frame_queue_.push(received_frame);

      std::unique_ptr<ReceivedFrame> received_frame =
          std::make_unique<ReceivedFrame>(rtp_packet_h264.Payload(),
                                          rtp_packet_h264.PayloadSize());
      received_frame->SetReceivedTimestamp(clock_->CurrentTime().us());
      received_frame->SetCapturedTimestamp(
          (static_cast<int64_t>(rtp_packet_h264.Timestamp()) /
               rtp::kMsToRtpTimestamp -
           delta_ntp_internal_ms_) *
          1000);

      std::lock_guard<std::mutex> lock(pending_frames_mtx_);
      pending_frames_[rtp_packet_h264.Timestamp()] = {
          std::move(received_frame), true, clock_->CurrentTime().ms()};
    } else if (rtp::NAL_UNIT_TYPE::FU_A == nalu_type) {
      if (rtp::PAYLOAD_TYPE::H264 == rtp_packet_h264.PayloadType()) {
        incomplete_h264_frame_list_[rtp_packet_h264.SequenceNumber()] =
            rtp_packet_h264;
        CheckIsH264FrameCompleted(rtp_packet_h264, rtp_packet_h264.FuAStart(),
                                  rtp_packet_h264.FuAEnd(), false);
      } else if (rtp::PAYLOAD_TYPE::RTX == rtp_packet_h264.PayloadType()) {
        incomplete_h264_frame_list_[rtp_packet_h264.GetOsn()] = rtp_packet_h264;
        CheckIsH264FrameCompleted(rtp_packet_h264, rtp_packet_h264.FuAStart(),
                                  rtp_packet_h264.FuAEnd(), true);
      } else if (rtp::PAYLOAD_TYPE::H264 - 1 == rtp_packet_h264.PayloadType()) {
        padding_sequence_numbers_.insert(rtp_packet_h264.SequenceNumber());
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
  //       rtp_packet; bool complete = CheckIsH264FrameCompleted(rtp_packet);
  //       if
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
  //                                [rtp_packet.SequenceNumber()] =
  //                                rtp_packet;

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
  //       for (int index = 0; index < rtp_packet.FecSourceSymbolNum();
  //       index++)
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
  //                                [rtp_packet.SequenceNumber()] =
  //                                rtp_packet;

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
  //       for (int index = 0; index < rtp_packet.FecSourceSymbolNum();
  //       index++)
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

bool RtpVideoReceiver::CheckIsH264FrameCompleted(RtpPacketH264& rtp_packet_h264,
                                                 bool is_start, bool is_end,
                                                 bool is_rtx) {
  uint32_t timestamp = rtp_packet_h264.Timestamp();
  uint16_t seq, start_seq, end_seq;

  if (pending_frames_.find(timestamp) == pending_frames_.end()) {
    std::lock_guard<std::mutex> lock(pending_frames_mtx_);
    pending_frames_[timestamp] = {nullptr, false, clock_->CurrentTime().ms()};
  }

  if (is_rtx) {
    seq = rtp_packet_h264.GetOsn();
  } else {
    seq = rtp_packet_h264.SequenceNumber();
  }

  if (is_start) {
    fua_start_sequence_numbers_[timestamp] = seq;
  }

  if (is_end) {
    fua_end_sequence_numbers_[timestamp] = seq;
    if (missing_sequence_numbers_wait_time_.find(timestamp) ==
        missing_sequence_numbers_wait_time_.end()) {
      missing_sequence_numbers_wait_time_[timestamp] =
          clock_->CurrentTime().ms();
    }
  }

  if (fua_end_sequence_numbers_.find(timestamp) ==
      fua_end_sequence_numbers_.end()) {
    return false;
  }
  end_seq = fua_end_sequence_numbers_[timestamp];

  if (fua_start_sequence_numbers_.find(timestamp) ==
      fua_start_sequence_numbers_.end()) {
    return false;
  }
  start_seq = fua_start_sequence_numbers_[timestamp];

  if (is_rtx && fua_end_sequence_numbers_.find(timestamp) !=
                    fua_end_sequence_numbers_.end()) {
    auto missing_seqs_wait_ts_iter =
        missing_sequence_numbers_wait_time_.find(timestamp);
    if (missing_seqs_wait_ts_iter !=
        missing_sequence_numbers_wait_time_.end()) {
      if (clock_->CurrentTime().ms() - missing_seqs_wait_ts_iter->second >
          MAX_WAIT_TIME_MS) {
        missing_sequence_numbers_wait_time_.erase(missing_seqs_wait_ts_iter);
        std::lock_guard<std::mutex> lock(pending_frames_mtx_);
        LOG_WARN(
            "retransmit packet [seq {} | ts {}] timeout, remove pending frame",
            seq, timestamp);
        pending_frames_.erase(timestamp);
        return false;
      }
    }
  }

  for (uint16_t sequence_number = start_seq; sequence_number <= end_seq;
       ++sequence_number) {
    if (incomplete_h264_frame_list_.find(sequence_number) ==
        incomplete_h264_frame_list_.end()) {
      return false;
    }
  }

  return PopCompleteFrame(start_seq, end_seq, timestamp);
}

bool RtpVideoReceiver::PopCompleteFrame(uint16_t start_seq, uint16_t end_seq,
                                        uint32_t timestamp) {
  size_t complete_frame_size = 0;
  int frame_fragment_count = 0;

  for (uint16_t seq = start_seq; seq <= end_seq; ++seq) {
    if (padding_sequence_numbers_.find(seq) !=
        padding_sequence_numbers_.end()) {
      padding_sequence_numbers_.erase(seq);
      continue;
    }
    if (incomplete_h264_frame_list_.find(seq) !=
        incomplete_h264_frame_list_.end()) {
      complete_frame_size += incomplete_h264_frame_list_[seq].PayloadSize();
    }
  }

  if (!nv12_data_) {
    nv12_data_ = new uint8_t[NV12_BUFFER_SIZE];
  } else if (complete_frame_size > NV12_BUFFER_SIZE) {
    delete[] nv12_data_;
    nv12_data_ = new uint8_t[complete_frame_size];
  }

  uint8_t* dest = nv12_data_;
  for (uint16_t seq = start_seq; seq <= end_seq; ++seq) {
    if (incomplete_h264_frame_list_.find(seq) !=
        incomplete_h264_frame_list_.end()) {
      size_t payload_size = incomplete_h264_frame_list_[seq].PayloadSize();
      memcpy(dest, incomplete_h264_frame_list_[seq].Payload(), payload_size);
      dest += payload_size;
      incomplete_h264_frame_list_.erase(seq);
      frame_fragment_count++;
    }
  }

  std::unique_ptr<ReceivedFrame> received_frame =
      std::make_unique<ReceivedFrame>(nv12_data_, complete_frame_size);
  received_frame->SetReceivedTimestamp(clock_->CurrentTime().us());
  received_frame->SetCapturedTimestamp(
      (static_cast<int64_t>(timestamp) / rtp::kMsToRtpTimestamp -
       delta_ntp_internal_ms_) *
      1000);

  fua_start_sequence_numbers_.erase(timestamp);
  fua_end_sequence_numbers_.erase(timestamp);
  missing_sequence_numbers_wait_time_.erase(timestamp);
  // compelete_video_frame_queue_.push(received_frame);

  std::lock_guard<std::mutex> lock(pending_frames_mtx_);
  pending_frames_[timestamp] = {std::move(received_frame), true,
                                clock_->CurrentTime().ms()};
  return true;
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

      ReceivedFrame received_frame(nv12_data_, complete_frame_size);
      received_frame.SetReceivedTimestamp(clock_->CurrentTime().us());
      received_frame.SetCapturedTimestamp(
          (static_cast<int64_t>(rtp_packet_av1.Timestamp()) /
               rtp::kMsToRtpTimestamp -
           delta_ntp_internal_ms_) *
          1000);
      compelete_video_frame_queue_.push(received_frame);

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
    rtcp_packet->SetSenderSsrc(ssrc_);
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

bool RtpVideoReceiver::CheckIsTimeSendRR(uint32_t now) {
  if (now - last_send_rtcp_rr_packet_ts_ >= RTCP_RR_INTERVAL) {
    last_send_rtcp_rr_packet_ts_ = now;
    return true;
  } else {
    return false;
  }
}

void RtpVideoReceiver::CheckIsTimeUpdateNack(uint32_t now) {
  if (now - last_nack_update_ts_ >= NACK_UPDATE_INTERVAL) {
    last_send_rtcp_rr_packet_ts_ = now;
    if (nack_) {
      nack_->ProcessNacks();
    }
  }
}

bool RtpVideoReceiver::Process() {
  if (!is_running_.load()) {
    return false;
  }

  // if (!compelete_video_frame_queue_.isEmpty()) {
  //   std::optional<ReceivedFrame> video_frame =
  //       compelete_video_frame_queue_.pop();
  //   if (on_receive_complete_frame_ && video_frame) {
  //     // auto now_complete_frame_ts =
  //     //     std::chrono::duration_cast<std::chrono::milliseconds>(
  //     //         std::chrono::system_clock::now().time_since_epoch())
  //     //         .count();
  //     // uint32_t duration = now_complete_frame_ts -
  //     // last_complete_frame_ts_; LOG_ERROR("Duration {}", duration);
  //     // last_complete_frame_ts_ = now_complete_frame_ts;

  //     on_receive_complete_frame_(*video_frame);
  //     // #ifdef SAVE_RTP_RECV_STREAM
  //     //       fwrite((unsigned char*)video_frame.Buffer(), 1,
  //     //       video_frame.Size(),
  //     //              file_rtp_recv_);
  //     // #endif
  //   }
  // }

  std::lock_guard<std::mutex> lock(pending_frames_mtx_);
  if (pending_frames_.empty()) {
    return false;
  }

  auto it = pending_frames_.begin();
  while (it != pending_frames_.end()) {
    if (it->second.is_complete) {
      if (on_receive_complete_frame_) {
        on_receive_complete_frame_(*(it->second.frame));
      }
      it = pending_frames_.erase(it);
    } else {
      if (clock_->CurrentTime().ms() - it->second.arrival_time >
          MAX_WAIT_TIME_MS) {
        LOG_WARN("pending frame [ts {}] timeout, remove it", it->first);
        // it = pending_frames_.erase(it);
        pending_frames_.clear();
        RequestKeyFrame();
        return false;
      } else {
        return false;
      }
    }
  }

  return true;
}

void RtpVideoReceiver::ReviseFrequencyAndJitter(int payload_type_frequency) {
  if (payload_type_frequency == last_payload_type_frequency_) {
    return;
  }

  if (payload_type_frequency != 0) {
    if (last_payload_type_frequency_ != 0) {
      // Value in "jitter_q4_" variable is a number of samples.
      // I.e. jitter = timestamp (s) * frequency (Hz).
      // Since the frequency has changed we have to update the number of
      // samples accordingly. The new value should rely on a new frequency.

      // If we don't do such procedure we end up with the number of samples
      // that cannot be converted into TimeDelta correctly (i.e. jitter =
      // jitter_q4_ >> 4 / payload_type_frequency). In such case, the number
      // of samples has a "mix".

      // Doing so we pretend that everything prior and including the current
      // packet were computed on packet's frequency.
      jitter_q4_ = static_cast<int>(static_cast<uint64_t>(jitter_q4_) *
                                    payload_type_frequency /
                                    last_payload_type_frequency_);
    }
    // If last_payload_type_frequency_ is not present, the jitter_q4_
    // variable has its initial value.

    // Keep last_payload_type_frequency_ up to date and non-zero (set).
    last_payload_type_frequency_ = payload_type_frequency;
  }
}

void RtpVideoReceiver::SendRR() {
  uint32_t now = CompactNtp(clock_->CurrentNtpTime());

  // Calculate fraction lost.
  int64_t exp_since_last = extended_high_seq_num_ - last_extended_high_seq_num_;
  int32_t lost_since_last = cumulative_loss_ - last_report_cumulative_loss_;
  if (exp_since_last > 0 && lost_since_last > 0) {
    // Scale 0 to 255, where 255 is 100% loss.
    fraction_lost_ = 255 * lost_since_last / exp_since_last;
  } else {
    fraction_lost_ = 0;
  }

  cumulative_lost_ = cumulative_loss_ + cumulative_loss_rtcp_offset_;
  if (cumulative_lost_ < 0) {
    // Clamp to zero. Work around to accommodate for senders that misbehave
    // with negative cumulative loss.
    cumulative_lost_ = 0;
    cumulative_loss_rtcp_offset_ = -cumulative_loss_;
  }
  if (cumulative_lost_ > 0x7fffff) {
    // Packets lost is a 24 bit signed field, and thus should be clamped, as
    // described in https://datatracker.ietf.org/doc/html/rfc3550#appendix-A.3
    cumulative_lost_ = 0x7fffff;
  }

  uint32_t receive_time = last_arrival_ntp_timestamp;
  uint32_t delay_since_last_sr = now - receive_time;

  ReceiverReport rtcp_rr;
  RtcpReportBlock report;

  report.SetMediaSsrc(remote_ssrc_);
  report.SetFractionLost(fraction_lost_);
  report.SetCumulativeLost(cumulative_lost_);
  report.SetExtHighestSeqNum(extended_high_seq_num_);
  report.SetJitter(jitter_);
  report.SetLastSr(last_remote_ntp_timestamp);
  report.SetDelayLastSr(delay_since_last_sr);
  rtcp_rr.SetSenderSsrc(ssrc_);
  rtcp_rr.SetReportBlock(report);
  rtcp_rr.Build();
  SendRtcpRR(rtcp_rr);

  last_extended_high_seq_num_ = extended_high_seq_num_;
  last_report_cumulative_loss_ = cumulative_loss_;
}

void RtpVideoReceiver::StopRtcp() {
  is_running_.store(false);
  if (rtcp_stop_.load()) {
    return;
  }

  rtcp_stop_.store(true);
  rtcp_cv_.notify_all();
  if (rtcp_thread_.joinable()) {
    rtcp_thread_.join();
  }
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
      if (elapsed >= rtcp_rr_interval_ms_ && last_receive_time_.has_value()) {
        SendRR();
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
    nack.SetSenderSsrc(ssrc_);
    nack.SetMediaSsrc(remote_ssrc_);
    nack.SetPacketIds(std::move(nack_list));

    rtcp_sender_->AppendPacket(nack);
    rtcp_sender_->Send();
  }
}

void RtpVideoReceiver::RequestKeyFrame() {
  ++sequence_number_fir_;
  webrtc::rtcp::Fir fir;
  fir.SetSenderSsrc(ssrc_);
  fir.AddRequestTo(remote_ssrc_, sequence_number_fir_);

  rtcp_sender_->AppendPacket(fir);
  rtcp_sender_->Send();
}

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
}