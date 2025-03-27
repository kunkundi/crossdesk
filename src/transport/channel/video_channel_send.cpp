#include "video_channel_send.h"

#include "log.h"
#include "rtc_base/network/sent_packet.h"

#define SAVE_RTP_SENT_STREAM

VideoChannelSend::VideoChannelSend(
    std::shared_ptr<SystemClock> clock, std::shared_ptr<IceAgent> ice_agent,
    std::shared_ptr<PacketSender> packet_sender,
    std::shared_ptr<IOStatistics> ice_io_statistics,
    std::function<void(const webrtc::RtpPacketToSend& packet)>
        on_sent_packet_func)
    : ice_agent_(ice_agent),
      packet_sender_(packet_sender),
      ice_io_statistics_(ice_io_statistics),
      on_sent_packet_func_(on_sent_packet_func),
      delta_ntp_internal_ms_(clock->CurrentNtpInMilliseconds() -
                             clock->CurrentTimeMs()),
      rtp_packet_history_(clock),
      clock_(clock) {
#ifdef SAVE_RTP_SENT_STREAM
  file_rtp_sent_ = fopen("rtp_sent_stream.h264", "w+b");
  if (!file_rtp_sent_) {
    LOG_WARN("Fail to open rtp_sent_stream.h264");
  }
#endif
};

VideoChannelSend::~VideoChannelSend() {
#ifdef SAVE_RTP_SENT_STREAM
  if (file_rtp_sent_) {
    fflush(file_rtp_sent_);
    fclose(file_rtp_sent_);
    file_rtp_sent_ = nullptr;
  }
#endif
}

void VideoChannelSend::Initialize(rtp::PAYLOAD_TYPE payload_type) {
  rtp_video_sender_ =
      std::make_unique<RtpVideoSender>(clock_, ice_io_statistics_);
  rtp_packetizer_ =
      RtpPacketizer::Create(payload_type, rtp_video_sender_->GetSsrc());
  rtp_video_sender_->SetSendDataFunc(
      [this](const char* data, size_t size) -> int {
        if (!ice_agent_) {
          LOG_ERROR("ice_agent_ is nullptr");
          return -1;
        }

        auto ice_state = ice_agent_->GetIceState();

        if (ICE_STATE_DESTROYED == ice_state) {
          return -2;
        }

        ice_io_statistics_->UpdateVideoOutboundBytes((uint32_t)size);

        return ice_agent_->Send(data, size);
      });

  rtp_video_sender_->SetOnSentPacketFunc(
      [this](const webrtc::RtpPacketToSend& packet) -> void {
        on_sent_packet_func_(packet);
      });

  rtp_video_sender_->Start();
}

void VideoChannelSend::SetEnqueuePacketsFunc(
    std::function<void(std::vector<std::unique_ptr<webrtc::RtpPacketToSend>>&)>
        enqueue_packets_func) {
  rtp_video_sender_->SetEnqueuePacketsFunc(enqueue_packets_func);
}

void VideoChannelSend::OnSentRtpPacket(
    std::unique_ptr<webrtc::RtpPacketToSend> packet) {
  if (packet->retransmitted_sequence_number()) {
    rtp_packet_history_.MarkPacketAsSent(
        *packet->retransmitted_sequence_number());
  } else if (packet->PayloadType() != rtp::PAYLOAD_TYPE::H264 - 1) {
    rtp_packet_history_.PutRtpPacket(std::move(packet), clock_->CurrentTime());
  }
}

void VideoChannelSend::OnReceiveNack(
    const std::vector<uint16_t>& nack_sequence_numbers) {
  // int64_t rtt = rtt_ms();
  // if (rtt == 0) {
  //   if (std::optional<webrtc::TimeDelta> average_rtt =
  //           rtcp_receiver_.AverageRtt()) {
  //     rtt = average_rtt->ms();
  //   }
  // }

  int64_t avg_rtt = 10;
  rtp_packet_history_.SetRtt(TimeDelta::Millis(5 + avg_rtt));
  for (uint16_t seq_no : nack_sequence_numbers) {
    const int32_t bytes_sent = ReSendPacket(seq_no);
    if (bytes_sent < 0) {
      // Failed to send one Sequence number. Give up the rest in this nack.
      LOG_WARN("Failed resending RTP packet {}, Discard rest of packets",
               seq_no);
      break;
    }
  }
}

std::vector<std::unique_ptr<RtpPacket>> VideoChannelSend::GeneratePadding(
    uint32_t payload_size, int64_t captured_timestamp_us) {
  if (rtp_packetizer_) {
    return rtp_packetizer_->BuildPadding(payload_size, captured_timestamp_us,
                                         true);
  }
  return std::vector<std::unique_ptr<RtpPacket>>{};
}

void VideoChannelSend::Destroy() {
  if (rtp_video_sender_) {
    rtp_video_sender_->Stop();
  }
}

int VideoChannelSend::SendVideo(const EncodedFrame& encoded_frame) {
  if (rtp_video_sender_ && rtp_packetizer_ && packet_sender_) {
    int32_t rtp_timestamp =
        delta_ntp_internal_ms_ +
        static_cast<uint32_t>(encoded_frame.CapturedTimestamp() / 1000);
    std::vector<std::unique_ptr<RtpPacket>> rtp_packets =
        rtp_packetizer_->Build((uint8_t*)encoded_frame.Buffer(),
                               (uint32_t)encoded_frame.Size(), rtp_timestamp,
                               true);

#ifdef SAVE_RTP_SENT_STREAM
    fwrite((unsigned char*)encoded_frame.Buffer(), 1, encoded_frame.Size(),
           file_rtp_sent_);
#endif

    packet_sender_->EnqueueRtpPacket(std::move(rtp_packets), rtp_timestamp);
  }

  return 0;
}

int32_t VideoChannelSend::ReSendPacket(uint16_t packet_id) {
  int32_t packet_size = 0;

  std::unique_ptr<webrtc::RtpPacketToSend> packet =
      rtp_packet_history_.GetPacketAndMarkAsPending(
          packet_id, [&](const webrtc::RtpPacketToSend& stored_packet) {
            // Check if we're overusing retransmission bitrate.
            // TODO(sprang): Add histograms for nack success or failure
            // reasons.
            packet_size = stored_packet.size();
            std::unique_ptr<webrtc::RtpPacketToSend> retransmit_packet;

            retransmit_packet =
                std::make_unique<webrtc::RtpPacketToSend>(stored_packet);

            if (retransmit_packet) {
              retransmit_packet->set_retransmitted_sequence_number(
                  stored_packet.SequenceNumber());
              retransmit_packet->set_original_ssrc(stored_packet.Ssrc());
            }
            return retransmit_packet;
          });
  if (packet_size == 0) {
    // Packet not found or already queued for retransmission, ignore.
    return 0;
  }
  if (!packet) {
    // Packet was found, but lambda helper above chose not to create
    // `retransmit_packet` out of it.
    LOG_WARN("packet not found");
    return -1;
  }

  packet->set_packet_type(webrtc::RtpPacketMediaType::kRetransmission);
  packet->set_fec_protect_packet(false);
  std::vector<std::unique_ptr<webrtc::RtpPacketToSend>> packets;
  packets.emplace_back(std::move(packet));

  if (packet_sender_) {
    packet_sender_->EnqueueRtpPacket(std::move(packets));
  }

  return packet_size;
}