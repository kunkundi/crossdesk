#include "io_statistics.h"

IOStatistics::IOStatistics(
    std::function<void(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                       uint32_t, uint32_t, uint32_t)>
        io_report_callback)
    : io_report_callback_(io_report_callback) {
  interval_ = 1000;
}

IOStatistics::~IOStatistics() {
  running_ = false;
  cond_var_.notify_one();
  if (statistics_thread_.joinable()) {
    statistics_thread_.join();
  }
}

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

    if (io_report_callback_) {
      io_report_callback_(video_inbound_bitrate_, video_outbound_bitrate_,
                          audio_inbound_bitrate_, audio_outbound_bitrate_,
                          data_inbound_bitrate_, data_outbound_bitrate_,
                          total_inbound_bitrate_, total_outbound_bitrate_);
    }
  }
}

void IOStatistics::Start() {
  if (!running_) {
    running_ = true;
    statistics_thread_ = std::thread(&IOStatistics::Process, this);
  }
}

void IOStatistics::UpdateVideoInboundBytes(uint32_t bytes) {
  video_inbound_bytes_ += bytes;
}

void IOStatistics::UpdateVideoOutboundBytes(uint32_t bytes) {
  video_outbound_bytes_ += bytes;
}

void IOStatistics::UpdateAudioInboundBytes(uint32_t bytes) {
  audio_inbound_bytes_ += bytes;
}

void IOStatistics::UpdateAudioOutboundBytes(uint32_t bytes) {
  audio_outbound_bytes_ += bytes;
}

void IOStatistics::UpdateDataInboundBytes(uint32_t bytes) {
  data_inbound_bytes_ += bytes;
}

void IOStatistics::UpdateDataOutboundBytes(uint32_t bytes) {
  data_outbound_bytes_ += bytes;
}