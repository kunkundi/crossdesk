#pragma once

#include <cstdint>

namespace crossdesk::android_controller {
inline double VideoLatencyMilliseconds(uint64_t captured_us,
                                       uint64_t received_us,
                                       int64_t submitted_us) {
  // MiniRTC calibrates capture time when the frame is reassembled. Validate
  // that mapping at reception, not after decoding/Surface waits: a real local
  // backlog over five seconds must remain visible in the latency measurement.
  if (!captured_us || received_us < captured_us || submitted_us <= 0 ||
      received_us > static_cast<uint64_t>(submitted_us) ||
      received_us - captured_us > 5'000'000)
    return -1;
  return (static_cast<uint64_t>(submitted_us) - captured_us) / 1000.0;
}
}  // namespace crossdesk::android_controller
