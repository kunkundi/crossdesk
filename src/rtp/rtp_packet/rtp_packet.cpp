#include "rtp_packet.h"

#include <string>

RtpPacket::RtpPacket() {}

RtpPacket::RtpPacket(size_t size) : buffer_(size) {}

RtpPacket::RtpPacket(const RtpPacket &rtp_packet) = default;

RtpPacket::RtpPacket(RtpPacket &&rtp_packet) = default;

RtpPacket &RtpPacket::operator=(const RtpPacket &rtp_packet) = default;

RtpPacket &RtpPacket::operator=(RtpPacket &&rtp_packet) = default;

RtpPacket::~RtpPacket() = default;

bool RtpPacket::Build(const uint8_t *buffer, uint32_t size) {
  if (!Parse(buffer, size)) {
    LOG_WARN("RtpPacket::Build: parse failed");
    return false;
  }
  buffer_.SetData(buffer, size);
  size_ = size;
  return true;
}

bool RtpPacket::Parse(const uint8_t *buffer, uint32_t size) {
  if (size < kFixedHeaderSize) {
    LOG_WARN("RtpPacket::Parse: size is too small");
    return false;
  }

  // 1st byte
  version_ = (buffer[payload_offset_] >> 6) & 0x03;
  if (version_ != kRtpVersion) {
    LOG_WARN("RtpPacket::Parse: version is not qual to kRtpVersion");
    return false;
  }
  has_padding_ = (buffer[payload_offset_] >> 5) & 0x01;
  has_extension_ = (buffer[payload_offset_] >> 4) & 0x01;
  csrc_count_ = buffer[payload_offset_] & 0x0f;
  if (csrc_count_ > kMaxRtpCsrcSize) {
    LOG_WARN("RtpPacket::Parse: csrc count is too large");
    return false;
  }
  payload_offset_++;

  // 2nd byte
  marker_ = (buffer[payload_offset_] >> 7) & 0x01;
  payload_type_ = buffer[payload_offset_] & 0x7f;
  payload_offset_ += 1;

  // 3rd byte and 4th byte
  sequence_number_ =
      (buffer[payload_offset_] << 8) | buffer[payload_offset_ + 1];
  payload_offset_ += 2;

  // 5th byte to 8th byte
  timestamp_ = (buffer[payload_offset_] << 24) |
               (buffer[payload_offset_ + 1] << 16) |
               (buffer[payload_offset_ + 2] << 8) | buffer[payload_offset_ + 3];
  payload_offset_ += 4;

  // 9th byte to 12th byte
  ssrc_ = (buffer[payload_offset_] << 24) |
          (buffer[payload_offset_ + 1] << 16) |
          (buffer[payload_offset_ + 2] << 8) | buffer[payload_offset_ + 3];

  payload_offset_ = kFixedHeaderSize;

  if (kFixedHeaderSize + csrc_count_ * 4 > size) {
    LOG_WARN("RtpPacket::Parse: csrc count is too large");
    return false;
  }
  // csrc
  for (uint32_t csrc_index = 0; csrc_index < csrc_count_; csrc_index++) {
    uint32_t csrc = (buffer[payload_offset_ + csrc_index * 4] << 24) |
                    (buffer[payload_offset_ + 1 + csrc_index * 4] << 16) |
                    (buffer[payload_offset_ + 2 + csrc_index * 4] << 8) |
                    buffer[payload_offset_ + 3 + csrc_index * 4];
    csrcs_.push_back(csrc);
  }

  payload_offset_ = kFixedHeaderSize + csrc_count_ * 4;
  if (payload_offset_ > size) {
    LOG_WARN("RtpPacket::Parse: payload offset is too large");
    return false;
  }

  // extensions
  if (has_extension_) {
    if (payload_offset_ + 4 > size) {
      LOG_WARN("RtpPacket::Parse: extension profile is too large");
      return false;
    }
    extension_profile_ =
        (buffer[payload_offset_] << 8) | buffer[payload_offset_ + 1];
    extension_len_ =
        (buffer[payload_offset_ + 2] << 8) | buffer[payload_offset_ + 3];

    if (payload_offset_ + extension_len_ > size) {
      LOG_WARN("RtpPacket::Parse: extension len is too large");
      return false;
    }

    size_t offset = payload_offset_ + 4;
    while (offset < size && extension_len_ > 0) {
      uint8_t id = buffer[offset] >> 4;
      uint8_t len = (buffer[offset] & 0x0F) + 1;
      if (offset + 1 + len > size) {
        break;
      }
      Extension extension;
      extension.id = id;
      extension.data =
          std::vector<uint8_t>(buffer + offset + 1, buffer + offset + 1 + len);
      extensions_.push_back(extension);
      offset += 1 + len;
    }
    payload_offset_ += extension_len_;
  }

  if (has_padding_ && payload_offset_ < size) {
    padding_size_ = buffer[size - 1];
    if (padding_size_ == 0) {
      LOG_WARN("Padding was set, but padding size is zero");
      return false;
    }
  } else {
    padding_size_ = 0;
  }

  // payload
  if (payload_offset_ + padding_size_ > size) {
    LOG_WARN("RtpPacket::Parse: payload size is too large");
    return false;
  }
  payload_size_ = size - payload_offset_ - padding_size_;

  return true;
}