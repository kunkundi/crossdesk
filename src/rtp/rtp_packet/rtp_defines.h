/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-23
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RTP_DEFINES_H_
#define _RTP_DEFINES_H_

#include <cstddef>
#include <cstdint>

#define DEFAULT_MTU 1500
#define MAX_NALU_LEN 1400

namespace rtp {

typedef enum {
  UNDEFINED = 0,
  H264 = 96,
  H264_FEC_SOURCE = 97,
  H264_FEC_REPAIR = 98,
  AV1 = 99,
  OPUS = 111,
  DATA = 127
} PAYLOAD_TYPE;

typedef struct {
  uint8_t forbidden_bit : 1;
  uint8_t nal_reference_idc : 2;
  uint8_t nal_unit_type : 5;
} FU_INDICATOR;

typedef struct {
  uint8_t start : 1;
  uint8_t end : 1;
  uint8_t remain_bit : 1;
  uint8_t nal_unit_type : 5;
} FU_HEADER;

typedef enum { UNKNOWN = 0, NALU = 1, FU_A = 28, FU_B = 29 } NAL_UNIT_TYPE;

const int kVideoPayloadTypeFrequency = 90000;

static int kMsToRtpTimestamp = 90;
}  // namespace rtp
#endif