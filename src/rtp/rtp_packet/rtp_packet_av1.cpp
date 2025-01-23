#include "rtp_packet_av1.h"

RtpPacketAv1::RtpPacketAv1() {}

RtpPacketAv1::~RtpPacketAv1() {}

bool RtpPacketAv1::GetFrameHeaderInfo() {
  const uint8_t* frame_buffer = Payload();
  av1_aggr_header_ = frame_buffer[0];
  z_ = av1_aggr_header_ >> 7;
  y_ = av1_aggr_header_ >> 6 & 0x01;
  w_ = av1_aggr_header_ >> 4 & 0x03;
  n_ = av1_aggr_header_ >> 3 & 0x01;

  return true;
}