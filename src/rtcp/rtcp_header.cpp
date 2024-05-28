#include "rtcp_header.h"

RtcpHeader::RtcpHeader()
    : version_(0),
      padding_(0),
      count_or_format_(0),
      payload_type_(PAYLOAD_TYPE::UNKNOWN),
      length_(0) {}

RtcpHeader::RtcpHeader(const uint8_t* buffer, uint32_t size) {
  if (size < 4) {
    version_ = 2;
    padding_ = 0;
    count_or_format_ = 0;
    payload_type_ = PAYLOAD_TYPE::UNKNOWN;
    length_ = 0;
  } else {
    version_ = buffer[0] >> 6;
    padding_ = buffer[0] >> 5 & 0x01;
    count_or_format_ = buffer[0] & 0x1F;
    payload_type_ = PAYLOAD_TYPE(buffer[1]);
    length_ = (buffer[2] << 8) + buffer[3];
  }
}

RtcpHeader::~RtcpHeader() {}

int RtcpHeader::Encode(uint8_t version, uint8_t padding,
                       uint8_t count_or_format, uint8_t payload_type,
                       uint16_t length, uint8_t* buffer) {
  if (!buffer) {
    return 0;
  }

  buffer[0] = (version << 6) | (padding << 5) | (count_or_format << 4);
  buffer[1] = payload_type;
  buffer[2] = length >> 8 & 0xFF;
  buffer[3] = length & 0xFF;
  return 4;
}