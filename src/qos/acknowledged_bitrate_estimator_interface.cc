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

namespace webrtc {

AcknowledgedBitrateEstimatorInterface::
    ~AcknowledgedBitrateEstimatorInterface() {}

std::unique_ptr<AcknowledgedBitrateEstimatorInterface>
AcknowledgedBitrateEstimatorInterface::Create() {
  return std::make_unique<AcknowledgedBitrateEstimator>();
}

}  // namespace webrtc
