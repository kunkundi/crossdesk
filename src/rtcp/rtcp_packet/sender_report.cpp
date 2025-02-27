#include "sender_report.h"

SenderReport::SenderReport() : buffer_(nullptr), size_(0) {}

SenderReport::~SenderReport() {
  if (buffer_) {
    delete[] buffer_;
    buffer_ = nullptr;
  }

  size_ = 0;
}

void SenderReport::SetReportBlock(RtcpReportBlock &rtcp_report_block) {
  reports_.push_back(std::move(rtcp_report_block));
}

void SenderReport::SetReportBlocks(
    std::vector<RtcpReportBlock> &rtcp_report_blocks) {
  reports_ = std::move(rtcp_report_blocks);
}

const uint8_t *SenderReport::Build() {
  size_t buffer_size =
      DEFAULT_SR_SIZE + reports_.size() * RtcpReportBlock::kLength;
  if (!buffer_ || buffer_size != size_) {
    delete[] buffer_;
    buffer_ = nullptr;
  }

  buffer_ = new uint8_t[buffer_size];
  size_ = buffer_size;

  int pos =
      rtcp_common_header_.Create(DEFAULT_RTCP_VERSION, 0, DEFAULT_SR_BLOCK_NUM,
                                 RTCP_TYPE::SR, buffer_size, buffer_);

  buffer_[pos++] = sender_info_.sender_ssrc >> 24 & 0xFF;
  buffer_[pos++] = sender_info_.sender_ssrc >> 16 & 0xFF;
  buffer_[pos++] = sender_info_.sender_ssrc >> 8 & 0xFF;
  buffer_[pos++] = sender_info_.sender_ssrc & 0xFF;

  buffer_[pos++] = sender_info_.ntp_ts_msw >> 24 & 0xFF;
  buffer_[pos++] = sender_info_.ntp_ts_msw >> 16 & 0xFF;
  buffer_[pos++] = sender_info_.ntp_ts_msw >> 8 & 0xFF;
  buffer_[pos++] = sender_info_.ntp_ts_msw & 0xFF;
  buffer_[pos++] = sender_info_.ntp_ts_lsw >> 24 & 0xFF;
  buffer_[pos++] = sender_info_.ntp_ts_lsw >> 16 & 0xFF;
  buffer_[pos++] = sender_info_.ntp_ts_lsw >> 8 & 0xFF;
  buffer_[pos++] = sender_info_.ntp_ts_lsw & 0xFF;

  buffer_[pos++] = sender_info_.rtp_ts >> 24 & 0xFF;
  buffer_[pos++] = sender_info_.rtp_ts >> 16 & 0xFF;
  buffer_[pos++] = sender_info_.rtp_ts >> 8 & 0xFF;
  buffer_[pos++] = sender_info_.rtp_ts & 0xFF;

  buffer_[pos++] = sender_info_.sender_packet_count >> 24 & 0xFF;
  buffer_[pos++] = sender_info_.sender_packet_count >> 16 & 0xFF;
  buffer_[pos++] = sender_info_.sender_packet_count >> 8 & 0xFF;
  buffer_[pos++] = sender_info_.sender_packet_count & 0xFF;

  buffer_[pos++] = sender_info_.sender_octet_count >> 24 & 0xFF;
  buffer_[pos++] = sender_info_.sender_octet_count >> 16 & 0xFF;
  buffer_[pos++] = sender_info_.sender_octet_count >> 8 & 0xFF;
  buffer_[pos++] = sender_info_.sender_octet_count & 0xFF;

  for (auto &report : reports_) {
    pos += report.Create(buffer_ + pos);
  }

  return buffer_;
}

bool SenderReport::Parse(const RtcpCommonHeader &packet) {
  reports_.clear();
  const uint8_t *payload = packet.payload();
  const uint8_t *payload_end = packet.payload() + packet.payload_size_bytes();
  size_t pos = 0;

  sender_info_.sender_ssrc = (payload[pos] << 24) + (payload[pos + 1] << 16) +
                             (payload[pos + 2] << 8) + payload[pos + 3];
  pos += 4;

  if (pos > packet.payload_size_bytes()) {
    return false;
  }
  sender_info_.ntp_ts_msw = (payload[pos] << 24) + (payload[pos + 1] << 16) +
                            (payload[pos + 2] << 8) + payload[pos + 3];
  pos += 4;
  if (pos > packet.payload_size_bytes()) {
    return false;
  }
  sender_info_.ntp_ts_lsw = (payload[pos] << 24) + (payload[pos + 1] << 16) +
                            (payload[pos + 2] << 8) + payload[pos + 3];
  pos += 4;
  if (pos > packet.payload_size_bytes()) {
    return false;
  }
  sender_info_.rtp_ts = (payload[pos] << 24) + (payload[pos + 1] << 16) +
                        (payload[pos + 2] << 8) + payload[pos + 3];
  pos += 4;
  if (pos > packet.payload_size_bytes()) {
    return false;
  }
  sender_info_.sender_packet_count = (payload[pos] << 24) +
                                     (payload[pos + 1] << 16) +
                                     (payload[pos + 2] << 8) + payload[pos + 3];
  pos += 4;
  if (pos > packet.payload_size_bytes()) {
    return false;
  }
  sender_info_.sender_octet_count = (payload[pos] << 24) +
                                    (payload[pos + 1] << 16) +
                                    (payload[pos + 2] << 8) + payload[pos + 3];

  pos += 4;
  if (pos > packet.payload_size_bytes()) {
    return false;
  }

  for (int i = 0; i < rtcp_common_header_.fmt(); i++) {
    RtcpReportBlock report;
    pos += report.Parse(payload + pos);
    reports_.emplace_back(std::move(report));
  }

  return pos == packet.payload_size_bytes();
}