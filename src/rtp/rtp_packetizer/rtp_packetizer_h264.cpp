#include "rtp_packetizer_h264.h"

RtpPacketizerH264::RtpPacketizerH264(uint32_t ssrc)
    : version_(kRtpVersion),
      has_padding_(false),
      has_extension_(true),
      csrc_count_(0),
      marker_(false),
      payload_type_(rtp::PAYLOAD_TYPE::H264),
      sequence_number_(0),
      timestamp_(0),
      ssrc_(ssrc),
      profile_(0),
      extension_profile_(0),
      extension_len_(0),
      extension_data_(nullptr) {}

RtpPacketizerH264::~RtpPacketizerH264() {}

//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |  ID   |  L=2  |              Absolute Send Time               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// ID (4 bits): The identifier of the extension header field. In WebRTC,
// the ID for Absolute Send Time is typically 3.
// L (4 bits): The length of the extension data in bytes minus 1. For
// Absolute Send Time: the length is 2 (indicating 3 bytes of data).
// Absolute Send Time (24 bits): The absolute send time, with a unit of
// 1/65536 seconds (approximately 15.258 microseconds).

void RtpPacketizerH264::AddAbsSendTimeExtension(
    std::vector<uint8_t>& rtp_packet_frame) {
  uint16_t extension_profile = 0xBEDE;  // One-byte header extension
  uint8_t sub_extension_id = 3;         // ID for Absolute Send Time
  uint8_t sub_extension_length =
      2;  // Length of the extension data in bytes minus 1

  uint32_t abs_send_time =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  abs_send_time &= 0x00FFFFFF;  // Absolute Send Time is 24 bits

  // Add extension profile
  rtp_packet_frame.push_back((extension_profile >> 8) & 0xFF);
  rtp_packet_frame.push_back(extension_profile & 0xFF);

  // Add extension length (in 32-bit words, minus one)
  rtp_packet_frame.push_back(
      0x00);  // Placeholder for length, will be updated later
  rtp_packet_frame.push_back(0x01);  // One 32-bit word

  // Add Absolute Send Time extension
  rtp_packet_frame.push_back((sub_extension_id << 4) | sub_extension_length);
  rtp_packet_frame.push_back((abs_send_time >> 16) & 0xFF);
  rtp_packet_frame.push_back((abs_send_time >> 8) & 0xFF);
  rtp_packet_frame.push_back(abs_send_time & 0xFF);
}

std::vector<std::unique_ptr<RtpPacket>> RtpPacketizerH264::Build(
    uint8_t* payload, uint32_t payload_size, uint32_t rtp_timestamp,
    bool use_rtp_packet_to_send) {
  if (payload_size <= MAX_NALU_LEN) {
    return BuildNalu(payload, payload_size, rtp_timestamp,
                     use_rtp_packet_to_send);
  } else {
    return BuildFua(payload, payload_size, rtp_timestamp,
                    use_rtp_packet_to_send);
  }
}

std::vector<std::unique_ptr<RtpPacket>> RtpPacketizerH264::BuildNalu(
    uint8_t* payload, uint32_t payload_size, uint32_t rtp_timestamp,
    bool use_rtp_packet_to_send) {
  std::vector<std::unique_ptr<RtpPacket>> rtp_packets;

  version_ = kRtpVersion;
  has_padding_ = false;
  has_extension_ = true;
  csrc_count_ = 0;
  marker_ = 1;
  payload_type_ = rtp::PAYLOAD_TYPE(payload_type_);
  sequence_number_++;
  timestamp_ = rtp::kMsToRtpTimestamp * rtp_timestamp;

  if (!csrc_count_) {
  }

  rtp::FU_INDICATOR fu_indicator;
  fu_indicator.forbidden_bit = 0;
  fu_indicator.nal_reference_idc = 1;
  fu_indicator.nal_unit_type = rtp::NAL_UNIT_TYPE::NALU;

  rtp_packet_frame_.clear();
  rtp_packet_frame_.push_back((version_ << 6) | (has_padding_ << 5) |
                              (has_extension_ << 4) | csrc_count_);
  rtp_packet_frame_.push_back((marker_ << 7) | payload_type_);
  rtp_packet_frame_.push_back((sequence_number_ >> 8) & 0xFF);
  rtp_packet_frame_.push_back(sequence_number_ & 0xFF);
  rtp_packet_frame_.push_back((timestamp_ >> 24) & 0xFF);
  rtp_packet_frame_.push_back((timestamp_ >> 16) & 0xFF);
  rtp_packet_frame_.push_back((timestamp_ >> 8) & 0xFF);
  rtp_packet_frame_.push_back(timestamp_ & 0xFF);
  rtp_packet_frame_.push_back((ssrc_ >> 24) & 0xFF);
  rtp_packet_frame_.push_back((ssrc_ >> 16) & 0xFF);
  rtp_packet_frame_.push_back((ssrc_ >> 8) & 0xFF);
  rtp_packet_frame_.push_back(ssrc_ & 0xFF);

  for (uint32_t index = 0; index < csrc_count_ && !csrcs_.empty(); index++) {
    rtp_packet_frame_.push_back((csrcs_[index] >> 24) & 0xFF);
    rtp_packet_frame_.push_back((csrcs_[index] >> 16) & 0xFF);
    rtp_packet_frame_.push_back((csrcs_[index] >> 8) & 0xFF);
    rtp_packet_frame_.push_back(csrcs_[index] & 0xFF);
  }

  if (has_extension_) {
    AddAbsSendTimeExtension(rtp_packet_frame_);
  }

  rtp_packet_frame_.push_back((fu_indicator.forbidden_bit << 7) |
                              (fu_indicator.nal_reference_idc << 5) |
                              fu_indicator.nal_unit_type);

  rtp_packet_frame_.insert(rtp_packet_frame_.end(), payload,
                           payload + payload_size);

  if (use_rtp_packet_to_send) {
    std::unique_ptr<webrtc::RtpPacketToSend> rtp_packet =
        std::make_unique<webrtc::RtpPacketToSend>();
    rtp_packet->Build(rtp_packet_frame_.data(), rtp_packet_frame_.size());
    rtp_packets.emplace_back(std::move(rtp_packet));
  } else {
    std::unique_ptr<RtpPacket> rtp_packet = std::make_unique<RtpPacket>();
    rtp_packet->Build(rtp_packet_frame_.data(), rtp_packet_frame_.size());
    rtp_packets.emplace_back(std::move(rtp_packet));
  }

  return rtp_packets;
}

std::vector<std::unique_ptr<RtpPacket>> RtpPacketizerH264::BuildFua(
    uint8_t* payload, uint32_t payload_size, uint32_t rtp_timestamp,
    bool use_rtp_packet_to_send) {
  std::vector<std::unique_ptr<RtpPacket>> rtp_packets;

  uint32_t last_packet_size = payload_size % MAX_NALU_LEN;
  uint32_t packet_num =
      payload_size / MAX_NALU_LEN + (last_packet_size ? 1 : 0);

  // TODO: use frame timestamp
  uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

  for (uint32_t index = 0; index < packet_num; index++) {
    version_ = kRtpVersion;
    has_padding_ = false;
    has_extension_ = true;
    csrc_count_ = 0;
    marker_ = (index == (packet_num - 1)) ? 1 : 0;
    payload_type_ = rtp::PAYLOAD_TYPE(payload_type_);
    sequence_number_++;
    timestamp_ = timestamp;
    ssrc_ = ssrc_;

    if (!csrc_count_) {
      csrcs_ = csrcs_;
    }

    rtp::FU_INDICATOR fu_indicator;
    fu_indicator.forbidden_bit = 0;
    fu_indicator.nal_reference_idc = 0;
    fu_indicator.nal_unit_type = rtp::NAL_UNIT_TYPE::FU_A;

    rtp::FU_HEADER fu_header;
    fu_header.start = index == 0 ? 1 : 0;
    fu_header.end = index == packet_num - 1 ? 1 : 0;
    fu_header.remain_bit = 0;
    fu_header.nal_unit_type = rtp::NAL_UNIT_TYPE::FU_A;

    rtp_packet_frame_.clear();
    rtp_packet_frame_.push_back((version_ << 6) | (has_padding_ << 5) |
                                (has_extension_ << 4) | csrc_count_);
    rtp_packet_frame_.push_back((marker_ << 7) | payload_type_);
    rtp_packet_frame_.push_back((sequence_number_ >> 8) & 0xFF);
    rtp_packet_frame_.push_back(sequence_number_ & 0xFF);
    rtp_packet_frame_.push_back((timestamp_ >> 24) & 0xFF);
    rtp_packet_frame_.push_back((timestamp_ >> 16) & 0xFF);
    rtp_packet_frame_.push_back((timestamp_ >> 8) & 0xFF);
    rtp_packet_frame_.push_back(timestamp_ & 0xFF);
    rtp_packet_frame_.push_back((ssrc_ >> 24) & 0xFF);
    rtp_packet_frame_.push_back((ssrc_ >> 16) & 0xFF);
    rtp_packet_frame_.push_back((ssrc_ >> 8) & 0xFF);
    rtp_packet_frame_.push_back(ssrc_ & 0xFF);

    for (uint32_t csrc_index = 0; csrc_index < csrc_count_ && !csrcs_.empty();
         csrc_index++) {
      rtp_packet_frame_.push_back((csrcs_[csrc_index] >> 24) & 0xFF);
      rtp_packet_frame_.push_back((csrcs_[csrc_index] >> 16) & 0xFF);
      rtp_packet_frame_.push_back((csrcs_[csrc_index] >> 8) & 0xFF);
      rtp_packet_frame_.push_back(csrcs_[csrc_index] & 0xFF);
    }

    if (has_extension_) {
      AddAbsSendTimeExtension(rtp_packet_frame_);
    }

    rtp_packet_frame_.push_back(fu_indicator.forbidden_bit << 7 |
                                fu_indicator.nal_reference_idc << 5 |
                                fu_indicator.nal_unit_type);

    rtp_packet_frame_.push_back(fu_header.start << 7 | fu_header.end << 6 |
                                fu_header.remain_bit << 5 |
                                fu_header.nal_unit_type);

    if (index == packet_num - 1 && last_packet_size > 0) {
      rtp_packet_frame_.insert(
          rtp_packet_frame_.end(), payload + index * MAX_NALU_LEN,
          payload + index * MAX_NALU_LEN + last_packet_size);
    } else {
      rtp_packet_frame_.insert(rtp_packet_frame_.end(),
                               payload + index * MAX_NALU_LEN,
                               payload + index * MAX_NALU_LEN + MAX_NALU_LEN);
    }

    if (use_rtp_packet_to_send) {
      std::unique_ptr<webrtc::RtpPacketToSend> rtp_packet =
          std::make_unique<webrtc::RtpPacketToSend>();
      rtp_packet->Build(rtp_packet_frame_.data(), rtp_packet_frame_.size());
      rtp_packets.emplace_back(std::move(rtp_packet));
    } else {
      std::unique_ptr<RtpPacket> rtp_packet = std::make_unique<RtpPacket>();
      rtp_packet->Build(rtp_packet_frame_.data(), rtp_packet_frame_.size());
      rtp_packets.emplace_back(std::move(rtp_packet));
    }
  }

  return rtp_packets;
}

std::vector<std::unique_ptr<RtpPacket>> RtpPacketizerH264::BuildPadding(
    uint32_t payload_size, uint32_t rtp_timestamp,
    bool use_rtp_packet_to_send) {
  std::vector<std::unique_ptr<RtpPacket>> rtp_packets;

  uint32_t remaining_size = payload_size;
  while (remaining_size > 0) {
    uint32_t current_payload_size =
        std::min<uint32_t>(remaining_size, MAX_NALU_LEN);

    version_ = kRtpVersion;
    has_padding_ = true;
    has_extension_ = true;
    csrc_count_ = 0;
    marker_ = 0;
    uint8_t payload_type = rtp::PAYLOAD_TYPE(payload_type_ - 1);
    sequence_number_++;
    timestamp_ = rtp::kMsToRtpTimestamp * rtp_timestamp;

    rtp_packet_frame_.clear();
    rtp_packet_frame_.push_back((version_ << 6) | (has_padding_ << 5) |
                                (has_extension_ << 4) | csrc_count_);
    rtp_packet_frame_.push_back((marker_ << 7) | payload_type);
    rtp_packet_frame_.push_back((sequence_number_ >> 8) & 0xFF);
    rtp_packet_frame_.push_back(sequence_number_ & 0xFF);
    rtp_packet_frame_.push_back((timestamp_ >> 24) & 0xFF);
    rtp_packet_frame_.push_back((timestamp_ >> 16) & 0xFF);
    rtp_packet_frame_.push_back((timestamp_ >> 8) & 0xFF);
    rtp_packet_frame_.push_back(timestamp_ & 0xFF);
    rtp_packet_frame_.push_back((ssrc_ >> 24) & 0xFF);
    rtp_packet_frame_.push_back((ssrc_ >> 16) & 0xFF);
    rtp_packet_frame_.push_back((ssrc_ >> 8) & 0xFF);
    rtp_packet_frame_.push_back(ssrc_ & 0xFF);

    for (uint32_t index = 0; index < csrc_count_ && !csrcs_.empty(); index++) {
      rtp_packet_frame_.push_back((csrcs_[index] >> 24) & 0xFF);
      rtp_packet_frame_.push_back((csrcs_[index] >> 16) & 0xFF);
      rtp_packet_frame_.push_back((csrcs_[index] >> 8) & 0xFF);
      rtp_packet_frame_.push_back(csrcs_[index] & 0xFF);
    }

    if (has_extension_) {
      AddAbsSendTimeExtension(rtp_packet_frame_);
    }

    // Add padding bytes
    uint32_t padding_size = current_payload_size;
    rtp_packet_frame_.insert(rtp_packet_frame_.end(), padding_size, 0);
    rtp_packet_frame_.push_back(padding_size);

    if (use_rtp_packet_to_send) {
      std::unique_ptr<webrtc::RtpPacketToSend> rtp_packet =
          std::make_unique<webrtc::RtpPacketToSend>();
      rtp_packet->Build(rtp_packet_frame_.data(), rtp_packet_frame_.size());
      rtp_packets.emplace_back(std::move(rtp_packet));
    } else {
      std::unique_ptr<RtpPacket> rtp_packet = std::make_unique<RtpPacket>();
      rtp_packet->Build(rtp_packet_frame_.data(), rtp_packet_frame_.size());
      rtp_packets.emplace_back(std::move(rtp_packet));
    }

    remaining_size -= current_payload_size;
  }

  return rtp_packets;
}

// bool BuildFec(uint8_t* payload, uint32_t payload_size) {
//   uint8_t** fec_packets =
//       fec_encoder_.Encode((const char*)payload, payload_size);
//   if (nullptr == fec_packets) {
//     LOG_ERROR("Invalid fec_packets");
//     return;
//   }
//   uint8_t num_of_total_packets = 0;
//   uint8_t num_of_source_packets = 0;
//   unsigned int last_packet_size = 0;
//   fec_encoder_.GetFecPacketsParams(payload_size, num_of_total_packets,
//                                    num_of_source_packets,
//                                    last_packet_size);

//   timestamp_ = std::chrono::duration_cast<std::chrono::microseconds>(
//                    std::chrono::system_clock::now().time_since_epoch())
//                    .count();

//   for (uint8_t index = 0; index < num_of_total_packets; index++) {
//     RtpPacket rtp_packet;
//     if (index < num_of_source_packets) {
//       rtp_packet.SetVerion(kRtpVersion);
//       rtp_packet.SetHasPadding(false);
//       rtp_packet.SetHasExtension(has_extension_);
//       rtp_packet.SetMarker(index == num_of_source_packets - 1 ? 1 : 0);
//       rtp_packet.SetPayloadType(rtp::PAYLOAD_TYPE::H264_FEC_SOURCE);
//       rtp_packet.SetSequenceNumber(sequence_number_++);
//       rtp_packet.SetTimestamp(timestamp_);
//       rtp_packet.SetSsrc(ssrc_);

//       if (!csrcs_.empty()) {
//         rtp_packet.SetCsrcs(csrcs_);
//       }

//       if (has_extension_) {
//         rtp_packet.SetExtensionProfile(extension_profile_);
//         rtp_packet.SetExtensionData(extension_data_, extension_len_);
//       }

//       RtpPacket::FU_INDICATOR fu_indicator;
//       fu_indicator.forbidden_bit = 0;
//       fu_indicator.nal_reference_idc = 0;
//       fu_indicator.nal_unit_type = FU_A;

//       RtpPacket::FU_HEADER fu_header;
//       fu_header.start = index == 0 ? 1 : 0;
//       fu_header.end = index == num_of_source_packets - 1 ? 1 : 0;
//       fu_header.remain_bit = 0;
//       fu_header.nal_unit_type = FU_A;

//       rtp_packet.SetFuIndicator(fu_indicator);
//       rtp_packet.SetFuHeader(fu_header);

//       if (index == num_of_source_packets - 1) {
//         if (last_packet_size > 0) {
//           rtp_packet.EncodeH264FecSource(fec_packets[index],
//           last_packet_size,
//                                          index, num_of_source_packets);
//         } else {
//           rtp_packet.EncodeH264FecSource(fec_packets[index], MAX_NALU_LEN,
//                                          index, num_of_source_packets);
//         }
//       } else {
//         rtp_packet.EncodeH264FecSource(fec_packets[index], MAX_NALU_LEN,
//         index,
//                                        num_of_source_packets);
//       }

//     } else if (index >= num_of_source_packets && index <
//     num_of_total_packets) {
//       rtp_packet.SetVerion(kRtpVersion);
//       rtp_packet.SetHasPadding(false);
//       rtp_packet.SetHasExtension(has_extension_);
//       rtp_packet.SetMarker(index == num_of_total_packets - 1 ? 1 : 0);
//       rtp_packet.SetPayloadType(rtp::PAYLOAD_TYPE::H264_FEC_REPAIR);
//       rtp_packet.SetSequenceNumber(sequence_number_++);
//       rtp_packet.SetTimestamp(timestamp_);
//       rtp_packet.SetSsrc(ssrc_);

//       if (!csrcs_.empty()) {
//         rtp_packet.SetCsrcs(csrcs_);
//       }

//       if (has_extension_) {
//         rtp_packet.SetExtensionProfile(extension_profile_);
//         rtp_packet.SetExtensionData(extension_data_, extension_len_);
//       }
//       rtp_packet.EncodeH264FecRepair(fec_packets[index], MAX_NALU_LEN,
//       index,
//                                      num_of_source_packets);
//     }
//     packets.emplace_back(rtp_packet);

//     // if (index < num_of_source_packets) {
//     //   rtp_packet.EncodeH264Fua(fec_packets[index], MAX_NALU_LEN);
//     //   packets.emplace_back(rtp_packet);
//     // }
//   }

//   fec_encoder_.ReleaseFecPackets(fec_packets, payload_size);
//   return;
// }

// if (payload_size <= MAX_NALU_LEN) {
//   RtpPacket rtp_packet;
//   rtp_packet.SetVerion(kRtpVersion);
//   rtp_packet.SetHasPadding(false);

//   has_extension_ = true;
//   uint32_t abs_send_time =
//       std::chrono::duration_cast<std::chrono::microseconds>(
//           std::chrono::system_clock::now().time_since_epoch())
//           .count();
//   rtp_packet.SetAbsoluteSendTimestamp(abs_send_time);

//   rtp_packet.SetHasExtension(has_extension_);
//   rtp_packet.SetMarker(1);
//   rtp_packet.SetPayloadType(rtp::PAYLOAD_TYPE(payload_type_));
//   rtp_packet.SetSequenceNumber(sequence_number_++);

//   timestamp_ = std::chrono::duration_cast<std::chrono::microseconds>(
//                    std::chrono::system_clock::now().time_since_epoch())
//                    .count();

//   rtp_packet.SetTimestamp(timestamp_);
//   rtp_packet.SetSsrc(ssrc_);

//   if (!csrcs_.empty()) {
//     rtp_packet.SetCsrcs(csrcs_);
//   }

//   if (has_extension_) {
//     rtp_packet.SetExtensionProfile(extension_profile_);
//     rtp_packet.SetExtensionData(extension_data_, extension_len_);
//   }

//   RtpPacket::FU_INDICATOR fu_indicator;
//   fu_indicator.forbidden_bit = 0;
//   fu_indicator.nal_reference_idc = 1;
//   fu_indicator.nal_unit_type = NALU;
//   rtp_packet.SetFuIndicator(fu_indicator);

//   rtp_packet.EncodeH264Nalu(payload, payload_size);
//   packets.emplace_back(rtp_packet);

//   return true;
// }
