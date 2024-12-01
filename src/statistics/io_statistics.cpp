#include "io_statistics.h"

#include "log.h"

#define RTP_SEQ_NUM_MAX 65535

IOStatistics::IOStatistics(
    std::function<void(const NetTrafficStats&)> io_report_callback)
    : io_report_callback_(io_report_callback) {
  interval_ = 1000;
}

IOStatistics::~IOStatistics() {}

void IOStatistics::Process() {
  while (running_) {
    std::unique_lock<std::mutex> lock(mtx_);
    cond_var_.wait_for(lock, std::chrono::milliseconds(interval_),
                       [this] { return !running_; });

    video_inbound_bitrate_ = video_inbound_bytes_ * 1000 * 8 / interval_;
    video_outbound_bitrate_ = video_outbound_bytes_ * 1000 * 8 / interval_;
    audio_inbound_bitrate_ = audio_inbound_bytes_ * 1000 * 8 / interval_;
    audio_outbound_bitrate_ = audio_outbound_bytes_ * 1000 * 8 / interval_;
    data_inbound_bitrate_ = data_inbound_bytes_ * 1000 * 8 / interval_;
    data_outbound_bitrate_ = data_outbound_bytes_ * 1000 * 8 / interval_;
    total_inbound_bitrate_ =
        video_inbound_bitrate_ + audio_inbound_bitrate_ + data_inbound_bitrate_;
    total_outbound_bitrate_ = video_outbound_bitrate_ +
                              audio_outbound_bitrate_ + data_outbound_bitrate_;

    video_inbound_bytes_ = 0;
    video_outbound_bytes_ = 0;
    audio_inbound_bytes_ = 0;
    audio_outbound_bytes_ = 0;
    data_inbound_bytes_ = 0;
    data_outbound_bytes_ = 0;

    // packet loss rate
    {
      video_rtp_pkt_loss_cnt_ =
          expected_video_inbound_rtp_pkt_cnt_ - video_inbound_rtp_pkt_cnt_tmp_;
      audio_rtp_pkt_loss_cnt_ =
          expected_audio_inbound_rtp_pkt_cnt_ - audio_inbound_rtp_pkt_cnt_tmp_;
      data_rtp_pkt_loss_cnt_ =
          expected_data_inbound_rtp_pkt_cnt_ - data_inbound_rtp_pkt_cnt_tmp_;

      if (expected_video_inbound_rtp_pkt_cnt_ > 0 &&
          video_rtp_pkt_loss_cnt_ >= 0) {
        video_rtp_pkt_loss_rate_ = video_rtp_pkt_loss_cnt_ /
                                   (float)expected_video_inbound_rtp_pkt_cnt_;
      } else {
        video_rtp_pkt_loss_rate_ = 0;
      }

      if (expected_audio_inbound_rtp_pkt_cnt_ > 0 &&
          audio_inbound_rtp_pkt_cnt_tmp_ >= 0) {
        audio_rtp_pkt_loss_rate_ =
            audio_rtp_pkt_loss_cnt_ / (float)audio_inbound_rtp_pkt_cnt_;
      } else {
        audio_rtp_pkt_loss_rate_ = 0;
      }

      if (expected_data_inbound_rtp_pkt_cnt_ > 0 &&
          data_inbound_rtp_pkt_cnt_ >= 0) {
        data_rtp_pkt_loss_rate_ =
            data_rtp_pkt_loss_cnt_ / (float)data_inbound_rtp_pkt_cnt_;
      } else {
        data_rtp_pkt_loss_rate_ = 0;
      }

      expected_video_inbound_rtp_pkt_cnt_ = 0;
      expected_audio_inbound_rtp_pkt_cnt_ = 0;
      expected_data_inbound_rtp_pkt_cnt_ = 0;
      video_inbound_rtp_pkt_cnt_tmp_ = 0;
      audio_inbound_rtp_pkt_cnt_tmp_ = 0;
      data_inbound_rtp_pkt_cnt_tmp_ = 0;
      video_rtp_pkt_loss_cnt_ = 0;
      audio_rtp_pkt_loss_cnt_ = 0;
      data_rtp_pkt_loss_cnt_ = 0;
    }

    if (io_report_callback_) {
      NetTrafficStats net_traffic_stats;
      net_traffic_stats.video_inbound_stats.bitrate = video_inbound_bitrate_;
      net_traffic_stats.video_inbound_stats.rtp_packet_count =
          video_inbound_rtp_pkt_cnt_;
      net_traffic_stats.video_inbound_stats.loss_rate =
          video_rtp_pkt_loss_rate_;

      net_traffic_stats.video_outbound_stats.bitrate = video_outbound_bitrate_;
      net_traffic_stats.video_outbound_stats.rtp_packet_count =
          video_outbound_rtp_pkt_cnt_;

      net_traffic_stats.audio_inbound_stats.bitrate = audio_inbound_bitrate_;
      net_traffic_stats.audio_inbound_stats.rtp_packet_count =
          audio_inbound_rtp_pkt_cnt_;
      net_traffic_stats.audio_inbound_stats.loss_rate =
          audio_rtp_pkt_loss_rate_;

      net_traffic_stats.audio_outbound_stats.bitrate = audio_outbound_bitrate_;
      net_traffic_stats.audio_outbound_stats.rtp_packet_count =
          audio_outbound_rtp_pkt_cnt_;

      net_traffic_stats.data_inbound_stats.bitrate = data_inbound_bitrate_;
      net_traffic_stats.data_inbound_stats.rtp_packet_count =
          data_inbound_rtp_pkt_cnt_;
      net_traffic_stats.data_inbound_stats.loss_rate = data_rtp_pkt_loss_rate_;

      net_traffic_stats.data_outbound_stats.bitrate = data_outbound_bitrate_;
      net_traffic_stats.data_outbound_stats.rtp_packet_count =
          data_outbound_rtp_pkt_cnt_;

      net_traffic_stats.total_inbound_stats.bitrate = total_inbound_bitrate_;
      net_traffic_stats.total_inbound_stats.loss_rate =
          video_rtp_pkt_loss_rate_ + audio_rtp_pkt_loss_rate_ +
          data_rtp_pkt_loss_rate_;
      net_traffic_stats.total_inbound_stats.rtp_packet_count =
          video_inbound_rtp_pkt_cnt_ + audio_inbound_rtp_pkt_cnt_ +
          data_inbound_rtp_pkt_cnt_;

      net_traffic_stats.total_outbound_stats.bitrate = total_outbound_bitrate_;
      net_traffic_stats.total_outbound_stats.rtp_packet_count =
          video_outbound_rtp_pkt_cnt_ + audio_outbound_rtp_pkt_cnt_ +
          data_outbound_rtp_pkt_cnt_;

      io_report_callback_(net_traffic_stats);
    }
  }
}

void IOStatistics::Start() {
  if (!running_) {
    running_ = true;
    statistics_thread_ = std::thread(&IOStatistics::Process, this);
  }
}

void IOStatistics::Stop() {
  running_ = false;
  cond_var_.notify_one();
  if (statistics_thread_.joinable()) {
    statistics_thread_.join();
  }
}

void IOStatistics::UpdateVideoInboundBytes(uint32_t bytes) {
  video_inbound_bytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void IOStatistics::UpdateVideoOutboundBytes(uint32_t bytes) {
  video_outbound_bytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void IOStatistics::UpdateVideoPacketLossCount(uint16_t seq_num) {
  if (last_received_video_rtp_pkt_seq_ != 0) {
    if (last_received_video_rtp_pkt_seq_ < seq_num) {
      if (seq_num - last_received_video_rtp_pkt_seq_ > 0x8000) {
        expected_video_inbound_rtp_pkt_cnt_.fetch_add(
            0xffff - last_received_video_rtp_pkt_seq_ + seq_num + 1,
            std::memory_order_relaxed);
      } else {
        expected_video_inbound_rtp_pkt_cnt_.fetch_add(
            seq_num - last_received_video_rtp_pkt_seq_,
            std::memory_order_relaxed);
      }
    } else if (last_received_video_rtp_pkt_seq_ > seq_num) {
      expected_video_inbound_rtp_pkt_cnt_.fetch_add(
          0xffff - last_received_video_rtp_pkt_seq_ + seq_num + 1,
          std::memory_order_relaxed);
    }
  } else {
    expected_video_inbound_rtp_pkt_cnt_.fetch_add(1, std::memory_order_relaxed);
  }
  last_received_video_rtp_pkt_seq_ = seq_num;
}

void IOStatistics::UpdateAudioInboundBytes(uint32_t bytes) {
  audio_inbound_bytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void IOStatistics::UpdateAudioOutboundBytes(uint32_t bytes) {
  audio_outbound_bytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void IOStatistics::UpdateAudioPacketLossCount(uint16_t seq_num) {
  if (last_received_audio_rtp_pkt_seq_ != 0) {
    if (last_received_audio_rtp_pkt_seq_ < seq_num) {
      if (seq_num - last_received_audio_rtp_pkt_seq_ > 0x8000) {
        expected_audio_inbound_rtp_pkt_cnt_.fetch_add(
            0xffff - last_received_audio_rtp_pkt_seq_ + seq_num + 1,
            std::memory_order_relaxed);
      } else {
        expected_audio_inbound_rtp_pkt_cnt_.fetch_add(
            seq_num - last_received_audio_rtp_pkt_seq_,
            std::memory_order_relaxed);
      }
    } else if (last_received_audio_rtp_pkt_seq_ > seq_num) {
      expected_audio_inbound_rtp_pkt_cnt_.fetch_add(
          0xffff - last_received_audio_rtp_pkt_seq_ + seq_num + 1,
          std::memory_order_relaxed);
    }
  }
  last_received_audio_rtp_pkt_seq_ = seq_num;
}

void IOStatistics::UpdateDataInboundBytes(uint32_t bytes) {
  data_inbound_bytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void IOStatistics::UpdateDataOutboundBytes(uint32_t bytes) {
  data_outbound_bytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void IOStatistics::UpdateDataPacketLossCount(uint16_t seq_num) {
  if (last_received_data_rtp_pkt_seq_ != 0) {
    if (last_received_data_rtp_pkt_seq_ < seq_num) {
      if (seq_num - last_received_data_rtp_pkt_seq_ > 0x8000) {
        expected_data_inbound_rtp_pkt_cnt_.fetch_add(
            0xffff - last_received_data_rtp_pkt_seq_ + seq_num + 1,
            std::memory_order_relaxed);
      } else {
        expected_data_inbound_rtp_pkt_cnt_.fetch_add(
            seq_num - last_received_data_rtp_pkt_seq_,
            std::memory_order_relaxed);
      }
    } else if (last_received_data_rtp_pkt_seq_ > seq_num) {
      expected_data_inbound_rtp_pkt_cnt_.fetch_add(
          0xffff - last_received_data_rtp_pkt_seq_ + seq_num + 1,
          std::memory_order_relaxed);
    }
  }
  last_received_data_rtp_pkt_seq_ = seq_num;
}

void IOStatistics::IncrementVideoInboundRtpPacketCount() {
  ++video_inbound_rtp_pkt_cnt_;
  ++video_inbound_rtp_pkt_cnt_tmp_;
}

void IOStatistics::IncrementVideoOutboundRtpPacketCount() {
  ++video_outbound_rtp_pkt_cnt_;
}

void IOStatistics::IncrementAudioInboundRtpPacketCount() {
  ++audio_inbound_rtp_pkt_cnt_;
  ++audio_inbound_rtp_pkt_cnt_tmp_;
}

void IOStatistics::IncrementAudioOutboundRtpPacketCount() {
  ++audio_outbound_rtp_pkt_cnt_;
}

void IOStatistics::IncrementDataInboundRtpPacketCount() {
  ++data_inbound_rtp_pkt_cnt_;
  ++data_inbound_rtp_pkt_cnt_tmp_;
}

void IOStatistics::IncrementDataOutboundRtpPacketCount() {
  ++data_outbound_rtp_pkt_cnt_;
}