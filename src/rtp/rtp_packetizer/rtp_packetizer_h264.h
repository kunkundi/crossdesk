/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-22
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RTP_PACKETIZER_H264_H_
#define _RTP_PACKETIZER_H264_H_

#include "rtp_packetizer.h"

class RtpPacketizerH264 : public RtpPacketizer {
 public:
  RtpPacketizerH264(uint32_t ssrc);

  virtual ~RtpPacketizerH264();

  std::vector<RtpPacket> Build(uint8_t* payload,
                               uint32_t payload_size) override;

  std::vector<RtpPacket> BuildNalu(uint8_t* payload, uint32_t payload_size);

  std::vector<RtpPacket> BuildFua(uint8_t* payload, uint32_t payload_size);

 private:
  bool EncodeH264Fua(RtpPacket& rtp_packet, uint8_t* payload,
                     size_t payload_size);
  void AddAbsSendTimeExtension(std::vector<uint8_t>& rtp_packet_frame);

 private:
  uint8_t version_;
  bool has_padding_;
  bool has_extension_;
  uint32_t csrc_count_;
  bool marker_;
  uint32_t payload_type_;
  uint16_t sequence_number_;
  uint64_t timestamp_;
  uint32_t ssrc_;
  std::vector<uint32_t> csrcs_;
  uint16_t profile_;
  uint16_t extension_profile_;
  uint16_t extension_len_;
  uint8_t* extension_data_;

 private:
  // H.264 header
  rtp::FU_INDICATOR fu_indicator_;
  rtp::FU_HEADER fu_header_;
  uint8_t fec_symbol_id_ = 0;
  uint8_t fec_source_symbol_num_ = 0;
  uint8_t av1_aggr_header_ = 0;

 private:
  std::vector<uint8_t> rtp_packet_frame_;
};

#endif