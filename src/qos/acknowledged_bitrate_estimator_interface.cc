/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "acknowledged_bitrate_estimator_interface.h"

#include <algorithm>
#include <memory>

#include "acknowledged_bitrate_estimator.h"
#include "api/units/time_delta.h"
#include "log.h"
#include "robust_throughput_estimator.h"

namespace webrtc {

constexpr char RobustThroughputEstimatorSettings::kKey[];

RobustThroughputEstimatorSettings::RobustThroughputEstimatorSettings() {
  if (window_packets < 10 || 1000 < window_packets) {
    LOG_WARN("Window size must be between 10 and 1000 packets");
    window_packets = 20;
  }
  if (max_window_packets < 10 || 1000 < max_window_packets) {
    LOG_WARN("Max window size must be between 10 and 1000 packets");
    max_window_packets = 500;
  }
  max_window_packets = std::max(max_window_packets, window_packets);

  if (required_packets < 10 || 1000 < required_packets) {
    LOG_WARN(
        "Required number of initial packets must be between 10 and 1000 "
        "packets");
    required_packets = 10;
  }
  required_packets = std::min(required_packets, window_packets);

  if (min_window_duration < TimeDelta::Millis(100) ||
      TimeDelta::Millis(3000) < min_window_duration) {
    LOG_WARN("Window duration must be between 100 and 3000 ms");
    min_window_duration = TimeDelta::Millis(750);
  }
  if (max_window_duration < TimeDelta::Seconds(1) ||
      TimeDelta::Seconds(15) < max_window_duration) {
    LOG_WARN("Max window duration must be between 1 and 15 s");
    max_window_duration = TimeDelta::Seconds(5);
  }
  min_window_duration = std::min(min_window_duration, max_window_duration);

  if (unacked_weight < 0.0 || 1.0 < unacked_weight) {
    LOG_WARN("Weight for prior unacked size must be between 0 and 1.");
    unacked_weight = 1.0;
  }
}

AcknowledgedBitrateEstimatorInterface::
    ~AcknowledgedBitrateEstimatorInterface() {}

std::unique_ptr<AcknowledgedBitrateEstimatorInterface>
AcknowledgedBitrateEstimatorInterface::Create() {
  // return std::make_unique<AcknowledgedBitrateEstimator>();
  RobustThroughputEstimatorSettings settings;
  return std::make_unique<RobustThroughputEstimator>(settings);
}

}  // namespace webrtc
