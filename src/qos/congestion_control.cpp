#include "congestion_control.h"

#include <algorithm>
#include <numeric>
#include <vector>

#include "log.h"

constexpr int64_t kLossUpdateInterval = 1000;

// Pacing-rate relative to our target send rate.
// Multiplicative factor that is applied to the target bitrate to calculate
// the number of bytes that can be transmitted per interval.
// Increasing this factor will result in lower delays in cases of bitrate
// overshoots from the encoder.
constexpr float kDefaultPaceMultiplier = 2.5f;

// If the probe result is far below the current throughput estimate
// it's unlikely that the probe is accurate, so we don't want to drop too far.
// However, if we actually are overusing, we want to drop to something slightly
// below the current throughput estimate to drain the network queues.
constexpr double kProbeDropThroughputFraction = 0.85;

CongestionControl::CongestionControl()
    : packet_feedback_only_(true),
      use_min_allocatable_as_lower_bound_(false),
      ignore_probes_lower_than_network_estimate_(false),
      limit_probes_lower_than_throughput_estimate_(false),
      pace_at_max_of_bwe_and_lower_link_capacity_(false),
      limit_pacingfactor_by_upper_link_capacity_estimate_(false),
      probe_controller_(new ProbeController()),
      congestion_window_pushback_controller_(
          // std::make_unique<CongestionWindowPushbackController>()
          nullptr),
      bandwidth_estimation_(new SendSideBandwidthEstimation()),
      alr_detector_(new AlrDetector()),
      probe_bitrate_estimator_(new ProbeBitrateEstimator()),
      delay_based_bwe_(new DelayBasedBwe()),
      acknowledged_bitrate_estimator_(
          AcknowledgedBitrateEstimatorInterface::Create()),
      pacing_factor_(kDefaultPaceMultiplier),
      min_total_allocated_bitrate_(DataRate::Zero()),
      max_padding_rate_(DataRate::Zero())

{}

CongestionControl::~CongestionControl() {}

NetworkControlUpdate CongestionControl::OnTransportPacketsFeedback(
    TransportPacketsFeedback report) {
  if (report.packet_feedbacks.empty()) {
    // TODO(bugs.webrtc.org/10125): Design a better mechanism to safe-guard
    // against building very large network queues.
    return NetworkControlUpdate();
  }

  // if (congestion_window_pushback_controller_) {
  //   congestion_window_pushback_controller_->UpdateOutstandingData(
  //       report.data_in_flight.bytes());
  // }
  TimeDelta max_feedback_rtt = TimeDelta::MinusInfinity();
  TimeDelta min_propagation_rtt = TimeDelta::PlusInfinity();
  Timestamp max_recv_time = Timestamp::MinusInfinity();

  std::vector<PacketResult> feedbacks = report.ReceivedWithSendInfo();
  for (const auto& feedback : feedbacks)
    max_recv_time = std::max(max_recv_time, feedback.receive_time);

  for (const auto& feedback : feedbacks) {
    TimeDelta feedback_rtt =
        report.feedback_time - feedback.sent_packet.send_time;
    TimeDelta min_pending_time = max_recv_time - feedback.receive_time;
    TimeDelta propagation_rtt = feedback_rtt - min_pending_time;
    max_feedback_rtt = std::max(max_feedback_rtt, feedback_rtt);
    min_propagation_rtt = std::min(min_propagation_rtt, propagation_rtt);
  }

  if (max_feedback_rtt.IsFinite()) {
    feedback_max_rtts_.push_back(max_feedback_rtt.ms());
    const size_t kMaxFeedbackRttWindow = 32;
    if (feedback_max_rtts_.size() > kMaxFeedbackRttWindow)
      feedback_max_rtts_.pop_front();
    // TODO(srte): Use time since last unacknowledged packet.
    bandwidth_estimation_->UpdatePropagationRtt(report.feedback_time,
                                                min_propagation_rtt);
  }
  if (packet_feedback_only_) {
    if (!feedback_max_rtts_.empty()) {
      int64_t sum_rtt_ms =
          std::accumulate(feedback_max_rtts_.begin(), feedback_max_rtts_.end(),
                          static_cast<int64_t>(0));
      int64_t mean_rtt_ms = sum_rtt_ms / feedback_max_rtts_.size();
      if (delay_based_bwe_) {
        delay_based_bwe_->OnRttUpdate(TimeDelta::Millis(mean_rtt_ms));
      }
    }

    TimeDelta feedback_min_rtt = TimeDelta::PlusInfinity();
    for (const auto& packet_feedback : feedbacks) {
      TimeDelta pending_time = max_recv_time - packet_feedback.receive_time;
      TimeDelta rtt = report.feedback_time -
                      packet_feedback.sent_packet.send_time - pending_time;
      // Value used for predicting NACK round trip time in FEC controller.
      feedback_min_rtt = std::min(rtt, feedback_min_rtt);
    }
    if (feedback_min_rtt.IsFinite()) {
      bandwidth_estimation_->UpdateRtt(feedback_min_rtt, report.feedback_time);
    }

    expected_packets_since_last_loss_update_ +=
        report.PacketsWithFeedback().size();
    for (const auto& packet_feedback : report.PacketsWithFeedback()) {
      if (!packet_feedback.IsReceived())
        lost_packets_since_last_loss_update_ += 1;
    }
    if (report.feedback_time > next_loss_update_) {
      next_loss_update_ =
          report.feedback_time + TimeDelta::Millis(kLossUpdateInterval);
      bandwidth_estimation_->UpdatePacketsLost(
          lost_packets_since_last_loss_update_,
          expected_packets_since_last_loss_update_, report.feedback_time);
      expected_packets_since_last_loss_update_ = 0;
      lost_packets_since_last_loss_update_ = 0;
    }
  }
  std::optional<int64_t> alr_start_time =
      alr_detector_->GetApplicationLimitedRegionStartTime();

  if (previously_in_alr_ && !alr_start_time.has_value()) {
    int64_t now_ms = report.feedback_time.ms();
    acknowledged_bitrate_estimator_->SetAlrEndedTime(report.feedback_time);
    probe_controller_->SetAlrEndedTimeMs(now_ms);
  }
  previously_in_alr_ = alr_start_time.has_value();

  acknowledged_bitrate_estimator_->IncomingPacketFeedbackVector(
      report.SortedByReceiveTime());
  auto acknowledged_bitrate = acknowledged_bitrate_estimator_->bitrate();
  bandwidth_estimation_->SetAcknowledgedRate(acknowledged_bitrate,
                                             report.feedback_time);
  for (const auto& feedback : report.SortedByReceiveTime()) {
    if (feedback.sent_packet.pacing_info.probe_cluster_id !=
        PacedPacketInfo::kNotAProbe) {
      probe_bitrate_estimator_->HandleProbeAndEstimateBitrate(feedback);
    }
  }

  std::optional<DataRate> probe_bitrate =
      probe_bitrate_estimator_->FetchAndResetLastEstimatedBitrate();
  if (limit_probes_lower_than_throughput_estimate_ && probe_bitrate &&
      acknowledged_bitrate) {
    // Limit the backoff to something slightly below the acknowledged
    // bitrate. ("Slightly below" because we want to drain the queues
    // if we are actually overusing.)
    // The acknowledged bitrate shouldn't normally be higher than the delay
    // based estimate, but it could happen e.g. due to packet bursts or
    // encoder overshoot. We use std::min to ensure that a probe result
    // below the current BWE never causes an increase.
    DataRate limit =
        std::min(delay_based_bwe_->last_estimate(),
                 *acknowledged_bitrate * kProbeDropThroughputFraction);
    probe_bitrate = std::max(*probe_bitrate, limit);
  }

  NetworkControlUpdate update;
  bool recovered_from_overuse = false;

  DelayBasedBwe::Result result;
  result = delay_based_bwe_->IncomingPacketFeedbackVector(
      report, acknowledged_bitrate, probe_bitrate, alr_start_time.has_value());

  if (result.updated) {
    if (result.probe) {
      bandwidth_estimation_->SetSendBitrate(result.target_bitrate,
                                            report.feedback_time);
    }
    // Since SetSendBitrate now resets the delay-based estimate, we have to
    // call UpdateDelayBasedEstimate after SetSendBitrate.
    bandwidth_estimation_->UpdateDelayBasedEstimate(report.feedback_time,
                                                    result.target_bitrate);
  }
  // bandwidth_estimation_->UpdateLossBasedEstimator(
  //     report, result.delay_detector_state, probe_bitrate,
  //     alr_start_time.has_value());
  // if (result.updated) {
  //   // Update the estimate in the ProbeController, in case we want to probe.
  //   MaybeTriggerOnNetworkChanged(&update, report.feedback_time);
  // }

  recovered_from_overuse = result.recovered_from_overuse;

  if (recovered_from_overuse) {
    probe_controller_->SetAlrStartTimeMs(alr_start_time);
    auto probes = probe_controller_->RequestProbe(report.feedback_time);
    update.probe_cluster_configs.insert(update.probe_cluster_configs.end(),
                                        probes.begin(), probes.end());
  }

  // No valid RTT could be because send-side BWE isn't used, in which case
  // we don't try to limit the outstanding packets.
  // if (rate_control_settings_.UseCongestionWindow() &&
  //     max_feedback_rtt.IsFinite()) {
  //   UpdateCongestionWindowSize();
  // }
  // if (congestion_window_pushback_controller_ && current_data_window_) {
  //   congestion_window_pushback_controller_->SetDataWindow(
  //       *current_data_window_);
  // } else {
  //   update.congestion_window = current_data_window_;
  // }

  return update;
}

void CongestionControl::MaybeTriggerOnNetworkChanged(
    NetworkControlUpdate* update, Timestamp at_time) {
  // uint8_t fraction_loss = bandwidth_estimation_->fraction_loss();
  // TimeDelta round_trip_time = bandwidth_estimation_->round_trip_time();
  // DataRate loss_based_target_rate = bandwidth_estimation_->target_rate();
  // LossBasedState loss_based_state =
  // bandwidth_estimation_->loss_based_state(); DataRate pushback_target_rate =
  // loss_based_target_rate;

  // double cwnd_reduce_ratio = 0.0;
  // if (congestion_window_pushback_controller_) {
  //   int64_t pushback_rate =
  //       congestion_window_pushback_controller_->UpdateTargetBitrate(
  //           loss_based_target_rate.bps());
  //   pushback_rate = std::max<int64_t>(bandwidth_estimation_->GetMinBitrate(),
  //                                     pushback_rate);
  //   pushback_target_rate = DataRate::BitsPerSec(pushback_rate);
  //   if (rate_control_settings_.UseCongestionWindowDropFrameOnly()) {
  //     cwnd_reduce_ratio = static_cast<double>(loss_based_target_rate.bps() -
  //                                             pushback_target_rate.bps()) /
  //                         loss_based_target_rate.bps();
  //   }
  // }
  // DataRate stable_target_rate =
  //     bandwidth_estimation_->GetEstimatedLinkCapacity();
  // stable_target_rate = std::min(stable_target_rate, pushback_target_rate);

  // if ((loss_based_target_rate != last_loss_based_target_rate_) ||
  //     (loss_based_state != last_loss_base_state_) ||
  //     (fraction_loss != last_estimated_fraction_loss_) ||
  //     (round_trip_time != last_estimated_round_trip_time_) ||
  //     (pushback_target_rate != last_pushback_target_rate_) ||
  //     (stable_target_rate != last_stable_target_rate_)) {
  //   last_loss_based_target_rate_ = loss_based_target_rate;
  //   last_pushback_target_rate_ = pushback_target_rate;
  //   last_estimated_fraction_loss_ = fraction_loss;
  //   last_estimated_round_trip_time_ = round_trip_time;
  //   last_stable_target_rate_ = stable_target_rate;
  //   last_loss_base_state_ = loss_based_state;

  //   alr_detector_->SetEstimatedBitrate(loss_based_target_rate.bps());

  //   TimeDelta bwe_period = delay_based_bwe_->GetExpectedBwePeriod();

  //   TargetTransferRate target_rate_msg;
  //   target_rate_msg.at_time = at_time;
  //   if (rate_control_settings_.UseCongestionWindowDropFrameOnly()) {
  //     target_rate_msg.target_rate = loss_based_target_rate;
  //     target_rate_msg.cwnd_reduce_ratio = cwnd_reduce_ratio;
  //   } else {
  //     target_rate_msg.target_rate = pushback_target_rate;
  //   }
  //   target_rate_msg.stable_target_rate = stable_target_rate;
  //   target_rate_msg.network_estimate.at_time = at_time;
  //   target_rate_msg.network_estimate.round_trip_time = round_trip_time;
  //   target_rate_msg.network_estimate.loss_rate_ratio = fraction_loss /
  //   255.0f; target_rate_msg.network_estimate.bwe_period = bwe_period;

  //   update->target_rate = target_rate_msg;

  //   auto probes = probe_controller_->SetEstimatedBitrate(
  //       loss_based_target_rate,
  //       GetBandwidthLimitedCause(bandwidth_estimation_->loss_based_state(),
  //                                bandwidth_estimation_->IsRttAboveLimit(),
  //                                delay_based_bwe_->last_state()),
  //       at_time);
  //   update->probe_cluster_configs.insert(update->probe_cluster_configs.end(),
  //                                        probes.begin(), probes.end());
  //   update->pacer_config = GetPacingRates(at_time);
  //   LOG_INFO("bwe {} pushback_target_bps={} estimate_bps={}", at_time.ms(),
  //            last_pushback_target_rate_.bps(), loss_based_target_rate.bps());
  // }
}