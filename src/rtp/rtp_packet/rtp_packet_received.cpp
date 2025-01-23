/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtp_packet_received.h"

#include <stddef.h>

#include <cstdint>
#include <vector>

#include "rtc_base/numerics/safe_conversions.h"

namespace webrtc {

RtpPacketReceived::RtpPacketReceived() = default;
RtpPacketReceived::RtpPacketReceived(webrtc::Timestamp arrival_time)
    : RtpPacket(), arrival_time_(arrival_time) {}
RtpPacketReceived::RtpPacketReceived(const RtpPacketReceived& packet) = default;
RtpPacketReceived::RtpPacketReceived(RtpPacketReceived&& packet) = default;

RtpPacketReceived& RtpPacketReceived::operator=(
    const RtpPacketReceived& packet) = default;
RtpPacketReceived& RtpPacketReceived::operator=(RtpPacketReceived&& packet) =
    default;

RtpPacketReceived::~RtpPacketReceived() {}

void RtpPacketReceived::GetHeader(RTPHeader* header) const {
  header->version_ = Version();
  header->has_padding_ = HasPadding();
  header->has_extension_ = HasExtension();
  header->csrc_count_ = Csrcs().size();
  header->marker_ = Marker();
  header->payload_type_ = PayloadType();
  header->sequence_number_ = SequenceNumber();
  header->timestamp_ = Timestamp();
  header->ssrc_ = Ssrc();
  std::vector<uint32_t> csrcs = Csrcs();
  for (size_t i = 0; i < csrcs.size(); ++i) {
    header->csrcs_[i] = csrcs[i];
  }
  header->padding_len = padding_size();
  header->header_len = headers_size();
}

}  // namespace webrtc
