/*
 * @Author: DI JUNKUN
 * @Date: 2025-02-18
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SENDER_REPORT_H_
#define _SENDER_REPORT_H_

// SR
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P|   RC    |   PT=SR=200   |            length             |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                        SSRC of sender                         |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |              NTP timestamp, most significant word             | sender
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ info
// |             NTP timestamp, least significant word             |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                         RTP timestamp                         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                     sender's packet count                     |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                      sender's octet count                     |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |                 SSRC_1 (SSRC of first source)                 | report
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ block
// | fraction lost |       cumulative number of packets lost       | 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           extended highest sequence number received           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                      interarrival jitter                      |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                         last SR (LSR)                         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                   delay since last SR (DLSR)                  |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |                 SSRC_2 (SSRC of second source)                | report
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ block
// :                               ...                             : 2
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

#include <vector>

#include "api/ntp/ntp_time.h"
#include "rtcp_common_header.h"
#include "rtcp_report_block.h"

class SenderReport {
 public:
  typedef struct {
    uint32_t sender_ssrc : 32;
    uint32_t ntp_ts_msw : 32;
    uint32_t ntp_ts_lsw : 32;
    uint32_t rtp_ts : 32;
    uint32_t sender_packet_count : 32;
    uint32_t sender_octet_count : 32;
  } SenderInfo;

 public:
  SenderReport();
  ~SenderReport();

 public:
  void SetSenderSsrc(uint32_t ssrc) { sender_info_.sender_ssrc = ssrc; }
  void SetNtpTimestamp(uint64_t ntp_timestamp) {
    sender_info_.ntp_ts_msw =
        ntp_timestamp / webrtc::NtpTime::kFractionsPerSecond;
    sender_info_.ntp_ts_lsw =
        ntp_timestamp % webrtc::NtpTime::kFractionsPerSecond;
  }
  void SetTimestamp(uint32_t timestamp) { sender_info_.rtp_ts = timestamp; }
  void SetSenderPacketCount(uint32_t packet_count) {
    sender_info_.sender_packet_count = packet_count;
  }
  void SetSenderOctetCount(uint32_t octet_count) {
    sender_info_.sender_octet_count = octet_count;
  }
  void SetReportBlock(RtcpReportBlock &rtcp_report_block);
  void SetReportBlocks(std::vector<RtcpReportBlock> &rtcp_report_blocks);

  uint32_t SenderSsrc() const { return sender_info_.sender_ssrc; }
  uint32_t NtpTimestamp() const {
    return (sender_info_.ntp_ts_msw << 16) | sender_info_.ntp_ts_lsw >> 16;
  }
  uint32_t Timestamp() const { return sender_info_.rtp_ts; }
  uint32_t SenderPacketCount() const {
    return sender_info_.sender_packet_count;
  }
  uint32_t SenderOctetCount() const { return sender_info_.sender_octet_count; }

 public:
  const uint8_t *Build();
  bool Parse(const RtcpCommonHeader &packet);

  // Entire RTP buffer
  const uint8_t *Buffer() const { return buffer_; }
  size_t Size() const { return size_; }

 private:
  RtcpCommonHeader rtcp_common_header_;
  SenderInfo sender_info_;
  std::vector<RtcpReportBlock> reports_;

  // Entire RTCP buffer
  uint8_t *buffer_;
  size_t size_;
};

#endif