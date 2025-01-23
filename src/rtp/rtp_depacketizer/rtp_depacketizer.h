/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-23
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RTP_DEPACKETIZER_H_
#define _RTP_DEPACKETIZER_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "rtp_packet.h"

class RtpDepacketizer {
 public:
  static std::shared_ptr<RtpDepacketizer> Create(uint32_t payload_type);

  virtual ~RtpDepacketizer() = default;

  bool Build(uint8_t* payload, uint32_t payload_size) = 0;
};

#endif