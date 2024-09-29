#include "byte_buffer.h"
#include "log.h"
#include "obu_parser.h"
#include "rtp_codec.h"

void EncodeAv1(uint8_t* buffer, size_t size, std::vector<RtpPacket>& packets) {
  std::vector<Obu> obus = obu::ParseObus(buffer, size);
  std::vector<Packet> packets = obu::Packetizer(obus);

  int num_packets = packets.size();

  if (1 == num_packets) {
  }

  // LOG_ERROR("Total size = [{}]", size);

  uint32_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

  for (int i = 0; i < obus.size(); i++) {
    // LOG_ERROR("1 [{}] Obu size = [{}], Obu type [{}]", i, obus[i].size_,
    //           ObuTypeToString((OBU_TYPE)ObuType(obus[i].header_)));

    if (obus[i].size_ <= MAX_NALU_LEN) {
      RtpPacket rtp_packet;
      rtp_packet.SetVerion(version_);
      rtp_packet.SetHasPadding(has_padding_);
      rtp_packet.SetHasExtension(has_extension_);
      rtp_packet.SetMarker(1);
      rtp_packet.SetPayloadType(RtpPacket::PAYLOAD_TYPE(payload_type_));
      rtp_packet.SetSequenceNumber(sequence_number_++);
      rtp_packet.SetTimestamp(timestamp);
      rtp_packet.SetSsrc(ssrc_);

      if (!csrcs_.empty()) {
        rtp_packet.SetCsrcs(csrcs_);
      }

      if (has_extension_) {
        rtp_packet.SetExtensionProfile(extension_profile_);
        rtp_packet.SetExtensionData(extension_data_, extension_len_);
      }

      rtp_packet.SetAv1AggrHeader(0, 0, 1, 0);
      rtp_packet.EncodeAv1(obus[i].data_, obus[i].size_);

      packets.emplace_back(rtp_packet);
    } else {
      size_t last_packet_size = obus[i].size_ % MAX_NALU_LEN;
      size_t packet_num =
          obus[i].size_ / MAX_NALU_LEN + (last_packet_size ? 1 : 0);

      for (size_t index = 0; index < packet_num; index++) {
        RtpPacket rtp_packet;
        rtp_packet.SetVerion(version_);
        rtp_packet.SetHasPadding(has_padding_);
        rtp_packet.SetHasExtension(has_extension_);
        rtp_packet.SetMarker(index == packet_num - 1 ? 1 : 0);
        rtp_packet.SetPayloadType(RtpPacket::PAYLOAD_TYPE(payload_type_));
        rtp_packet.SetSequenceNumber(sequence_number_++);
        rtp_packet.SetTimestamp(timestamp);
        rtp_packet.SetSsrc(ssrc_);
        if (!csrcs_.empty()) {
          rtp_packet.SetCsrcs(csrcs_);
        }
        if (has_extension_) {
          rtp_packet.SetExtensionProfile(extension_profile_);
          rtp_packet.SetExtensionData(extension_data_, extension_len_);
        }

        int z = index != 0 ? 1 : 0;
        int y = index != packet_num - 1 ? 1 : 0;
        int w = 1;
        int n = (frame_type == VideoFrameType::kVideoFrameKey) &&
                        (ObuType(obus[i].header_) == kObuTypeSequenceHeader)
                    ? 1
                    : 0;
        rtp_packet.SetAv1AggrHeader(z, y, w, n);

        if (index == packet_num - 1 && last_packet_size > 0) {
          rtp_packet.EncodeAv1(obus[i].data_ + index * MAX_NALU_LEN,
                               last_packet_size);
        } else {
          rtp_packet.EncodeAv1(obus[i].data_ + index * MAX_NALU_LEN,
                               MAX_NALU_LEN);
        }

        packets.emplace_back(rtp_packet);
      }
    }
  }
}