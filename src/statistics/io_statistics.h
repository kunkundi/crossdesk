/*
 * @Author: DI JUNKUN
 * @Date: 2024-09-05
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _STATISTICS_H_
#define _STATISTICS_H_

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

class IOStatistics {
 public:
  IOStatistics(std::function<void(uint32_t, uint32_t, uint32_t, uint32_t,
                                  uint32_t, uint32_t, uint32_t, uint32_t)>
                   io_report_callback);
  ~IOStatistics();

 public:
  void Start();
  void Stop();

  void UpdateVideoInboundBytes(uint32_t bytes);
  void UpdateVideoOutboundBytes(uint32_t bytes);

  void UpdateAudioInboundBytes(uint32_t bytes);
  void UpdateAudioOutboundBytes(uint32_t bytes);

  void UpdateDataInboundBytes(uint32_t bytes);
  void UpdateDataOutboundBytes(uint32_t bytes);

 private:
  void Process();

 private:
  std::function<void(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                     uint32_t, uint32_t)>
      io_report_callback_ = nullptr;
  std::thread statistics_thread_;
  std::mutex mtx_;
  uint32_t interval_ = 1000;
  std::condition_variable cond_var_;
  std::atomic<bool> running_{false};

  std::atomic<uint32_t> video_inbound_bytes_ = 0;
  std::atomic<uint32_t> video_outbound_bytes_ = 0;
  std::atomic<uint32_t> audio_inbound_bytes_ = 0;
  std::atomic<uint32_t> audio_outbound_bytes_ = 0;
  std::atomic<uint32_t> data_inbound_bytes_ = 0;
  std::atomic<uint32_t> data_outbound_bytes_ = 0;
  std::atomic<uint32_t> total_inbound_bytes_ = 0;
  std::atomic<uint32_t> total_outbound_bytes_ = 0;

  std::atomic<uint32_t> video_inbound_bitrate_ = 0;
  std::atomic<uint32_t> video_outbound_bitrate_ = 0;
  std::atomic<uint32_t> audio_inbound_bitrate_ = 0;
  std::atomic<uint32_t> audio_outbound_bitrate_ = 0;
  std::atomic<uint32_t> data_inbound_bitrate_ = 0;
  std::atomic<uint32_t> data_outbound_bitrate_ = 0;
  std::atomic<uint32_t> total_inbound_bitrate_ = 0;
  std::atomic<uint32_t> total_outbound_bitrate_ = 0;
};

#endif