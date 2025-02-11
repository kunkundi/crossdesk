#ifndef _CONGESTION_CONTROL_H_
#define _CONGESTION_CONTROL_H_

#include <deque>
#include <memory>

#include "acknowledged_bitrate_estimator.h"
#include "acknowledged_bitrate_estimator_interface.h"
#include "alr_detector.h"
#include "api/network_state_predictor.h"
#include "api/transport/network_types.h"
#include "congestion_window_pushback_controller.h"
#include "delay_based_bwe.h"
#include "probe_controller.h"
#include "send_side_bandwidth_estimation.h"

using namespace webrtc;

class CongestionControl {
 public:
  CongestionControl();
  ~CongestionControl();

 public:
  NetworkControlUpdate OnTransportPacketsFeedback(
      TransportPacketsFeedback report);

  void MaybeTriggerOnNetworkChanged(NetworkControlUpdate* update,
                                    Timestamp at_time);

 private:
  PacerConfig GetPacingRates(Timestamp at_time) const;

 private:
  const bool packet_feedback_only_;
  const bool use_min_allocatable_as_lower_bound_;
  const bool ignore_probes_lower_than_network_estimate_;
  const bool limit_probes_lower_than_throughput_estimate_;
  const bool pace_at_max_of_bwe_and_lower_link_capacity_;
  const bool limit_pacingfactor_by_upper_link_capacity_estimate_;

  const std::unique_ptr<ProbeController> probe_controller_;
  const std::unique_ptr<CongestionWindowPushbackController>
      congestion_window_pushback_controller_;

  std::unique_ptr<SendSideBandwidthEstimation> bandwidth_estimation_;
  std::unique_ptr<AlrDetector> alr_detector_;
  std::unique_ptr<ProbeBitrateEstimator> probe_bitrate_estimator_;
  std::unique_ptr<DelayBasedBwe> delay_based_bwe_;
  std::unique_ptr<AcknowledgedBitrateEstimatorInterface>
      acknowledged_bitrate_estimator_;

  DataRate min_target_rate_ = DataRate::Zero();
  DataRate min_data_rate_ = DataRate::Zero();
  DataRate max_data_rate_ = DataRate::PlusInfinity();
  std::optional<DataRate> starting_rate_;

  bool first_packet_sent_ = false;

  std::optional<NetworkStateEstimate> estimate_;

  Timestamp next_loss_update_ = Timestamp::MinusInfinity();
  int lost_packets_since_last_loss_update_ = 0;
  int expected_packets_since_last_loss_update_ = 0;

  std::deque<int64_t> feedback_max_rtts_;

  DataRate last_loss_based_target_rate_;
  DataRate last_pushback_target_rate_;
  DataRate last_stable_target_rate_;
  LossBasedState last_loss_base_state_;

  std::optional<uint8_t> last_estimated_fraction_loss_ = 0;
  TimeDelta last_estimated_round_trip_time_ = TimeDelta::PlusInfinity();

  double pacing_factor_;
  DataRate min_total_allocated_bitrate_;
  DataRate max_padding_rate_;

  bool previously_in_alr_ = false;

  std::optional<DataSize> current_data_window_;
};

#endif