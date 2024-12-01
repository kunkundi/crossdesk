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
  typedef struct {
    uint32_t bitrate;
    uint32_t rtp_packet_count;
    float loss_rate;
  } InboundStats;

  typedef struct {
    uint32_t bitrate;
    uint32_t rtp_packet_count;
  } OutboundStats;

  typedef struct {
    InboundStats video_inbound_stats;
    OutboundStats video_outbound_stats;
    InboundStats audio_inbound_stats;
    OutboundStats audio_outbound_stats;
    InboundStats data_inbound_stats;
    OutboundStats data_outbound_stats;
    InboundStats total_inbound_stats;
    OutboundStats total_outbound_stats;
  } NetTrafficStats;

 public:
  IOStatistics(std::function<void(const NetTrafficStats&)> io_report_callback);
  ~IOStatistics();

 public:
  void Start();
  void Stop();

  void UpdateVideoInboundBytes(uint32_t bytes);
  void UpdateVideoOutboundBytes(uint32_t bytes);
  void UpdateVideoPacketLossCount(uint16_t seq_num);

  void UpdateAudioInboundBytes(uint32_t bytes);
  void UpdateAudioOutboundBytes(uint32_t bytes);
  void UpdateAudioPacketLossCount(uint16_t seq_num);

  void UpdateDataInboundBytes(uint32_t bytes);
  void UpdateDataOutboundBytes(uint32_t bytes);
  void UpdateDataPacketLossCount(uint16_t seq_num);

  void IncrementVideoInboundRtpPacketCount();
  void IncrementVideoOutboundRtpPacketCount();

  void IncrementAudioInboundRtpPacketCount();
  void IncrementAudioOutboundRtpPacketCount();

  void IncrementDataInboundRtpPacketCount();
  void IncrementDataOutboundRtpPacketCount();

 private:
  void Process();

 private:
  std::function<void(const NetTrafficStats&)> io_report_callback_ = nullptr;
  std::thread statistics_thread_;
  std::mutex mtx_;
  uint32_t interval_ = 1000;
  std::condition_variable cond_var_;
  std::atomic<bool> running_{false};

  std::atomic<uint32_t> video_inbound_bytes_ = 0;
  std::atomic<uint32_t> video_outbound_bytes_ = 0;
  uint16_t last_received_video_rtp_pkt_seq_ = 0;
  std::atomic<uint32_t> audio_inbound_bytes_ = 0;
  std::atomic<uint32_t> audio_outbound_bytes_ = 0;
  uint16_t last_received_audio_rtp_pkt_seq_ = 0;
  std::atomic<uint32_t> data_inbound_bytes_ = 0;
  std::atomic<uint32_t> data_outbound_bytes_ = 0;
  uint16_t last_received_data_rtp_pkt_seq_ = 0;
  std::atomic<uint32_t> total_inbound_bytes_ = 0;
  std::atomic<uint32_t> total_outbound_bytes_ = 0;

  std::atomic<uint32_t> video_inbound_rtp_pkt_cnt_ = 0;
  std::atomic<uint32_t> audio_inbound_rtp_pkt_cnt_ = 0;
  std::atomic<uint32_t> data_inbound_rtp_pkt_cnt_ = 0;

  std::atomic<uint32_t> video_inbound_rtp_pkt_cnt_tmp_ = 0;
  std::atomic<uint32_t> audio_inbound_rtp_pkt_cnt_tmp_ = 0;
  std::atomic<uint32_t> data_inbound_rtp_pkt_cnt_tmp_ = 0;

  std::atomic<uint32_t> data_outbound_rtp_pkt_cnt_ = 0;
  std::atomic<uint32_t> video_outbound_rtp_pkt_cnt_ = 0;
  std::atomic<uint32_t> audio_outbound_rtp_pkt_cnt_ = 0;

  std::atomic<uint32_t> expected_video_inbound_rtp_pkt_cnt_ = 0;
  std::atomic<uint32_t> expected_audio_inbound_rtp_pkt_cnt_ = 0;
  std::atomic<uint32_t> expected_data_inbound_rtp_pkt_cnt_ = 0;
  int32_t video_rtp_pkt_loss_cnt_ = 0;
  int32_t audio_rtp_pkt_loss_cnt_ = 0;
  int32_t data_rtp_pkt_loss_cnt_ = 0;

  uint32_t video_inbound_bitrate_ = 0;
  uint32_t video_outbound_bitrate_ = 0;
  uint32_t audio_inbound_bitrate_ = 0;
  uint32_t audio_outbound_bitrate_ = 0;
  uint32_t data_inbound_bitrate_ = 0;
  uint32_t data_outbound_bitrate_ = 0;
  uint32_t total_inbound_bitrate_ = 0;
  uint32_t total_outbound_bitrate_ = 0;

  float video_rtp_pkt_loss_rate_ = 0;
  float audio_rtp_pkt_loss_rate_ = 0;
  float data_rtp_pkt_loss_rate_ = 0;
};

#endif