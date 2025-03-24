#include "rtp_statistics.h"

#include "log.h"

RtpStatistics::RtpStatistics() {
  SetPeriod(std::chrono::milliseconds(1000));
  SetThreadName("RtpStatistics");
}

RtpStatistics::~RtpStatistics() {}

void RtpStatistics::UpdateSentBytes(uint32_t sent_bytes) {
  sent_bytes_ += sent_bytes;
}

void RtpStatistics::UpdateReceiveBytes(uint32_t received_bytes) {
  received_bytes_ += received_bytes;
}

void RtpStatistics::UpdatePacketLossRate(uint16_t seq_num) {
  if (last_received_seq_num_ != 0) {
    if (last_received_seq_num_ < seq_num) {
      // seq wrap
      if (seq_num - last_received_seq_num_ > 0x8000) {
        lost_packets_num_ += 0xffff - last_received_seq_num_ + seq_num + 1;
      } else {
        lost_packets_num_ += seq_num - last_received_seq_num_ - 1;
      }
    } else if (last_received_seq_num_ > seq_num) {
      lost_packets_num_ += 0xffff - last_received_seq_num_ + seq_num + 1;
    }
  }
  last_received_seq_num_ = seq_num;
}

bool RtpStatistics::Process() {
  if (!sent_bytes_) {
    // LOG_INFO("rtp statistics: Send [{} bps]", sent_bytes_);
  }

  if (!received_bytes_) {
    // LOG_INFO("rtp statistics: Receive [{} bps]", received_bytes_);
  }

  sent_bytes_ = 0;
  received_bytes_ = 0;

  std::this_thread::sleep_for(std::chrono::seconds(1));
  return true;
}