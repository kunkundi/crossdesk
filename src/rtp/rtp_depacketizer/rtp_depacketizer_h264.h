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

class RtpDepacketizerH264 {
 public:
  RtpDepacketizerH264();

  virtual ~RtpDepacketizerH264() = default;

  bool Parse(uint8_t* payload, uint32_t payload_size);
};

#endif