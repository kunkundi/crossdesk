/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-22
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RTP_PACKETIZER_H_
#define _RTP_PACKETIZER_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "rtp_packet.h"
#include "rtp_packet_to_send.h"

class RtpPacketizer {
 public:
  static std::unique_ptr<RtpPacketizer> Create(uint32_t payload_type,
                                               uint32_t ssrc);

  virtual ~RtpPacketizer() = default;

  virtual std::vector<std::unique_ptr<RtpPacket>> Build(
      uint8_t* payload, uint32_t payload_size, uint32_t rtp_timestamp,
      bool use_rtp_packet_to_send) = 0;

  virtual std::vector<std::unique_ptr<RtpPacket>> BuildPadding(
      uint32_t payload_size, uint32_t rtp_timestamp,
      bool use_rtp_packet_to_send) = 0;
};

#endif