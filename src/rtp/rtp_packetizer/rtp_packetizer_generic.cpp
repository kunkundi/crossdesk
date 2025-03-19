#include "rtp_packetizer_generic.h"

RtpPacketizerGeneric::RtpPacketizerGeneric(uint32_t ssrc)
    : version_(kRtpVersion),
      has_padding_(false),
      has_extension_(true),
      csrc_count_(0),
      marker_(false),
      payload_type_(rtp::PAYLOAD_TYPE::DATA),
      sequence_number_(0),
      timestamp_(0),
      ssrc_(ssrc),
      profile_(0),
      extension_profile_(0),
      extension_len_(0),
      extension_data_(nullptr) {}

RtpPacketizerGeneric::~RtpPacketizerGeneric() {}

void RtpPacketizerGeneric::AddAbsSendTimeExtension(
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

std::vector<std::unique_ptr<RtpPacket>> RtpPacketizerGeneric::Build(
    uint8_t* payload, uint32_t payload_size, uint32_t rtp_timestamp,
    bool use_rtp_packet_to_send) {
  uint32_t last_packet_size = payload_size % MAX_NALU_LEN;
  uint32_t packet_num =
      payload_size / MAX_NALU_LEN + (last_packet_size ? 1 : 0);

  // TODO: use frame timestamp
  uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

  std::vector<std::unique_ptr<RtpPacket>> rtp_packets;

  for (uint32_t index = 0; index < packet_num; index++) {
    version_ = kRtpVersion;
    has_padding_ = false;
    has_extension_ = true;
    csrc_count_ = 0;
    marker_ = index == packet_num - 1 ? 1 : 0;
    payload_type_ = rtp::PAYLOAD_TYPE(payload_type_);
    sequence_number_++;
    timestamp_ = timestamp;
    ssrc_ = ssrc_;

    if (!csrc_count_) {
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
      AddAbsSendTimeExtension(rtp_packet_frame_);
    }

    if (index == packet_num - 1 && last_packet_size > 0) {
      rtp_packet_frame_.insert(rtp_packet_frame_.end(), payload,
                               payload + last_packet_size);
    } else {
      rtp_packet_frame_.insert(rtp_packet_frame_.end(), payload,
                               payload + MAX_NALU_LEN);
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
