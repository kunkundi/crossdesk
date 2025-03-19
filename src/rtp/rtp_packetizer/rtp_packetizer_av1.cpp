#include "rtp_packetizer_av1.h"

RtpPacketizerAv1::RtpPacketizerAv1(uint32_t ssrc) {}

RtpPacketizerAv1::~RtpPacketizerAv1() {}

std::vector<std::unique_ptr<RtpPacket>> RtpPacketizerAv1::Build(
    uint8_t* payload, uint32_t payload_size, uint32_t rtp_timestamp,
    bool use_rtp_packet_to_send) {
  std::vector<std::unique_ptr<RtpPacket>> rtp_packets;

  return rtp_packets;
}
