#include "rtp_packetizer.h"

#include "rtp_packetizer_av1.h"
#include "rtp_packetizer_generic.h"
#include "rtp_packetizer_h264.h"

std::unique_ptr<RtpPacketizer> RtpPacketizer::Create(uint32_t payload_type) {
  switch (payload_type) {
    case rtp::PAYLOAD_TYPE::H264:
      return std::make_unique<RtpPacketizerH264>();
    case rtp::PAYLOAD_TYPE::AV1:
      return std::make_unique<RtpPacketizerAv1>();
    default:
      return std::make_unique<RtpPacketizerGeneric>();
  }
}