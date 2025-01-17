/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_CONGESTION_CONTROLLER_INCLUDE_RECEIVE_SIDE_CONGESTION_CONTROLLER_H_
#define MODULES_CONGESTION_CONTROLLER_INCLUDE_RECEIVE_SIDE_CONGESTION_CONTROLLER_H_

#include <cstdint>
#include <memory>
#include <mutex>

#include "api/media_types.h"
#include "api/transport/network_control.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "congestion_control_feedback_generator.h"
#include "module_common_types.h"
#include "remb_throttler.h"
#include "rtc_base/thread_annotations.h"
#include "rtp_packet_received.h"

namespace webrtc {
class RemoteBitrateEstimator;

// This class represents the congestion control state for receive
// streams. For send side bandwidth estimation, this is simply
// relaying for each received RTP packet back to the sender. While for
// receive side bandwidth estimation, we do the estimation locally and
// send our results back to the sender.
class ReceiveSideCongestionController : public CallStatsObserver {
 public:
  ReceiveSideCongestionController(
      std::shared_ptr<SimulatedClock> clock,
      RtpTransportFeedbackGenerator::RtcpSender feedback_sender,
      RembThrottler::RembSender remb_sender,
      std::shared_ptr<NetworkStateEstimator> network_state_estimator);

  ~ReceiveSideCongestionController() override = default;

  void OnReceivedPacket(const RtpPacketReceived& packet, MediaType media_type);

  // Implements CallStatsObserver.
  void OnRttUpdate(int64_t avg_rtt_ms, int64_t max_rtt_ms) override;

  // This is send bitrate, used to control the rate of feedback messages.
  void OnBitrateChanged(int bitrate_bps);

  // Ensures the remote party is notified of the receive bitrate no larger than
  // `bitrate` using RTCP REMB.
  void SetMaxDesiredReceiveBitrate(DataRate bitrate);

  void SetTransportOverhead(DataSize overhead_per_packet);

  // Returns latest receive side bandwidth estimation.
  // Returns zero if receive side bandwidth estimation is unavailable.
  DataRate LatestReceiveSideEstimate() const;

  // Removes stream from receive side bandwidth estimation.
  // Noop if receive side bwe is not used or stream doesn't participate in it.
  void RemoveStream(uint32_t ssrc);

  // Runs periodic tasks if it is time to run them, returns time until next
  // call to `MaybeProcess` should be non idle.
  TimeDelta MaybeProcess();

 private:
  void PickEstimator();

  std::shared_ptr<SimulatedClock> clock_;
  RembThrottler remb_throttler_;

  CongestionControlFeedbackGenerator congestion_control_feedback_generator_;

  mutable std::mutex mutex_;
  std::unique_ptr<RemoteBitrateEstimator> rbe_;
  bool using_absolute_send_time_;
  uint32_t packets_since_absolute_send_time_;
};

}  // namespace webrtc

#endif  // MODULES_CONGESTION_CONTROLLER_INCLUDE_RECEIVE_SIDE_CONGESTION_CONTROLLER_H_
