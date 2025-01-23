#include "rtp_packetizer_av1.h"

RtpPacketizerAv1::RtpPacketizerAv1() {}

RtpPacketizerAv1::~RtpPacketizerAv1() {}

std::vector<RtpPacket> RtpPacketizerAv1::Build(uint8_t* payload,
                                               uint32_t payload_size) {
  std::vector<RtpPacket> rtp_packets;
  return rtp_packets;
}