/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-13
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _TRANSPORT_FEEDBACK_ADAPTER_H_
#define _TRANSPORT_FEEDBACK_ADAPTER_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

#include "api/transport/network_types.h"
#include "congestion_control_feedback.h"
#include "network_route.h"
#include "rtc_base/numerics/sequence_number_unwrapper.h"

struct PacketFeedback {
  PacketFeedback() = default;
  // Time corresponding to when this object was created.
  int64_t creation_time = std::numeric_limits<int64_t>::min();
  SentPacket sent;
  // Time corresponding to when the packet was received. Timestamped with the
  // receiver's clock. For unreceived packet,
  // std::numeric_limits<int64_t>::max() is used.
  int64_t receive_time = std::numeric_limits<int64_t>::max();

  // The network route that this packet is associated with.
  NetworkRoute network_route;

  uint32_t ssrc = 0;
  uint16_t rtp_sequence_number = 0;
};

class InFlightBytesTracker {
 public:
  void AddInFlightPacketBytes(const PacketFeedback& packet);
  void RemoveInFlightPacketBytes(const PacketFeedback& packet);
  int64_t GetOutstandingData(const NetworkRoute& network_route) const;

 private:
  struct NetworkRouteComparator {
    bool operator()(const NetworkRoute& a, const NetworkRoute& b) const;
  };
  std::map<NetworkRoute, int64_t, NetworkRouteComparator> in_flight_data_;
};

// TransportFeedbackAdapter converts RTCP feedback packets to RTCP agnostic per
// packet send/receive information.
// It supports rtcp::CongestionControlFeedback according to RFC 8888 and
// rtcp::TransportFeedback according to
// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01
class TransportFeedbackAdapter {
 public:
  TransportFeedbackAdapter();

  std::optional<TransportPacketsFeedback> ProcessCongestionControlFeedback(
      const CongestionControlFeedback& feedback, int64_t feedback_receive_time);

  void SetNetworkRoute(const NetworkRoute& network_route);

  int64_t GetOutstandingData() const;

 private:
  enum class SendTimeHistoryStatus { kNotAdded, kOk, kDuplicate };

  struct SsrcAndRtpSequencenumber {
    uint32_t ssrc;
    uint16_t rtp_sequence_number;

    bool operator<(const SsrcAndRtpSequencenumber& other) const {
      return std::tie(ssrc, rtp_sequence_number) <
             std::tie(other.ssrc, other.rtp_sequence_number);
    }
  };

  std::optional<PacketFeedback> RetrievePacketFeedback(
      int64_t transport_seq_num, bool received);
  std::optional<PacketFeedback> RetrievePacketFeedback(
      const SsrcAndRtpSequencenumber& key, bool received);
  std::optional<TransportPacketsFeedback> ToTransportFeedback(
      std::vector<PacketResult> packet_results, int64_t feedback_receive_time,
      bool supports_ecn);

  int64_t pending_untracked_size_ = 0;
  int64_t last_send_time_ = std::numeric_limits<int64_t>::min();
  int64_t last_untracked_send_time_ = std::numeric_limits<int64_t>::min();
  RtpSequenceNumberUnwrapper seq_num_unwrapper_;

  // Sequence numbers are never negative, using -1 as it always < a real
  // sequence number.
  int64_t last_ack_seq_num_ = -1;
  InFlightBytesTracker in_flight_;
  NetworkRoute network_route_;

  int64_t current_offset_ = std::numeric_limits<int64_t>::min();

  // `last_transport_feedback_base_time` is only used for transport feedback to
  // track base time.
  int64_t last_transport_feedback_base_time_ =
      std::numeric_limits<int64_t>::min();
  // Used by RFC 8888 congestion control feedback to track base time.
  std::optional<uint32_t> last_feedback_compact_ntp_time_;

  // Map SSRC and RTP sequence number to transport sequence number.
  std::map<SsrcAndRtpSequencenumber, int64_t /*transport_sequence_number*/>
      rtp_to_transport_sequence_number_;
  std::map<int64_t, PacketFeedback> history_;
};

#endif