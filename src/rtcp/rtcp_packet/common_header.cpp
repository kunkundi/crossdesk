/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "common_header.h"

#include "byte_io.h"
#include "log.h"

//    0                   1           1       2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 0 |V=2|P|   C/F   |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 1                 |  Packet Type  |
//   ----------------+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 2                                 |             length            |
//   --------------------------------+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// Common header for all RTCP packets, 4 octets.
bool CommonHeader::Parse(const uint8_t* buffer, size_t size_bytes) {
  const uint8_t kVersion = 2;

  if (size_bytes < kHeaderSizeBytes) {
    LOG_WARN(
        "Too little data ({} byte{}) remaining in buffer to parse RTCP header "
        "(4 bytes).",
        size_bytes, (size_bytes != 1 ? "s" : ""));
    return false;
  }

  uint8_t version = buffer[0] >> 6;
  if (version != kVersion) {
    LOG_WARN("Invalid RTCP header: Version must be {} but was {}",
             static_cast<int>(kVersion), static_cast<int>(version));
    return false;
  }

  bool has_padding = (buffer[0] & 0x20) != 0;
  count_or_format_ = buffer[0] & 0x1F;
  packet_type_ = buffer[1];
  payload_size_ = ByteReader<uint16_t>::ReadBigEndian(&buffer[2]) * 4;
  payload_ = buffer + kHeaderSizeBytes;
  padding_size_ = 0;

  if (size_bytes < kHeaderSizeBytes + payload_size_) {
    LOG_WARN(
        "Buffer too small ({} bytes) to fit an RtcpPacket with a header and {} "
        "bytes.",
        size_bytes, payload_size_);
    return false;
  }

  if (has_padding) {
    if (payload_size_ == 0) {
      LOG_WARN(
          "Invalid RTCP header: Padding bit set but 0 payload size specified.");
      return false;
    }

    padding_size_ = payload_[payload_size_ - 1];
    if (padding_size_ == 0) {
      LOG_WARN(
          "Invalid RTCP header: Padding bit set but 0 padding size specified.");
      return false;
    }
    if (padding_size_ > payload_size_) {
      LOG_WARN(
          "Invalid RTCP header: Too many padding bytes ({}) for a packet "
          "payload size of {} bytes.",
          padding_size_, payload_size_);
      return false;
    }
    payload_size_ -= padding_size_;
  }
  return true;
}
