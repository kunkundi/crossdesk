/*
 * @Author: DI JUNKUN
 * @Date: 2025-03-26
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _H264_FRAME_ASSEMBER_H_
#define _H264_FRAME_ASSEMBER_H_

#include <array>

#include "rtc_base/numerics/sequence_number_unwrapper.h"
#include "rtp_packet_h264.h"

#define MAX_PACKET_BUFFER_SIZE 2048
#define MAX_TRACKED_SEQUENCE_SIZE 5

class H264FrameAssembler {
 public:
  H264FrameAssembler();
  ~H264FrameAssembler();

 public:
  std::vector<std::unique_ptr<RtpPacketH264>> InsertPacket(
      std::unique_ptr<RtpPacketH264> rtp_packet);

 private:
  int64_t Unwrap(uint16_t seq_num);

  std::unique_ptr<RtpPacketH264>& GetPacketFromBuffer(
      int64_t unwrapped_seq_num);

  std::vector<std::unique_ptr<RtpPacketH264>> FindFrames(
      int64_t unwrapped_seq_num);

 private:
  std::array<std::unique_ptr<RtpPacketH264>, MAX_PACKET_BUFFER_SIZE>
      packet_buffer_;
  std::array<int64_t, MAX_TRACKED_SEQUENCE_SIZE> last_continuous_in_sequence_;
  int64_t last_continuous_in_sequence_index_ = 0;

  webrtc::SeqNumUnwrapper<uint16_t> rtp_seq_num_unwrapper_;
};

#endif