#include "rtp_video_receiver.h"

#include "log.h"
#include "obu_parser.h"

#define NV12_BUFFER_SIZE (1280 * 720 * 3 / 2)
#define RTCP_RR_INTERVAL 1000

RtpVideoReceiver::RtpVideoReceiver() {}

RtpVideoReceiver::~RtpVideoReceiver() {
  if (rtp_statistics_) {
    rtp_statistics_->Stop();
  }
}

void RtpVideoReceiver::InsertRtpPacket(RtpPacket& rtp_packet) {
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

    // SendRtcpRR(rtcp_rr);
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
    if (incomplete_frame_list_.size() == end_seq) {
      return true;
    }

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
        for (size_t start = it->first; start <= rtp_packet.SequenceNumber();
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
    if (incomplete_frame_list_.size() == end_seq) {
      return true;
    }

    size_t start = rtp_packet.SequenceNumber();

    while (end_seq--) {
      auto it = incomplete_frame_list_.find(end_seq);
      if (it == incomplete_frame_list_.end()) {
        // The last fragment has already received. If all fragments are in
        // order, then some fragments lost in tranmission and need to be
        // repaired using FEC
        return false;
      } else if (!it->second.Av1FrameStart()) {
        continue;
      } else if (it->second.Av1FrameStart()) {
        start = it->second.SequenceNumber();
        // skip temporal delimiter OBU
        break;
      } else {
        LOG_WARN("What happened?")
        return false;
      }
    }

    if (start != rtp_packet.SequenceNumber()) {
      if (!nv12_data_) {
        nv12_data_ = new uint8_t[NV12_BUFFER_SIZE];
      }

      size_t complete_frame_size = 0;
      for (; start <= rtp_packet.SequenceNumber(); start++) {
        memcpy(nv12_data_ + complete_frame_size,
               incomplete_frame_list_[start].Payload(),
               incomplete_frame_list_[start].PayloadSize());

        complete_frame_size += incomplete_frame_list_[start].PayloadSize();
        incomplete_frame_list_.erase(start);
      }

      compelete_video_frame_queue_.push(
          VideoFrame(nv12_data_, complete_frame_size));

      return true;
    }
  }
  return false;
}

bool RtpVideoReceiver::Process() {
  if (!compelete_video_frame_queue_.isEmpty()) {
    VideoFrame video_frame;
    compelete_video_frame_queue_.pop(video_frame);
    if (on_receive_complete_frame_) {
      // auto now_complete_frame_ts =
      //     std::chrono::high_resolution_clock::now().time_since_epoch().count()
      //     / 1000000;
      // uint32_t duration = now_complete_frame_ts - last_complete_frame_ts_;
      // LOG_ERROR("Duration {}", 1000 / duration);
      // last_complete_frame_ts_ = now_complete_frame_ts;

      on_receive_complete_frame_(video_frame);
    }
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(13));
  return true;
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

bool RtpVideoReceiver::CheckIsTimeSendRR() {
  uint32_t now_ts = static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::high_resolution_clock::now().time_since_epoch())
          .count());

  if (now_ts - last_send_rtcp_rr_packet_ts_ >= RTCP_RR_INTERVAL) {
    last_send_rtcp_rr_packet_ts_ = now_ts;
    return true;
  } else {
    return false;
  }
}