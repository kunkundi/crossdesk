#include "rtp_codec.h"

#include <chrono>

#include "log.h"
#include "obu_parser.h"

#define RTP_VERSION 2
#define NALU 1
#define FU_A 28
#define FU_B 29

constexpr int kObuTypeSequenceHeader = 1;

RtpCodec ::RtpCodec(RtpPacket::PAYLOAD_TYPE payload_type)
    : version_(RTP_VERSION),
      has_padding_(false),
      has_extension_(false),
      payload_type_(payload_type),
      sequence_number_(0) {
  fec_encoder_.Init();
}

RtpCodec ::~RtpCodec() {
  if (extension_data_) {
    delete extension_data_;
    extension_data_ = nullptr;
  }

  // if (rtp_packet_) {
  //   delete rtp_packet_;
  //   rtp_packet_ = nullptr;
  // }
}

void RtpCodec::Encode(uint8_t* buffer, size_t size,
                      std::vector<RtpPacket>& packets) {
  if (RtpPacket::PAYLOAD_TYPE::H264 == payload_type_) {
    if (fec_enable_ && IsKeyFrame((const uint8_t*)buffer, size)) {
      uint8_t** fec_packets = fec_encoder_.Encode((const char*)buffer, size);
      if (nullptr == fec_packets) {
        LOG_ERROR("Invalid fec_packets");
        return;
      }
      unsigned int num_of_total_packets = 0;
      unsigned int num_of_source_packets = 0;
      unsigned int last_packet_size = 0;
      fec_encoder_.GetFecPacketsParams(size, num_of_total_packets,
                                       num_of_source_packets, last_packet_size);

      timestamp_ =
          std::chrono::high_resolution_clock::now().time_since_epoch().count();

      for (unsigned int index = 0; index < num_of_total_packets; index++) {
        RtpPacket rtp_packet;
        if (index < num_of_source_packets) {
          rtp_packet.SetVerion(version_);
          rtp_packet.SetHasPadding(has_padding_);
          rtp_packet.SetHasExtension(has_extension_);
          rtp_packet.SetMarker(index == num_of_source_packets - 1 ? 1 : 0);
          rtp_packet.SetPayloadType(RtpPacket::PAYLOAD_TYPE::H264_FEC_SOURCE);
          rtp_packet.SetSequenceNumber(sequence_number_++);
          rtp_packet.SetTimestamp(timestamp_);
          rtp_packet.SetSsrc(ssrc_);

          if (!csrcs_.empty()) {
            rtp_packet.SetCsrcs(csrcs_);
          }

          if (has_extension_) {
            rtp_packet.SetExtensionProfile(extension_profile_);
            rtp_packet.SetExtensionData(extension_data_, extension_len_);
          }

          RtpPacket::FU_INDICATOR fu_indicator;
          fu_indicator.forbidden_bit = 0;
          fu_indicator.nal_reference_idc = 0;
          fu_indicator.nal_unit_type = FU_A;

          RtpPacket::FU_HEADER fu_header;
          fu_header.start = index == 0 ? 1 : 0;
          fu_header.end = index == num_of_source_packets - 1 ? 1 : 0;
          fu_header.remain_bit = 0;
          fu_header.nal_unit_type = FU_A;

          rtp_packet.SetFuIndicator(fu_indicator);
          rtp_packet.SetFuHeader(fu_header);

          if (index == num_of_source_packets - 1) {
            if (last_packet_size > 0) {
              rtp_packet.EncodeH264FecSource(fec_packets[index],
                                             last_packet_size, index,
                                             num_of_source_packets);
            } else {
              rtp_packet.EncodeH264FecSource(fec_packets[index], MAX_NALU_LEN,
                                             index, num_of_source_packets);
            }
          } else {
            rtp_packet.EncodeH264FecSource(fec_packets[index], MAX_NALU_LEN,
                                           index, num_of_source_packets);
          }

        } else if (index >= num_of_source_packets &&
                   index < num_of_total_packets) {
          rtp_packet.SetVerion(version_);
          rtp_packet.SetHasPadding(has_padding_);
          rtp_packet.SetHasExtension(has_extension_);
          rtp_packet.SetMarker(index == num_of_total_packets - 1 ? 1 : 0);
          rtp_packet.SetPayloadType(RtpPacket::PAYLOAD_TYPE::H264_FEC_REPAIR);
          rtp_packet.SetSequenceNumber(sequence_number_++);
          rtp_packet.SetTimestamp(timestamp_);
          rtp_packet.SetSsrc(ssrc_);

          if (!csrcs_.empty()) {
            rtp_packet.SetCsrcs(csrcs_);
          }

          if (has_extension_) {
            rtp_packet.SetExtensionProfile(extension_profile_);
            rtp_packet.SetExtensionData(extension_data_, extension_len_);
          }
          rtp_packet.EncodeH264FecRepair(fec_packets[index], MAX_NALU_LEN,
                                         index, num_of_source_packets);
        }
        packets.emplace_back(rtp_packet);

        // if (index < num_of_source_packets) {
        //   rtp_packet.EncodeH264Fua(fec_packets[index], MAX_NALU_LEN);
        //   packets.emplace_back(rtp_packet);
        // }
      }

      fec_encoder_.ReleaseFecPackets(fec_packets, size);
      return;
    }

    if (size <= MAX_NALU_LEN) {
      RtpPacket rtp_packet;
      rtp_packet.SetVerion(version_);
      rtp_packet.SetHasPadding(has_padding_);
      rtp_packet.SetHasExtension(has_extension_);
      rtp_packet.SetMarker(1);
      rtp_packet.SetPayloadType(RtpPacket::PAYLOAD_TYPE(payload_type_));
      rtp_packet.SetSequenceNumber(sequence_number_++);

      timestamp_ =
          std::chrono::high_resolution_clock::now().time_since_epoch().count();
      rtp_packet.SetTimestamp(timestamp_);
      rtp_packet.SetSsrc(ssrc_);

      if (!csrcs_.empty()) {
        rtp_packet.SetCsrcs(csrcs_);
      }

      if (has_extension_) {
        rtp_packet.SetExtensionProfile(extension_profile_);
        rtp_packet.SetExtensionData(extension_data_, extension_len_);
      }

      RtpPacket::FU_INDICATOR fu_indicator;
      fu_indicator.forbidden_bit = 0;
      fu_indicator.nal_reference_idc = 1;
      fu_indicator.nal_unit_type = NALU;
      rtp_packet.SetFuIndicator(fu_indicator);

      rtp_packet.EncodeH264Nalu(buffer, size);
      packets.emplace_back(rtp_packet);

    } else {
      size_t last_packet_size = size % MAX_NALU_LEN;
      size_t packet_num = size / MAX_NALU_LEN + (last_packet_size ? 1 : 0);
      timestamp_ =
          std::chrono::high_resolution_clock::now().time_since_epoch().count();

      for (size_t index = 0; index < packet_num; index++) {
        RtpPacket rtp_packet;
        rtp_packet.SetVerion(version_);
        rtp_packet.SetHasPadding(has_padding_);
        rtp_packet.SetHasExtension(has_extension_);
        rtp_packet.SetMarker(index == packet_num - 1 ? 1 : 0);
        rtp_packet.SetPayloadType(RtpPacket::PAYLOAD_TYPE(payload_type_));
        rtp_packet.SetSequenceNumber(sequence_number_++);
        rtp_packet.SetTimestamp(timestamp_);
        rtp_packet.SetSsrc(ssrc_);

        if (!csrcs_.empty()) {
          rtp_packet.SetCsrcs(csrcs_);
        }

        if (has_extension_) {
          rtp_packet.SetExtensionProfile(extension_profile_);
          rtp_packet.SetExtensionData(extension_data_, extension_len_);
        }

        RtpPacket::FU_INDICATOR fu_indicator;
        fu_indicator.forbidden_bit = 0;
        fu_indicator.nal_reference_idc = 0;
        fu_indicator.nal_unit_type = FU_A;

        RtpPacket::FU_HEADER fu_header;
        fu_header.start = index == 0 ? 1 : 0;
        fu_header.end = index == packet_num - 1 ? 1 : 0;
        fu_header.remain_bit = 0;
        fu_header.nal_unit_type = FU_A;

        rtp_packet.SetFuIndicator(fu_indicator);
        rtp_packet.SetFuHeader(fu_header);

        if (index == packet_num - 1 && last_packet_size > 0) {
          rtp_packet.EncodeH264Fua(buffer + index * MAX_NALU_LEN,
                                   last_packet_size);
        } else {
          rtp_packet.EncodeH264Fua(buffer + index * MAX_NALU_LEN, MAX_NALU_LEN);
        }
        packets.emplace_back(rtp_packet);
      }
    }
  } else if (RtpPacket::PAYLOAD_TYPE::AV1 == payload_type_) {
    std::vector<Obu> obus = ParseObus(buffer, size);
    LOG_ERROR("Total size = [{}]", size);
    for (int i = 0; i < obus.size(); i++) {
      LOG_ERROR("[{}] Obu size = [{}], Obu type [{}]", i, obus[i].size_,
                ObuTypeToString((OBU_TYPE)ObuType(obus[i].header_)));
      if (obus[i].size_ <= MAX_NALU_LEN) {
        RtpPacket rtp_packet;
        rtp_packet.SetVerion(version_);
        rtp_packet.SetHasPadding(has_padding_);
        rtp_packet.SetHasExtension(has_extension_);
        rtp_packet.SetMarker(1);
        rtp_packet.SetPayloadType(RtpPacket::PAYLOAD_TYPE(payload_type_));
        rtp_packet.SetSequenceNumber(sequence_number_++);

        timestamp_ = std::chrono::high_resolution_clock::now()
                         .time_since_epoch()
                         .count();
        rtp_packet.SetTimestamp(timestamp_);
        rtp_packet.SetSsrc(ssrc_);

        if (!csrcs_.empty()) {
          rtp_packet.SetCsrcs(csrcs_);
        }

        if (has_extension_) {
          rtp_packet.SetExtensionProfile(extension_profile_);
          rtp_packet.SetExtensionData(extension_data_, extension_len_);
        }

        rtp_packet.SetAv1AggrHeader(0, 0, 1, 0);

        rtp_packet.EncodeAv1(obus[i].data_, obus[i].payload_size_);
        packets.emplace_back(rtp_packet);
      } else {
        size_t last_packet_size = obus[i].payload_size_ % MAX_NALU_LEN;
        size_t packet_num =
            obus[i].payload_size_ / MAX_NALU_LEN + (last_packet_size ? 1 : 0);
        timestamp_ = std::chrono::high_resolution_clock::now()
                         .time_since_epoch()
                         .count();
        for (size_t index = 0; index < packet_num; index++) {
          RtpPacket rtp_packet;
          rtp_packet.SetVerion(version_);
          rtp_packet.SetHasPadding(has_padding_);
          rtp_packet.SetHasExtension(has_extension_);
          rtp_packet.SetMarker(index == packet_num - 1 ? 1 : 0);
          rtp_packet.SetPayloadType(RtpPacket::PAYLOAD_TYPE(payload_type_));
          rtp_packet.SetSequenceNumber(sequence_number_++);
          rtp_packet.SetTimestamp(timestamp_);
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
          int n = 0;
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
  } else if (RtpPacket::PAYLOAD_TYPE::OPUS == payload_type_) {
    RtpPacket rtp_packet;
    rtp_packet.SetVerion(version_);
    rtp_packet.SetHasPadding(has_padding_);
    rtp_packet.SetHasExtension(has_extension_);
    rtp_packet.SetMarker(1);
    rtp_packet.SetPayloadType(RtpPacket::PAYLOAD_TYPE(payload_type_));
    rtp_packet.SetSequenceNumber(sequence_number_++);

    timestamp_ =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    rtp_packet.SetTimestamp(timestamp_);
    rtp_packet.SetSsrc(ssrc_);

    rtp_packet.Encode(buffer, size);
    packets.emplace_back(rtp_packet);
  } else if (RtpPacket::PAYLOAD_TYPE::DATA == payload_type_) {
    RtpPacket rtp_packet;
    rtp_packet.SetVerion(version_);
    rtp_packet.SetHasPadding(has_padding_);
    rtp_packet.SetHasExtension(has_extension_);
    rtp_packet.SetMarker(1);
    rtp_packet.SetPayloadType(RtpPacket::PAYLOAD_TYPE(payload_type_));
    rtp_packet.SetSequenceNumber(sequence_number_++);

    timestamp_ =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    rtp_packet.SetTimestamp(timestamp_);
    rtp_packet.SetSsrc(ssrc_);

    rtp_packet.Encode(buffer, size);
    packets.emplace_back(rtp_packet);
  }
}

void RtpCodec::Encode(VideoFrameType frame_type, uint8_t* buffer, size_t size,
                      std::vector<RtpPacket>& packets) {
  if (RtpPacket::PAYLOAD_TYPE::H264 == payload_type_) {
    if (fec_enable_ && IsKeyFrame((const uint8_t*)buffer, size)) {
      uint8_t** fec_packets = fec_encoder_.Encode((const char*)buffer, size);
      if (nullptr == fec_packets) {
        LOG_ERROR("Invalid fec_packets");
        return;
      }
      unsigned int num_of_total_packets = 0;
      unsigned int num_of_source_packets = 0;
      unsigned int last_packet_size = 0;
      fec_encoder_.GetFecPacketsParams(size, num_of_total_packets,
                                       num_of_source_packets, last_packet_size);

      timestamp_ =
          std::chrono::high_resolution_clock::now().time_since_epoch().count();

      for (unsigned int index = 0; index < num_of_total_packets; index++) {
        RtpPacket rtp_packet;
        if (index < num_of_source_packets) {
          rtp_packet.SetVerion(version_);
          rtp_packet.SetHasPadding(has_padding_);
          rtp_packet.SetHasExtension(has_extension_);
          rtp_packet.SetMarker(index == num_of_source_packets - 1 ? 1 : 0);
          rtp_packet.SetPayloadType(RtpPacket::PAYLOAD_TYPE::H264_FEC_SOURCE);
          rtp_packet.SetSequenceNumber(sequence_number_++);
          rtp_packet.SetTimestamp(timestamp_);
          rtp_packet.SetSsrc(ssrc_);

          if (!csrcs_.empty()) {
            rtp_packet.SetCsrcs(csrcs_);
          }

          if (has_extension_) {
            rtp_packet.SetExtensionProfile(extension_profile_);
            rtp_packet.SetExtensionData(extension_data_, extension_len_);
          }

          RtpPacket::FU_INDICATOR fu_indicator;
          fu_indicator.forbidden_bit = 0;
          fu_indicator.nal_reference_idc = 0;
          fu_indicator.nal_unit_type = FU_A;

          RtpPacket::FU_HEADER fu_header;
          fu_header.start = index == 0 ? 1 : 0;
          fu_header.end = index == num_of_source_packets - 1 ? 1 : 0;
          fu_header.remain_bit = 0;
          fu_header.nal_unit_type = FU_A;

          rtp_packet.SetFuIndicator(fu_indicator);
          rtp_packet.SetFuHeader(fu_header);

          if (index == num_of_source_packets - 1) {
            if (last_packet_size > 0) {
              rtp_packet.EncodeH264FecSource(fec_packets[index],
                                             last_packet_size, index,
                                             num_of_source_packets);
            } else {
              rtp_packet.EncodeH264FecSource(fec_packets[index], MAX_NALU_LEN,
                                             index, num_of_source_packets);
            }
          } else {
            rtp_packet.EncodeH264FecSource(fec_packets[index], MAX_NALU_LEN,
                                           index, num_of_source_packets);
          }

        } else if (index >= num_of_source_packets &&
                   index < num_of_total_packets) {
          rtp_packet.SetVerion(version_);
          rtp_packet.SetHasPadding(has_padding_);
          rtp_packet.SetHasExtension(has_extension_);
          rtp_packet.SetMarker(index == num_of_total_packets - 1 ? 1 : 0);
          rtp_packet.SetPayloadType(RtpPacket::PAYLOAD_TYPE::H264_FEC_REPAIR);
          rtp_packet.SetSequenceNumber(sequence_number_++);
          rtp_packet.SetTimestamp(timestamp_);
          rtp_packet.SetSsrc(ssrc_);

          if (!csrcs_.empty()) {
            rtp_packet.SetCsrcs(csrcs_);
          }

          if (has_extension_) {
            rtp_packet.SetExtensionProfile(extension_profile_);
            rtp_packet.SetExtensionData(extension_data_, extension_len_);
          }
          rtp_packet.EncodeH264FecRepair(fec_packets[index], MAX_NALU_LEN,
                                         index, num_of_source_packets);
        }
        packets.emplace_back(rtp_packet);

        // if (index < num_of_source_packets) {
        //   rtp_packet.EncodeH264Fua(fec_packets[index], MAX_NALU_LEN);
        //   packets.emplace_back(rtp_packet);
        // }
      }

      fec_encoder_.ReleaseFecPackets(fec_packets, size);
      return;
    }

    if (size <= MAX_NALU_LEN) {
      RtpPacket rtp_packet;
      rtp_packet.SetVerion(version_);
      rtp_packet.SetHasPadding(has_padding_);
      rtp_packet.SetHasExtension(has_extension_);
      rtp_packet.SetMarker(1);
      rtp_packet.SetPayloadType(RtpPacket::PAYLOAD_TYPE(payload_type_));
      rtp_packet.SetSequenceNumber(sequence_number_++);

      timestamp_ =
          std::chrono::high_resolution_clock::now().time_since_epoch().count();
      rtp_packet.SetTimestamp(timestamp_);
      rtp_packet.SetSsrc(ssrc_);

      if (!csrcs_.empty()) {
        rtp_packet.SetCsrcs(csrcs_);
      }

      if (has_extension_) {
        rtp_packet.SetExtensionProfile(extension_profile_);
        rtp_packet.SetExtensionData(extension_data_, extension_len_);
      }

      RtpPacket::FU_INDICATOR fu_indicator;
      fu_indicator.forbidden_bit = 0;
      fu_indicator.nal_reference_idc = 1;
      fu_indicator.nal_unit_type = NALU;
      rtp_packet.SetFuIndicator(fu_indicator);

      rtp_packet.EncodeH264Nalu(buffer, size);
      packets.emplace_back(rtp_packet);

    } else {
      size_t last_packet_size = size % MAX_NALU_LEN;
      size_t packet_num = size / MAX_NALU_LEN + (last_packet_size ? 1 : 0);
      timestamp_ =
          std::chrono::high_resolution_clock::now().time_since_epoch().count();

      for (size_t index = 0; index < packet_num; index++) {
        RtpPacket rtp_packet;
        rtp_packet.SetVerion(version_);
        rtp_packet.SetHasPadding(has_padding_);
        rtp_packet.SetHasExtension(has_extension_);
        rtp_packet.SetMarker(index == packet_num - 1 ? 1 : 0);
        rtp_packet.SetPayloadType(RtpPacket::PAYLOAD_TYPE(payload_type_));
        rtp_packet.SetSequenceNumber(sequence_number_++);
        rtp_packet.SetTimestamp(timestamp_);
        rtp_packet.SetSsrc(ssrc_);

        if (!csrcs_.empty()) {
          rtp_packet.SetCsrcs(csrcs_);
        }

        if (has_extension_) {
          rtp_packet.SetExtensionProfile(extension_profile_);
          rtp_packet.SetExtensionData(extension_data_, extension_len_);
        }

        RtpPacket::FU_INDICATOR fu_indicator;
        fu_indicator.forbidden_bit = 0;
        fu_indicator.nal_reference_idc = 0;
        fu_indicator.nal_unit_type = FU_A;

        RtpPacket::FU_HEADER fu_header;
        fu_header.start = index == 0 ? 1 : 0;
        fu_header.end = index == packet_num - 1 ? 1 : 0;
        fu_header.remain_bit = 0;
        fu_header.nal_unit_type = FU_A;

        rtp_packet.SetFuIndicator(fu_indicator);
        rtp_packet.SetFuHeader(fu_header);

        if (index == packet_num - 1 && last_packet_size > 0) {
          rtp_packet.EncodeH264Fua(buffer + index * MAX_NALU_LEN,
                                   last_packet_size);
        } else {
          rtp_packet.EncodeH264Fua(buffer + index * MAX_NALU_LEN, MAX_NALU_LEN);
        }
        packets.emplace_back(rtp_packet);
      }
    }
  } else if (RtpPacket::PAYLOAD_TYPE::AV1 == payload_type_) {
    std::vector<Obu> obus = ParseObus(buffer, size);
    // LOG_ERROR("Total size = [{}]", size);

    uint32_t timestamp =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();

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
        // LOG_ERROR("enc payload size = {}", rtp_packet.PayloadSize());
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

          // LOG_ERROR("enc payload size = {}", rtp_packet.PayloadSize());
          packets.emplace_back(rtp_packet);
        }
      }
    }
  }
}

size_t RtpCodec::Decode(RtpPacket& packet, uint8_t* payload) {
  // if ((packet.Buffer()[13] >> 6) & 0x01) {
  //   LOG_ERROR("End bit!!!!!!!!!!!!!!!");
  // }

  // if ((packet.Buffer()[13] >> 7) & 0x01) {
  //   LOG_ERROR("Start bit!!!!!!!!!!!!!!!");
  // }

  auto nal_unit_type = packet.Buffer()[12] & 0x1F;

  if (NALU == nal_unit_type) {
    LOG_ERROR("Nalu");
    return packet.DecodeH264Nalu(payload);
  } else if (FU_A == nal_unit_type) {
    LOG_ERROR("Fua");
    return packet.DecodeH264Fua(payload);
  } else {
    LOG_ERROR("Default");
    return packet.DecodeData(payload);
  }
}

bool RtpCodec::IsKeyFrame(const uint8_t* buffer, size_t size) {
  if (buffer != nullptr && size != 0 && (*(buffer + 4) & 0x1f) == 0x07) {
    return true;
  }
  return false;
}