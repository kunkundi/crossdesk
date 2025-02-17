/*
 * @Author: DI JUNKUN
 * @Date: 2025-02-17
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _REPORT_BLOCK_H_
#define _REPORT_BLOCK_H_

#include <stddef.h>
#include <stdint.h>

// A ReportBlock represents the Sender Report packet from
// RFC 3550 section 6.4.1.
class ReportBlock {
 public:
  static constexpr size_t kLength = 24;

  ReportBlock();
  ~ReportBlock() {}

  bool Parse(const uint8_t* buffer, size_t length);

  // Fills buffer with the ReportBlock.
  // Consumes ReportBlock::kLength bytes.
  void Create(uint8_t* buffer) const;

  void SetMediaSsrc(uint32_t ssrc) { source_ssrc_ = ssrc; }
  void SetFractionLost(uint8_t fraction_lost) {
    fraction_lost_ = fraction_lost;
  }
  bool SetCumulativeLost(int32_t cumulative_lost);
  void SetExtHighestSeqNum(uint32_t ext_highest_seq_num) {
    extended_high_seq_num_ = ext_highest_seq_num;
  }
  void SetJitter(uint32_t jitter) { jitter_ = jitter; }
  void SetLastSr(uint32_t last_sr) { last_sr_ = last_sr; }
  void SetDelayLastSr(uint32_t delay_last_sr) {
    delay_since_last_sr_ = delay_last_sr;
  }

  uint32_t source_ssrc() const { return source_ssrc_; }
  uint8_t fraction_lost() const { return fraction_lost_; }
  int32_t cumulative_lost() const { return cumulative_lost_; }
  uint32_t extended_high_seq_num() const { return extended_high_seq_num_; }
  uint32_t jitter() const { return jitter_; }
  uint32_t last_sr() const { return last_sr_; }
  uint32_t delay_since_last_sr() const { return delay_since_last_sr_; }

 private:
  uint32_t source_ssrc_;     // 32 bits
  uint8_t fraction_lost_;    // 8 bits representing a fixed point value 0..1
  int32_t cumulative_lost_;  // Signed 24-bit value
  uint32_t extended_high_seq_num_;  // 32 bits
  uint32_t jitter_;                 // 32 bits
  uint32_t last_sr_;                // 32 bits
  uint32_t delay_since_last_sr_;    // 32 bits, units of 1/65536 seconds
};

#endif