/*
 * @Author: DI JUNKUN
 * @Date: 2025-03-12
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PACKET_SENDER_IMP_H_
#define _PACKET_SENDER_IMP_H_

#include <memory>

#include "api/array_view.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "ice_agent.h"
#include "log.h"
#include "pacing_controller.h"
#include "packet_sender.h"
#include "rtc_base/numerics/exp_filter.h"
#include "rtp_packet_pacer.h"
#include "rtp_packet_to_send.h"
#include "task_queue.h"

class PacketSenderImp : public PacketSender,
                        public webrtc::RtpPacketPacer,
                        public webrtc::PacingController::PacketSender {
 public:
  static const int kNoPacketHoldback;

  PacketSenderImp(std::shared_ptr<IceAgent> ice_agent,
                  std::shared_ptr<webrtc::Clock> clock);
  ~PacketSenderImp();

 public:
  int Send() { return 0; }

  int EnqueueRtpPacket(std::vector<std::unique_ptr<RtpPacket>>& rtp_packets,
                       int64_t captured_timestamp_us);

  void SetOnSentPacketFunc(
      std::function<void(const webrtc::RtpPacketToSend&)> on_sent_packet_func) {
    on_sent_packet_func_ = on_sent_packet_func;
  }

  void SetGeneratePaddingFunc(
      std::function<std::vector<std::unique_ptr<RtpPacket>>(uint32_t, int64_t)>
          generat_padding_func) {
    generat_padding_func_ = generat_padding_func;
  }

 public:
  void SendPacket(std::unique_ptr<webrtc::RtpPacketToSend> packet,
                  const webrtc::PacedPacketInfo& cluster_info) override {
    if (on_sent_packet_func_) {
      if (ssrc_seq_.find(packet->Ssrc()) == ssrc_seq_.end()) {
        ssrc_seq_[packet->Ssrc()] = 1;
      }

      packet->UpdateSequenceNumber(ssrc_seq_[packet->Ssrc()]++);

      on_sent_packet_func_(*packet);
    }
  }
  // Should be called after each call to SendPacket().
  std::vector<std::unique_ptr<webrtc::RtpPacketToSend>> FetchFec() override {
    std::vector<std::unique_ptr<webrtc::RtpPacketToSend>> fec_packets;
    return fec_packets;
  }
  std::vector<std::unique_ptr<webrtc::RtpPacketToSend>> GeneratePadding(
      webrtc::DataSize size) override;

  // TODO(bugs.webrtc.org/1439830): Make pure once subclasses adapt.
  void OnBatchComplete() override {}

  // TODO(bugs.webrtc.org/11340): Make pure  once downstream projects
  // have been updated.
  void OnAbortedRetransmissions(
      uint32_t /* ssrc */,
      rtc::ArrayView<const uint16_t> /* sequence_numbers */) {}
  std::optional<uint32_t> GetRtxSsrcForMedia(
      uint32_t /* ssrc */) const override {
    return std::nullopt;
  }

 public:
  void SetSendBurstInterval(webrtc::TimeDelta burst_interval);

  // A probe may be sent without first waing for a media packet.
  void SetAllowProbeWithoutMediaPacket(bool allow);

  // Ensure that necessary delayed tasks are scheduled.
  void EnsureStarted();

  // Methods implementing RtpPacketSender.

  // Adds the packet to the queue and calls
  // PacingController::PacketSenderImp::SendPacket() when it's time to send.
  void EnqueuePackets(
      std::vector<std::unique_ptr<webrtc::RtpPacketToSend>> packets);
  // Remove any pending packets matching this SSRC from the packet queue.
  void RemovePacketsForSsrc(uint32_t ssrc);

  void CreateProbeClusters(
      std::vector<webrtc::ProbeClusterConfig> probe_cluster_configs) override;

  // Temporarily pause all sending.
  void Pause() override;

  // Resume sending packets.
  void Resume() override;

  void SetCongested(bool congested) override;

  // Sets the pacing rates. Must be called once before packets can be sent.
  void SetPacingRates(webrtc::DataRate pacing_rate,
                      webrtc::DataRate padding_rate) override;

  // Currently audio traffic is not accounted for by pacer and passed through.
  // With the introduction of audio BWE, audio traffic will be accounted for
  // in the pacer budget calculation. The audio traffic will still be injected
  // at high priority.
  void SetAccountForAudioPackets(bool account_for_audio) override;

  void SetIncludeOverhead() override;
  void SetTransportOverhead(webrtc::DataSize overhead_per_packet) override;

  // Time since the oldest packet currently in the queue was added.
  webrtc::TimeDelta OldestPacketWaitTime() const override;

  // Sum of payload + padding bytes of all packets currently in the pacer queue.
  webrtc::DataSize QueueSizeData() const override;

  // Returns the time when the first packet was sent.
  std::optional<webrtc::Timestamp> FirstSentPacketTime() const override;

  // Returns the expected number of milliseconds it will take to send the
  // current packets in the queue, given the current size and bitrate, ignoring
  // priority.
  webrtc::TimeDelta ExpectedQueueTime() const override;

  // Set the average upper bound on pacer queuing delay. The pacer may send at
  // a higher rate than what was configured via SetPacingRates() in order to
  // keep ExpectedQueueTimeMs() below `limit_ms` on average.
  void SetQueueTimeLimit(webrtc::TimeDelta limit) override;

 protected:
  // Exposed as protected for test.
  struct Stats {
    Stats()
        : oldest_packet_enqueue_time(webrtc::Timestamp::MinusInfinity()),
          queue_size(webrtc::DataSize::Zero()),
          expected_queue_time(webrtc::TimeDelta::Zero()) {}
    webrtc::Timestamp oldest_packet_enqueue_time;
    webrtc::DataSize queue_size;
    webrtc::TimeDelta expected_queue_time;
    std::optional<webrtc::Timestamp> first_sent_packet_time;
  };
  void OnStatsUpdated(const Stats& stats);

 private:
  // Call in response to state updates that could warrant sending out packets.
  // Protected against re-entry from packet sent receipts.
  void MaybeScheduleProcessPackets();
  // Check if it is time to send packets, or schedule a delayed task if not.
  // Use Timestamp::MinusInfinity() to indicate that this call has _not_
  // been scheduled by the pacing controller. If this is the case, check if we
  // can execute immediately otherwise schedule a delay task that calls this
  // method again with desired (finite) scheduled process time.
  void MaybeProcessPackets(webrtc::Timestamp scheduled_process_time);

  void UpdateStats();
  Stats GetStats() const;

 private:
  std::shared_ptr<IceAgent> ice_agent_ = nullptr;
  webrtc::PacingController pacing_controller_;
  std::function<void(const webrtc::RtpPacketToSend&)> on_sent_packet_func_ =
      nullptr;

  std::function<std::vector<std::unique_ptr<RtpPacket>>(uint32_t, int64_t)>
      generat_padding_func_ = nullptr;

 private:
  std::shared_ptr<webrtc::Clock> clock_ = nullptr;

 private:
  const webrtc::TimeDelta max_hold_back_window_;
  const int max_hold_back_window_in_packets_;
  // We want only one (valid) delayed process task in flight at a time.
  // If the value of `next_process_time_` is finite, it is an id for a
  // delayed task that will call MaybeProcessPackets() with that time
  // as parameter.
  // Timestamp::MinusInfinity() indicates no valid pending task.
  webrtc::Timestamp next_process_time_;

  // Indicates if this task queue is started. If not, don't allow
  // posting delayed tasks yet.
  bool is_started_;

  // Indicates if this task queue is shutting down. If so, don't allow
  // posting any more delayed tasks as that can cause the task queue to
  // never drain.
  bool is_shutdown_;

  // Filtered size of enqueued packets, in bytes.
  rtc::ExpFilter packet_size_;
  bool include_overhead_;

  Stats current_stats_;
  // Protects against ProcessPackets reentry from packet sent receipts.
  bool processing_packets_ = false;

  TaskQueue task_queue_;
  int64_t transport_seq_ = 0;
  std::map<int32_t, int16_t> ssrc_seq_;

  webrtc::Timestamp last_send_time_;
};

#endif