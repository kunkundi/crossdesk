#include "rtp_packetizer_generic.h"

RtpPacketizerGeneric::RtpPacketizerGeneric()
    : version_(kRtpVersion),
      has_padding_(false),
      has_extension_(true),
      csrc_count_(0),
      marker_(false),
      payload_type_(rtp::PAYLOAD_TYPE::DATA),
      sequence_number_(1),
      timestamp_(0),
      ssrc_(0),
      profile_(0),
      extension_profile_(0),
      extension_len_(0),
      extension_data_(nullptr) {}

RtpPacketizerGeneric::~RtpPacketizerGeneric() {}

std::vector<RtpPacket> RtpPacketizerGeneric::Build(uint8_t* payload,
                                                   uint32_t payload_size) {
  uint32_t last_packet_size = payload_size % MAX_NALU_LEN;
  uint32_t packet_num =
      payload_size / MAX_NALU_LEN + (last_packet_size ? 1 : 0);

  // TODO: use frame timestamp
  timestamp_ = std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();

  std::vector<RtpPacket> rtp_packets;
  for (uint32_t index = 0; index < packet_num; index++) {
    version_ = kRtpVersion;
    has_padding_ = false;
    has_extension_ = true;
    csrc_count_ = 0;
    marker_ = index == packet_num - 1 ? 1 : 0;
    payload_type_ = rtp::PAYLOAD_TYPE(payload_type_);
    sequence_number_++;
    timestamp_ = timestamp_;
    ssrc_ = ssrc_;

    if (!csrc_count_) {
      csrcs_ = csrcs_;
    }

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
      extension_profile_ = kOneByteExtensionProfileId;
      extension_len_ = 5;  // 2 bytes for profile, 2 bytes for length, 3 bytes
                           // for abs_send_time

      uint32_t abs_send_time =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();

      abs_send_time &= 0x00FFFFFF;

      uint8_t sub_extension_id = 0;
      uint8_t sub_extension_len = 2;

      rtp_packet_frame_.push_back(extension_profile_ >> 8);
      rtp_packet_frame_.push_back(extension_profile_ & 0xff);
      rtp_packet_frame_.push_back((extension_len_ >> 8) & 0xFF);
      rtp_packet_frame_.push_back(extension_len_ & 0xFF);
      rtp_packet_frame_.push_back(sub_extension_id << 4 | sub_extension_len);
      rtp_packet_frame_.push_back((abs_send_time >> 16) & 0xFF);
      rtp_packet_frame_.push_back((abs_send_time >> 8) & 0xFF);
      rtp_packet_frame_.push_back(abs_send_time & 0xFF);
    }

    if (index == packet_num - 1 && last_packet_size > 0) {
      rtp_packet_frame_.insert(rtp_packet_frame_.end(), payload,
                               payload + last_packet_size);
    } else {
      rtp_packet_frame_.insert(rtp_packet_frame_.end(), payload,
                               payload + MAX_NALU_LEN);
    }

    RtpPacket rtp_packet;
    rtp_packet.Build(rtp_packet_frame_.data(), rtp_packet_frame_.size());

    rtp_packets.emplace_back(rtp_packet);
  }

  return rtp_packets;
}
