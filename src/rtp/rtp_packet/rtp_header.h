/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-16
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RTP_HEADER_H_
#define _RTP_HEADER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "api/units/timestamp.h"

static constexpr int kAbsSendTimeFraction = 18;

enum { kRtpCsrcSize = 15 };  // RFC 3550 page 13

struct RTPHeader {
  RTPHeader()
      : markerBit(false),
        payloadType(0),
        sequenceNumber(0),
        timestamp(0),
        ssrc(0),
        numCSRCs(0),
        arrOfCSRCs(),
        paddingLength(0),
        headerLength(0){};
  RTPHeader(const RTPHeader& other) = default;
  RTPHeader& operator=(const RTPHeader& other) = default;

  bool markerBit;
  uint8_t payloadType;
  uint16_t sequenceNumber;
  uint32_t timestamp;
  uint32_t ssrc;
  uint8_t numCSRCs;
  uint32_t arrOfCSRCs[kRtpCsrcSize];
  size_t paddingLength;
  size_t headerLength;
};

#endif