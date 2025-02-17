/*
 * @Author: DI JUNKUN
 * @Date: 2025-02-17
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RECEIVER_REPORT_H_
#define _RECEIVER_REPORT_H_

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "report_block.h"
#include "rtcp_packet.h"

class CommonHeader;

class ReceiverReport : public RtcpPacket {
 public:
  static constexpr uint8_t kPacketType = 201;
  static constexpr size_t kMaxNumberOfReportBlocks = 0x1f;

  ReceiverReport();
  ReceiverReport(const ReceiverReport&);
  ~ReceiverReport() override;

  // Parse assumes header is already parsed and validated.
  bool Parse(const CommonHeader& packet);

  bool AddReportBlock(const ReportBlock& block);
  bool SetReportBlocks(std::vector<ReportBlock> blocks);

  const std::vector<ReportBlock>& report_blocks() const {
    return report_blocks_;
  }

  size_t BlockLength() const override;

  bool Create(uint8_t* packet, size_t* index, size_t max_length,
              PacketReadyCallback callback) const override;

 private:
  static constexpr size_t kRrBaseLength = 4;

  std::vector<ReportBlock> report_blocks_;
};

#endif