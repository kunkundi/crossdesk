
#include "packet_sender_imp.h"

#include "log.h"

const int PacketSenderImp::kNoPacketHoldback = -1;

PacketSenderImp::PacketSenderImp(std::shared_ptr<IceAgent> ice_agent,
                                 std::shared_ptr<webrtc::Clock> clock,
                                 std::shared_ptr<TaskQueue> task_queue)
    : ice_agent_(ice_agent),
      clock_(clock),
      pacing_controller_(clock.get(), this),
      max_hold_back_window_(webrtc::TimeDelta::Millis(5)),
      max_hold_back_window_in_packets_(3),
      next_process_time_(webrtc::Timestamp::MinusInfinity()),
      is_started_(false),
      is_shutdown_(false),
      packet_size_(/*alpha=*/0.95),
      include_overhead_(false),
      last_send_time_(webrtc::Timestamp::Millis(0)),
      last_call_time_(webrtc::Timestamp::Millis(0)),
      task_queue_(task_queue) {}

PacketSenderImp::~PacketSenderImp() { is_shutdown_ = true; }

std::vector<std::unique_ptr<webrtc::RtpPacketToSend>>
PacketSenderImp::GeneratePadding(webrtc::DataSize size) {
  std::vector<std::unique_ptr<webrtc::RtpPacketToSend>> to_send_rtp_packets;
  std::vector<std::unique_ptr<RtpPacket>> rtp_packets =
      generat_padding_func_(size.bytes(), clock_->CurrentTime().ms());
  for (auto &packet : rtp_packets) {
    std::unique_ptr<webrtc::RtpPacketToSend> rtp_packet_to_send(
        static_cast<webrtc::RtpPacketToSend *>(packet.release()));

    rtp_packet_to_send->set_capture_time(clock_->CurrentTime());
    rtp_packet_to_send->set_transport_sequence_number((transport_seq_)++);
    rtp_packet_to_send->set_packet_type(webrtc::RtpPacketMediaType::kPadding);

    to_send_rtp_packets.push_back(std::move(rtp_packet_to_send));
  }

  return to_send_rtp_packets;
}

void PacketSenderImp::SetSendBurstInterval(webrtc::TimeDelta burst_interval) {
  pacing_controller_.SetSendBurstInterval(burst_interval);
}

void PacketSenderImp::SetAllowProbeWithoutMediaPacket(bool allow) {
  pacing_controller_.SetAllowProbeWithoutMediaPacket(allow);
}

void PacketSenderImp::EnsureStarted() {
  is_started_ = true;
  MaybeProcessPackets(webrtc::Timestamp::MinusInfinity());
}

void PacketSenderImp::CreateProbeClusters(
    std::vector<webrtc::ProbeClusterConfig> probe_cluster_configs) {
  pacing_controller_.CreateProbeClusters(probe_cluster_configs);
  MaybeScheduleProcessPackets();
}

void PacketSenderImp::Pause() { pacing_controller_.Pause(); }

void PacketSenderImp::Resume() {
  pacing_controller_.Resume();
  MaybeProcessPackets(webrtc::Timestamp::MinusInfinity());
}

void PacketSenderImp::SetCongested(bool congested) {
  pacing_controller_.SetCongested(congested);
  MaybeScheduleProcessPackets();
}

void PacketSenderImp::SetPacingRates(webrtc::DataRate pacing_rate,
                                     webrtc::DataRate padding_rate) {
  pacing_controller_.SetPacingRates(pacing_rate, padding_rate);
  MaybeScheduleProcessPackets();
}

void PacketSenderImp::EnqueuePackets(
    std::vector<std::unique_ptr<webrtc::RtpPacketToSend>> packets) {
  task_queue_->PostTask([this, packets = std::move(packets)]() mutable {
    for (auto &packet : packets) {
      size_t packet_size = packet->payload_size() + packet->padding_size();
      if (include_overhead_) {
        packet_size += packet->headers_size();
      }
      packet_size_.Apply(1, packet_size);
      pacing_controller_.EnqueuePacket(std::move(packet));
    }
    MaybeProcessPackets(webrtc::Timestamp::MinusInfinity());
  });
}

void PacketSenderImp::RemovePacketsForSsrc(uint32_t ssrc) {
  task_queue_->PostTask([this, ssrc] {
    pacing_controller_.RemovePacketsForSsrc(ssrc);
    MaybeProcessPackets(webrtc::Timestamp::MinusInfinity());
  });
}

void PacketSenderImp::SetAccountForAudioPackets(bool account_for_audio) {
  pacing_controller_.SetAccountForAudioPackets(account_for_audio);
  MaybeProcessPackets(webrtc::Timestamp::MinusInfinity());
}

void PacketSenderImp::SetIncludeOverhead() {
  include_overhead_ = true;
  pacing_controller_.SetIncludeOverhead();
  MaybeProcessPackets(webrtc::Timestamp::MinusInfinity());
}

void PacketSenderImp::SetTransportOverhead(
    webrtc::DataSize overhead_per_packet) {
  pacing_controller_.SetTransportOverhead(overhead_per_packet);
  MaybeProcessPackets(webrtc::Timestamp::MinusInfinity());
}

void PacketSenderImp::SetQueueTimeLimit(webrtc::TimeDelta limit) {
  pacing_controller_.SetQueueTimeLimit(limit);
  MaybeProcessPackets(webrtc::Timestamp::MinusInfinity());
}

webrtc::TimeDelta PacketSenderImp::ExpectedQueueTime() const {
  return GetStats().expected_queue_time;
}

webrtc::DataSize PacketSenderImp::QueueSizeData() const {
  return GetStats().queue_size;
}

std::optional<webrtc::Timestamp> PacketSenderImp::FirstSentPacketTime() const {
  return GetStats().first_sent_packet_time;
}

webrtc::TimeDelta PacketSenderImp::OldestPacketWaitTime() const {
  webrtc::Timestamp oldest_packet = GetStats().oldest_packet_enqueue_time;
  if (oldest_packet.IsInfinite()) {
    return webrtc::TimeDelta::Zero();
  }

  // (webrtc:9716): The clock is not always monotonic.
  webrtc::Timestamp current = clock_->CurrentTime();
  if (current < oldest_packet) {
    return webrtc::TimeDelta::Zero();
  }

  return current - oldest_packet;
}

void PacketSenderImp::OnStatsUpdated(const Stats &stats) {
  current_stats_ = stats;
}

void PacketSenderImp::MaybeScheduleProcessPackets() {
  if (!processing_packets_) {
    MaybeProcessPackets(webrtc::Timestamp::MinusInfinity());
  }
}

void PacketSenderImp::MaybeProcessPackets(
    webrtc::Timestamp scheduled_process_time) {
  if (is_shutdown_ || !is_started_) {
    LOG_ERROR("shutdown {}, started {}", is_shutdown_, is_started_);
    return;
  }

  // Protects against re-entry from transport feedback calling into the task
  // queue pacer.
  auto cleanup = std::unique_ptr<void, std::function<void(void *)>>(
      nullptr, [this](void *) { processing_packets_ = false; });

  webrtc::Timestamp next_send_time = pacing_controller_.NextSendTime();
  const webrtc::Timestamp now = clock_->CurrentTime();
  webrtc::TimeDelta early_execute_margin =
      pacing_controller_.IsProbing()
          ? webrtc::PacingController::kMaxEarlyProbeProcessing
          : webrtc::TimeDelta::Zero();

  // Process packets and update stats.
  while (next_send_time <= now + early_execute_margin) {
    pacing_controller_.ProcessPackets();
    next_send_time = pacing_controller_.NextSendTime();

    // Probing state could change. Get margin after process packets.
    early_execute_margin =
        pacing_controller_.IsProbing()
            ? webrtc::PacingController::kMaxEarlyProbeProcessing
            : webrtc::TimeDelta::Zero();
  }

  UpdateStats();

  // Ignore retired scheduled task, otherwise reset `next_process_time_`.
  if (scheduled_process_time.IsFinite()) {
    if (scheduled_process_time != next_process_time_) {
      return;
    }
    next_process_time_ = webrtc::Timestamp::MinusInfinity();
  }

  // Do not hold back in probing.
  webrtc::TimeDelta hold_back_window = webrtc::TimeDelta::Zero();
  if (!pacing_controller_.IsProbing()) {
    hold_back_window = max_hold_back_window_;
    webrtc::DataRate pacing_rate = pacing_controller_.pacing_rate();
    if (max_hold_back_window_in_packets_ != kNoPacketHoldback &&
        !pacing_rate.IsZero() &&
        packet_size_.filtered() != rtc::ExpFilter::kValueUndefined) {
      webrtc::TimeDelta avg_packet_send_time =
          webrtc::DataSize::Bytes(packet_size_.filtered()) / pacing_rate;
      hold_back_window =
          std::min(hold_back_window,
                   avg_packet_send_time * max_hold_back_window_in_packets_);
    }
  }

  // Calculate next process time.
  webrtc::TimeDelta time_to_next_process =
      std::max(hold_back_window, next_send_time - now - early_execute_margin);
  next_send_time = now + time_to_next_process;

  // If no in flight task or in flight task is later than `next_send_time`,
  // schedule a new one. Previous in flight task will be retired.
  if (next_process_time_.IsMinusInfinity() ||
      next_process_time_ > next_send_time) {
    // Prefer low precision if allowed and not probing.
    task_queue_->PostDelayedTask(
        [this, next_send_time]() { MaybeProcessPackets(next_send_time); },
        time_to_next_process.RoundUpTo(webrtc::TimeDelta::Millis(1)).ms());
    next_process_time_ = next_send_time;
  }
}

void PacketSenderImp::UpdateStats() {
  Stats new_stats;
  new_stats.expected_queue_time = pacing_controller_.ExpectedQueueTime();
  new_stats.first_sent_packet_time = pacing_controller_.FirstSentPacketTime();
  new_stats.oldest_packet_enqueue_time =
      pacing_controller_.OldestPacketEnqueueTime();
  new_stats.queue_size = pacing_controller_.QueueSizeData();
  OnStatsUpdated(new_stats);
}

PacketSenderImp::Stats PacketSenderImp::GetStats() const {
  return current_stats_;
}

/*----------------------------------------------------------------------------*/

int PacketSenderImp::EnqueueRtpPacket(
    std::vector<std::unique_ptr<RtpPacket>> &rtp_packets,
    int64_t captured_timestamp_us) {
  std::vector<std::unique_ptr<webrtc::RtpPacketToSend>> to_send_rtp_packets;
  for (auto &rtp_packet : rtp_packets) {
    std::unique_ptr<webrtc::RtpPacketToSend> rtp_packet_to_send(
        static_cast<webrtc::RtpPacketToSend *>(rtp_packet.release()));
    rtp_packet_to_send->set_capture_time(clock_->CurrentTime());
    rtp_packet_to_send->set_transport_sequence_number(transport_seq_++);

    switch (rtp_packet_to_send->PayloadType()) {
      case rtp::PAYLOAD_TYPE::H264:
        rtp_packet_to_send->set_packet_type(webrtc::RtpPacketMediaType::kVideo);
        break;
      case rtp::PAYLOAD_TYPE::AV1:
        rtp_packet_to_send->set_packet_type(webrtc::RtpPacketMediaType::kVideo);
        break;
      case rtp::PAYLOAD_TYPE::H264_FEC_SOURCE:
        rtp_packet_to_send->set_packet_type(
            webrtc::RtpPacketMediaType::kForwardErrorCorrection);
        break;
      case rtp::PAYLOAD_TYPE::H264_FEC_REPAIR:
        rtp_packet_to_send->set_packet_type(
            webrtc::RtpPacketMediaType::kForwardErrorCorrection);
        break;
      case rtp::PAYLOAD_TYPE::OPUS:
        rtp_packet_to_send->set_packet_type(webrtc::RtpPacketMediaType::kAudio);
        break;
      default:
        rtp_packet_to_send->set_packet_type(webrtc::RtpPacketMediaType::kVideo);
        break;
    }
    // webrtc::PacedPacketInfo cluster_info;
    // SendPacket(std::move(rtp_packet_to_send), cluster_info);

    to_send_rtp_packets.push_back(std::move(rtp_packet_to_send));
  }

  EnqueuePackets(std::move(to_send_rtp_packets));
  return 0;
}

int PacketSenderImp::EnqueueRtpPacket(
    std::vector<std::unique_ptr<webrtc::RtpPacketToSend>> &rtp_packets) {
  EnqueuePackets(std::move(rtp_packets));
  return 0;
}