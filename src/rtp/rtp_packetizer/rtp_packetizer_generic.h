/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-23
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RTP_PACKETIZER_GENERIC_H_
#define _RTP_PACKETIZER_GENERIC_H_

#include "rtp_packetizer.h"

class RtpPacketizerGeneric : public RtpPacketizer {
 public:
  RtpPacketizerGeneric(uint32_t ssrc);

  virtual ~RtpPacketizerGeneric();

  std::vector<std::unique_ptr<RtpPacket>> Build(
      uint8_t* payload, uint32_t payload_size, uint32_t rtp_timestamp,
      bool use_rtp_packet_to_send) override;

  std::vector<std::unique_ptr<RtpPacket>> BuildPadding(
      uint32_t payload_size, uint32_t rtp_timestamp,
      bool use_rtp_packet_to_send) override {
    return std::vector<std::unique_ptr<RtpPacket>>{};
  };

 private:
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
 private:
  std::vector<uint8_t> rtp_packet_frame_;
};

#endif