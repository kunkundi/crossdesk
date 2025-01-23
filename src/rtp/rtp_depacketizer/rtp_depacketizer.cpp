#include "rtp_depacketizer.h"

std::shared_ptr<RtpDepacketizer> Create(uint32_t payload_type, uint8_t* payload,
                                        size_t payload_size) {
  switch (payload_type) {
    case rtp::PAYLOAD_TYPE::H264:
      return std::make_shared<RtpDepacketizerH264>(payload, payload_size);
    case rtp::PAYLOAD_TYPE::AV1:
      return std::make_shared<RtpDepacketizerAv1>(payload, payload_size);
  }
}