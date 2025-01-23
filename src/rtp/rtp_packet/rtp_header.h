/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-16
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RTP_HEADER_H_
#define _RTP_HEADER_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

static constexpr int kMaxRtpCsrcSize = 15;
static constexpr uint8_t kRtpVersion = 2;

struct RTPHeaderExtension {
  RTPHeaderExtension();
  RTPHeaderExtension(const RTPHeaderExtension& other);
  RTPHeaderExtension& operator=(const RTPHeaderExtension& other);

  static constexpr int kAbsSendTimeFraction = 18;

  bool hasAbsoluteSendTime;
  uint32_t absoluteSendTime;
};

struct RTPHeader {
  RTPHeader()
      : version_(kRtpVersion),
        has_padding_(false),
        has_extension_(false),
        csrc_count_(0),
        marker_(false),
        payload_type_(0),
        sequence_number_(1),
        timestamp_(0),
        ssrc_(0),
        csrcs_(),
        padding_len(0),
        header_len(0){};

  RTPHeader(const RTPHeader& other) = default;
  RTPHeader& operator=(const RTPHeader& other) = default;

  uint8_t version_ = 0;
  bool has_padding_ = false;
  bool has_extension_ = false;
  uint8_t csrc_count_ = 0;
  bool marker_ = false;
  uint8_t payload_type_ = 0;
  uint16_t sequence_number_ = 1;
  uint64_t timestamp_ = 0;
  uint32_t ssrc_ = 0;
  uint32_t csrcs_[kMaxRtpCsrcSize];
  size_t padding_len;
  size_t header_len;

  RTPHeaderExtension extension;
};

#endif