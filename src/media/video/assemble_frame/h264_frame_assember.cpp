#include "h264_frame_assember.h"

#include "log.h"

H264FrameAssembler::H264FrameAssembler() {}

H264FrameAssembler::~H264FrameAssembler() {}

int64_t EuclideanMod(int64_t n, int64_t div) {
  return (n %= div) < 0 ? n + div : n;
}

template <typename T>
inline bool AheadOrAt(T a, T b) {
  const T maxDist = std::numeric_limits<T>::max() / 2 + T(1);
  if (a - b == maxDist) return b < a;
  return b - a < maxDist;
}

int64_t* GetContinuousSequence(
    std::array<int64_t, MAX_TRACKED_SEQUENCE_SIZE>& last_continuous,
    int64_t unwrapped_seq_num) {
  for (int64_t& last : last_continuous) {
    if (unwrapped_seq_num - 1 == last) {
      return &last;
    }
  }
  return nullptr;
}

int64_t H264FrameAssembler::Unwrap(uint16_t seq_num) {
  return (int64_t)seq_num;
}

std::vector<std::unique_ptr<RtpPacketH264>> H264FrameAssembler::InsertPacket(
    std::unique_ptr<RtpPacketH264> rtp_packet) {
  std::vector<std::unique_ptr<RtpPacketH264>> result;

  int64_t unwrapped_seq_num =
      rtp_seq_num_unwrapper_.Unwrap(rtp_packet->SequenceNumber());
  auto& packet_slotted = GetPacketFromBuffer(unwrapped_seq_num);
  if (packet_slotted != nullptr &&
      AheadOrAt(packet_slotted->Timestamp(), rtp_packet->Timestamp())) {
    // The incoming `packet` is old or a duplicate.
    return result;
  } else {
    packet_slotted = std::move(rtp_packet);
  }

  return FindFrames(unwrapped_seq_num);
}

std::unique_ptr<RtpPacketH264>& H264FrameAssembler::GetPacketFromBuffer(
    int64_t unwrapped_seq_num) {
  return packet_buffer_[EuclideanMod(unwrapped_seq_num,
                                     MAX_PACKET_BUFFER_SIZE)];
}

std::vector<std::unique_ptr<RtpPacketH264>> H264FrameAssembler::FindFrames(
    int64_t unwrapped_seq_num) {
  std::vector<std::unique_ptr<RtpPacketH264>> result;
  RtpPacketH264* packet_slotted = GetPacketFromBuffer(unwrapped_seq_num).get();

  int64_t* last_continuous_unwrapped_seq_num =
      GetContinuousSequence(last_continuous_in_sequence_, unwrapped_seq_num);

  // not continuous or the beginning of a new coded video sequence.
  if (last_continuous_unwrapped_seq_num == nullptr) {
    // if (packet_slotted->FuAStart()) {
    //   return result;
    // }

    last_continuous_in_sequence_[last_continuous_in_sequence_index_] =
        unwrapped_seq_num;
    last_continuous_unwrapped_seq_num =
        &last_continuous_in_sequence_[last_continuous_in_sequence_index_];
    last_continuous_in_sequence_index_ =
        (last_continuous_in_sequence_index_ + 1) %
        last_continuous_in_sequence_.size();
  }

  for (int64_t seq_num = unwrapped_seq_num;
       seq_num < unwrapped_seq_num + MAX_PACKET_BUFFER_SIZE;) {
    // Packets that were never assembled into a completed frame will stay in
    // the 'buffer_'. Check that the `packet` sequence number match the expected
    // unwrapped sequence number.
    if (seq_num != Unwrap(packet_slotted->SequenceNumber())) {
      return result;
    }

    *last_continuous_unwrapped_seq_num = seq_num;
    // Last packet of the frame, try to assemble the frame.
    if (packet_slotted->FuAEnd()) {
      uint32_t rtp_timestamp = packet_slotted->Timestamp();

      // Iterate backwards to find where the frame starts.
      for (int64_t seq_num_start = seq_num;
           seq_num_start > seq_num - MAX_PACKET_BUFFER_SIZE; --seq_num_start) {
        auto& prev_packet = GetPacketFromBuffer(seq_num_start - 1);

        if (prev_packet == nullptr ||
            prev_packet->Timestamp() != rtp_timestamp) {
          const auto& current_packet = GetPacketFromBuffer(seq_num_start);

          if (!current_packet->FuAStart()) {
            // First packet of the frame is missing.
            return result;
          }

          for (int64_t seq = seq_num_start; seq <= seq_num; ++seq) {
            auto& packet = GetPacketFromBuffer(seq);
            result.push_back(std::move(packet));
          }
          break;
        }
      }
    }

    seq_num++;
    packet_slotted = GetPacketFromBuffer(seq_num).get();
    if (packet_slotted == nullptr) {
      return result;
    }
  }

  return result;
}
