#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "rtp_defines.h"
#include "rtp_packetizer_av1.h"
#include "rtp_packetizer_h264.h"

namespace {

bool Expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool AllPacketsUseTimestamp(
    const std::vector<std::unique_ptr<minirtc::RtpPacket>> &packets,
    uint32_t expected_timestamp) {
  if (packets.empty()) {
    return false;
  }
  for (const auto &packet : packets) {
    if (!packet || packet->Timestamp() != expected_timestamp) {
      return false;
    }
  }
  return true;
}

bool TestTimestampConversion() {
  bool ok = Expect(minirtc::rtp::VideoTimestampFromMicroseconds(1000000) ==
                       90000,
                   "one second did not convert to 90 kHz");
  ok &= Expect(minirtc::rtp::VideoTimestampFromMicroseconds(16667) == 1500,
               "60 fps interval did not convert to 1500 RTP ticks");
  ok &= Expect(minirtc::rtp::VideoTimestampToMicroseconds(90000) == 1000000,
               "90 kHz timestamp did not convert to one second");
  return ok;
}

bool TestH264PacketizerPreservesTimestamp() {
  constexpr uint32_t kTimestamp = 0x89abcdef;
  minirtc::RtpPacketizerH264 packetizer(1234);
  std::vector<uint8_t> small_nalu = {0x65, 0x01, 0x02};
  auto single_packet =
      packetizer.Build(small_nalu.data(), small_nalu.size(), kTimestamp, true);
  bool ok = Expect(AllPacketsUseTimestamp(single_packet, kTimestamp),
                   "H.264 single-NAL packetizer changed RTP timestamp");

  std::vector<uint8_t> large_nalu(MAX_NALU_LEN + 100, 0x55);
  large_nalu.front() = 0x65;
  auto fragmented =
      packetizer.Build(large_nalu.data(), large_nalu.size(), kTimestamp, true);
  ok &= Expect(AllPacketsUseTimestamp(fragmented, kTimestamp),
               "H.264 FU-A packetizer changed RTP timestamp");

  auto padding = packetizer.BuildPadding(2000, kTimestamp, true);
  ok &= Expect(AllPacketsUseTimestamp(padding, kTimestamp),
               "H.264 padding packetizer changed RTP timestamp");
  return ok;
}

bool TestAv1PacketizerPreservesTimestamp() {
  constexpr uint32_t kTimestamp = 0x12345678;
  minirtc::RtpPacketizerAv1 packetizer(5678);
  // One frame OBU with a one-byte payload.
  std::vector<uint8_t> obu = {0x32, 0x01, 0x00};
  auto packets = packetizer.Build(obu.data(), obu.size(), kTimestamp, true);
  return Expect(AllPacketsUseTimestamp(packets, kTimestamp),
                "AV1 packetizer changed RTP timestamp");
}

} // namespace

int main() {
  bool ok = true;
  ok &= TestTimestampConversion();
  ok &= TestH264PacketizerPreservesTimestamp();
  ok &= TestAv1PacketizerPreservesTimestamp();
  return ok ? 0 : 1;
}
