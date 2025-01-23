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

class RtpPacketizer {
 public:
  static std::unique_ptr<RtpPacketizer> Create(uint32_t payload_type);

  virtual ~RtpPacketizer() = default;

  virtual std::vector<RtpPacket> Build(uint8_t* payload,
                                       uint32_t payload_size) = 0;
};

#endif