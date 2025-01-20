#include "rtp_video_receiver.h"

#include "common.h"
#include "log.h"
#include "rtcp_sender.h"

#define NV12_BUFFER_SIZE (1280 * 720 * 3 / 2)
#define RTCP_RR_INTERVAL 1000

RtpVideoReceiver::RtpVideoReceiver()
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
      clock_(Clock::GetRealTimeClock()) {}

RtpVideoReceiver::RtpVideoReceiver(std::shared_ptr<IOStatistics> io_statistics)
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
      clock_(Clock::GetRealTimeClock()) {
  rtcp_thread_ = std::thread(&RtpVideoReceiver::RtcpThread, this);
}

RtpVideoReceiver::~RtpVideoReceiver() {
  if (rtp_statistics_) {
    rtp_statistics_->Stop();
  }

  rtcp_stop_.store(true);
  rtcp_cv_.notify_one();

  if (rtcp_thread_.joinable()) {
    rtcp_thread_.join();
  }

  SSRCManager::Instance().DeleteSsrc(feedback_ssrc_);
}

void RtpVideoReceiver::InsertRtpPacket(RtpPacket& rtp_packet) {
  if (!rtp_statistics_) {
    rtp_statistics_ = std::make_unique<RtpStatistics>();
    rtp_statistics_->Start();
  }

  webrtc::RtpPacketReceived rtp_packet_received;
  rtp_packet_received.Build(rtp_packet.Buffer(), rtp_packet.Size());

  rtp_packet_received.set_arrival_time(clock_->CurrentTime());
  rtp_packet_received.set_ecn(EcnMarking::kEct0);
  rtp_packet_received.set_recovered(false);
  rtp_packet_received.set_payload_type_frequency(0);
  receive_side_congestion_controller_.OnReceivedPacket(rtp_packet_received,
                                                       MediaType::VIDEO);

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
  if (rtp_packet.PayloadType() == RtpPacket::PAYLOAD_TYPE::AV1) {
    ProcessAv1RtpPacket(rtp_packet);
  } else {
    ProcessH264RtpPacket(rtp_packet);
  }
}

void RtpVideoReceiver::ProcessH264RtpPacket(RtpPacket& rtp_packet) {
  if (!fec_enable_) {
    if (RtpPacket::PAYLOAD_TYPE::H264 == rtp_packet.PayloadType()) {
      if (RtpPacket::NAL_UNIT_TYPE::NALU == rtp_packet.NalUnitType()) {
        compelete_video_frame_queue_.push(
            VideoFrame(rtp_packet.Payload(), rtp_packet.PayloadSize()));
      } else if (RtpPacket::NAL_UNIT_TYPE::FU_A == rtp_packet.NalUnitType()) {
        incomplete_frame_list_[rtp_packet.SequenceNumber()] = rtp_packet;
        bool complete = CheckIsH264FrameCompleted(rtp_packet);
        if (!complete) {
        }
      }
    }
  } else {
    if (RtpPacket::PAYLOAD_TYPE::H264 == rtp_packet.PayloadType()) {
      if (RtpPacket::NAL_UNIT_TYPE::NALU == rtp_packet.NalUnitType()) {
        compelete_video_frame_queue_.push(
            VideoFrame(rtp_packet.Payload(), rtp_packet.PayloadSize()));
      } else if (RtpPacket::NAL_UNIT_TYPE::FU_A == rtp_packet.NalUnitType()) {
        incomplete_frame_list_[rtp_packet.SequenceNumber()] = rtp_packet;
        bool complete = CheckIsH264FrameCompleted(rtp_packet);
        if (!complete) {
        }
      }
    } else if (RtpPacket::PAYLOAD_TYPE::H264_FEC_SOURCE ==
               rtp_packet.PayloadType()) {
      if (last_packet_ts_ != rtp_packet.Timestamp()) {
        fec_decoder_.Init();
        fec_decoder_.ResetParams(rtp_packet.FecSourceSymbolNum());
        last_packet_ts_ = rtp_packet.Timestamp();
      }

      incomplete_fec_packet_list_[rtp_packet.Timestamp()]
                                 [rtp_packet.SequenceNumber()] = rtp_packet;

      uint8_t** complete_frame = fec_decoder_.DecodeWithNewSymbol(
          (const char*)incomplete_fec_packet_list_[rtp_packet.Timestamp()]
                                                  [rtp_packet.SequenceNumber()]
                                                      .Payload(),
          rtp_packet.FecSymbolId());

      if (nullptr != complete_frame) {
        if (!nv12_data_) {
          nv12_data_ = new uint8_t[NV12_BUFFER_SIZE];
        }

        size_t complete_frame_size = 0;
        for (int index = 0; index < rtp_packet.FecSourceSymbolNum(); index++) {
          if (nullptr == complete_frame[index]) {
            LOG_ERROR("Invalid complete_frame[{}]", index);
          }
          memcpy(nv12_data_ + complete_frame_size, complete_frame[index], 1400);
          complete_frame_size += 1400;
        }

        fec_decoder_.ReleaseSourcePackets(complete_frame);
        fec_decoder_.Release();
        LOG_ERROR("Release incomplete_fec_packet_list_");
        incomplete_fec_packet_list_.erase(rtp_packet.Timestamp());

        if (incomplete_fec_frame_list_.end() !=
            incomplete_fec_frame_list_.find(rtp_packet.Timestamp())) {
          incomplete_fec_frame_list_.erase(rtp_packet.Timestamp());
        }

        compelete_video_frame_queue_.push(
            VideoFrame(nv12_data_, complete_frame_size));
      } else {
        incomplete_fec_frame_list_.insert(rtp_packet.Timestamp());
      }
    } else if (RtpPacket::PAYLOAD_TYPE::H264_FEC_REPAIR ==
               rtp_packet.PayloadType()) {
      if (incomplete_fec_frame_list_.end() ==
          incomplete_fec_frame_list_.find(rtp_packet.Timestamp())) {
        return;
      }

      if (last_packet_ts_ != rtp_packet.Timestamp()) {
        fec_decoder_.Init();
        fec_decoder_.ResetParams(rtp_packet.FecSourceSymbolNum());
        last_packet_ts_ = rtp_packet.Timestamp();
      }

      incomplete_fec_packet_list_[rtp_packet.Timestamp()]
                                 [rtp_packet.SequenceNumber()] = rtp_packet;

      uint8_t** complete_frame = fec_decoder_.DecodeWithNewSymbol(
          (const char*)incomplete_fec_packet_list_[rtp_packet.Timestamp()]
                                                  [rtp_packet.SequenceNumber()]
                                                      .Payload(),
          rtp_packet.FecSymbolId());

      if (nullptr != complete_frame) {
        if (!nv12_data_) {
          nv12_data_ = new uint8_t[NV12_BUFFER_SIZE];
        }

        size_t complete_frame_size = 0;
        for (int index = 0; index < rtp_packet.FecSourceSymbolNum(); index++) {
          if (nullptr == complete_frame[index]) {
            LOG_ERROR("Invalid complete_frame[{}]", index);
          }
          memcpy(nv12_data_ + complete_frame_size, complete_frame[index], 1400);
          complete_frame_size += 1400;
        }

        fec_decoder_.ReleaseSourcePackets(complete_frame);
        fec_decoder_.Release();
        incomplete_fec_packet_list_.erase(rtp_packet.Timestamp());

        compelete_video_frame_queue_.push(
            VideoFrame(nv12_data_, complete_frame_size));
      }
    }
  }
}

void RtpVideoReceiver::ProcessAv1RtpPacket(RtpPacket& rtp_packet) {
  // LOG_ERROR("recv payload size = {}, sequence_number_ = {}",
  //           rtp_packet.PayloadSize(), rtp_packet.SequenceNumber());

  if (RtpPacket::PAYLOAD_TYPE::AV1 == rtp_packet.PayloadType()) {
    incomplete_frame_list_[rtp_packet.SequenceNumber()] = rtp_packet;
    bool complete = CheckIsAv1FrameCompleted(rtp_packet);
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

bool RtpVideoReceiver::CheckIsH264FrameCompleted(RtpPacket& rtp_packet) {
  if (rtp_packet.FuAEnd()) {
    uint16_t end_seq = rtp_packet.SequenceNumber();

    while (end_seq--) {
      auto it = incomplete_frame_list_.find(end_seq);
      if (it == incomplete_frame_list_.end()) {
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
        for (uint16_t start = it->first; start <= rtp_packet.SequenceNumber();
             start++) {
          memcpy(nv12_data_ + complete_frame_size,
                 incomplete_frame_list_[start].Payload(),
                 incomplete_frame_list_[start].PayloadSize());

          complete_frame_size += incomplete_frame_list_[start].PayloadSize();
          incomplete_frame_list_.erase(start);
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

bool RtpVideoReceiver::CheckIsAv1FrameCompleted(RtpPacket& rtp_packet) {
  if (rtp_packet.Av1FrameEnd()) {
    uint16_t end_seq = rtp_packet.SequenceNumber();
    uint16_t start = end_seq;
    while (end_seq--) {
      auto it = incomplete_frame_list_.find(end_seq);
      if (it == incomplete_frame_list_.end()) {
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

    if (start <= rtp_packet.SequenceNumber()) {
      if (!nv12_data_) {
        nv12_data_ = new uint8_t[NV12_BUFFER_SIZE];
      }

      size_t complete_frame_size = 0;
      for (; start <= rtp_packet.SequenceNumber(); start++) {
        const uint8_t* obu_frame = incomplete_frame_list_[start].Payload();
        size_t obu_frame_size = incomplete_frame_list_[start].PayloadSize();
        memcpy(nv12_data_ + complete_frame_size, obu_frame, obu_frame_size);

        complete_frame_size += obu_frame_size;
        incomplete_frame_list_.erase(start);
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

int RtpVideoReceiver::SendRtcpRR(RtcpReceiverReport& rtcp_rr) {
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

void RtpVideoReceiver::SendCombinedRtcpPacket(
    std::vector<std::unique_ptr<RtcpPacket>> rtcp_packets) {
  if (!data_send_func_) {
    LOG_ERROR("data_send_func_ is nullptr");
  }

  // LOG_ERROR("Send combined rtcp packet");

  RTCPSender rtcp_sender(
      [this](const uint8_t* buffer, size_t size) -> int {
        return data_send_func_((const char*)buffer, size);
      },
      IP_PACKET_SIZE);

  for (auto& rtcp_packet : rtcp_packets) {
    rtcp_packet->SetSenderSsrc(feedback_ssrc_);
    rtcp_sender.AppendPacket(*rtcp_packet);
    rtcp_sender.Send();
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
    }
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  return true;
}

void RtpVideoReceiver::RtcpThread() {
  while (!rtcp_stop_) {
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