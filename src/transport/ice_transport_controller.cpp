#include "ice_transport_controller.h"

#include "video_frame_wrapper.h"
#if __APPLE__
#else
#include "nvcodec_api.h"
#endif

IceTransportController::IceTransportController(
    std::shared_ptr<SystemClock> clock)
    : clock_(clock),
      b_force_i_frame_(true),
      video_codec_inited_(false),
      audio_codec_inited_(false),
      load_nvcodec_dll_success_(false),
      hardware_acceleration_(false) {}

IceTransportController::~IceTransportController() {
  user_data_ = nullptr;
  video_codec_inited_ = false;
  audio_codec_inited_ = false;
  load_nvcodec_dll_success_ = false;

#ifdef __APPLE__
#else
  if (hardware_acceleration_ && load_nvcodec_dll_success_) {
    ReleaseNvCodecDll();
  }
#endif
}

void IceTransportController::Create(
    std::string remote_user_id, rtp::PAYLOAD_TYPE video_codec_payload_type,
    bool hardware_acceleration, std::shared_ptr<IceAgent> ice_agent,
    std::shared_ptr<IOStatistics> ice_io_statistics,
    OnReceiveVideo on_receive_video, OnReceiveAudio on_receive_audio,
    OnReceiveData on_receive_data, void* user_data) {
  remote_user_id_ = remote_user_id;
  on_receive_video_ = on_receive_video;
  on_receive_audio_ = on_receive_audio;
  on_receive_data_ = on_receive_data;
  user_data_ = user_data;

  CreateVideoCodec(video_codec_payload_type, hardware_acceleration);
  CreateAudioCodec();

  controller_ = std::make_unique<CongestionControl>();

  video_channel_send_ = std::make_unique<VideoChannelSend>(
      clock_, ice_agent, ice_io_statistics,
      [this](const webrtc::RtpPacketToSend& packet) {
        OnSentRtpPacket(packet);
      });
  audio_channel_send_ =
      std::make_unique<AudioChannelSend>(ice_agent, ice_io_statistics);
  data_channel_send_ =
      std::make_unique<DataChannelSend>(ice_agent, ice_io_statistics);

  video_channel_send_->Initialize(video_codec_payload_type);
  audio_channel_send_->Initialize(rtp::PAYLOAD_TYPE::OPUS);
  data_channel_send_->Initialize(rtp::PAYLOAD_TYPE::DATA);

  std::weak_ptr<IceTransportController> weak_self = shared_from_this();
  video_channel_receive_ = std::make_unique<VideoChannelReceive>(
      clock_, ice_agent, ice_io_statistics,
      [this, weak_self](VideoFrame& video_frame) {
        if (auto self = weak_self.lock()) {
          OnReceiveCompleteFrame(video_frame);
        }
      });

  audio_channel_receive_ = std::make_unique<AudioChannelReceive>(
      ice_agent, ice_io_statistics,
      [this, weak_self](const char* data, size_t size) {
        if (auto self = weak_self.lock()) {
          OnReceiveCompleteAudio(data, size);
        }
      });

  data_channel_receive_ = std::make_unique<DataChannelReceive>(
      ice_agent, ice_io_statistics,
      [this, weak_self](const char* data, size_t size) {
        if (auto self = weak_self.lock()) {
          OnReceiveCompleteData(data, size);
        }
      });

  video_channel_receive_->Initialize(video_codec_payload_type);
  audio_channel_receive_->Initialize(rtp::PAYLOAD_TYPE::OPUS);
  data_channel_receive_->Initialize(rtp::PAYLOAD_TYPE::DATA);
}

void IceTransportController::Destroy() {
  if (video_channel_send_) {
    video_channel_send_->Destroy();
  }

  if (audio_channel_send_) {
    audio_channel_send_->Destroy();
  }

  if (data_channel_send_) {
    data_channel_send_->Destroy();
  }

  if (video_channel_receive_) {
    video_channel_receive_->Destroy();
  }

  if (audio_channel_receive_) {
    audio_channel_receive_->Destroy();
  }

  if (data_channel_receive_) {
    data_channel_receive_->Destroy();
  }
}

int IceTransportController::SendVideo(const XVideoFrame* video_frame) {
  if (!video_encoder_) {
    LOG_ERROR("Video Encoder not created");
    return -1;
  }

  if (b_force_i_frame_) {
    video_encoder_->ForceIdr();
    LOG_INFO("Force I frame");
    b_force_i_frame_ = false;
  }

  int ret = video_encoder_->Encode(
      video_frame,
      [this](std::shared_ptr<VideoFrameWrapper> encoded_frame) -> int {
        if (video_channel_send_) {
          video_channel_send_->SendVideo(encoded_frame);
        }

        return 0;
      });

  if (0 != ret) {
    LOG_ERROR("Encode failed");
    return -1;
  } else {
    return 0;
  }
}

int IceTransportController::SendAudio(const char* data, size_t size) {
  if (!audio_encoder_) {
    LOG_ERROR("Audio Encoder not created");
    return -1;
  }

  int ret = audio_encoder_->Encode(
      (uint8_t*)data, size,
      [this](char* encoded_audio_buffer, size_t size) -> int {
        if (audio_channel_send_) {
          audio_channel_send_->SendAudio(encoded_audio_buffer, size);
        }

        return 0;
      });

  return ret;
}

int IceTransportController::SendData(const char* data, size_t size) {
  if (data_channel_send_) {
    data_channel_send_->SendData(data, size);
  }

  return 0;
}

int IceTransportController::OnReceiveVideoRtpPacket(const char* data,
                                                    size_t size) {
  if (video_channel_receive_) {
    return video_channel_receive_->OnReceiveRtpPacket(data, size);
  }

  return -1;
}

int IceTransportController::OnReceiveAudioRtpPacket(const char* data,
                                                    size_t size) {
  if (audio_channel_receive_) {
    return audio_channel_receive_->OnReceiveRtpPacket(data, size);
  }

  return -1;
}

int IceTransportController::OnReceiveDataRtpPacket(const char* data,
                                                   size_t size) {
  if (data_channel_receive_) {
    return data_channel_receive_->OnReceiveRtpPacket(data, size);
  }

  return -1;
}

void IceTransportController::OnReceiveCompleteFrame(VideoFrame& video_frame) {
  int num_frame_returned = video_decoder_->Decode(
      (uint8_t*)video_frame.Buffer(), video_frame.Size(),
      [this](VideoFrame video_frame) {
        if (on_receive_video_) {
          XVideoFrame x_video_frame;
          x_video_frame.data = (const char*)video_frame.Buffer();
          x_video_frame.width = video_frame.Width();
          x_video_frame.height = video_frame.Height();
          x_video_frame.size = video_frame.Size();
          on_receive_video_(&x_video_frame, remote_user_id_.data(),
                            remote_user_id_.size(), user_data_);
        }
      });
}

void IceTransportController::OnReceiveCompleteAudio(const char* data,
                                                    size_t size) {
  int num_frame_returned = audio_decoder_->Decode(
      (uint8_t*)data, size, [this](uint8_t* data, int size) {
        if (on_receive_audio_) {
          on_receive_audio_((const char*)data, size, remote_user_id_.data(),
                            remote_user_id_.size(), user_data_);
        }
      });
}

void IceTransportController::OnReceiveCompleteData(const char* data,
                                                   size_t size) {
  if (on_receive_data_) {
    on_receive_data_(data, size, remote_user_id_.data(), remote_user_id_.size(),
                     user_data_);
  }
}

int IceTransportController::CreateVideoCodec(rtp::PAYLOAD_TYPE video_pt,
                                             bool hardware_acceleration) {
  if (video_codec_inited_) {
    return 0;
  }

  hardware_acceleration_ = hardware_acceleration;

  if (rtp::PAYLOAD_TYPE::AV1 == video_pt) {
    if (hardware_acceleration_) {
      hardware_acceleration_ = false;
      LOG_WARN("Only support software codec for AV1");
    }
    video_encoder_ = VideoEncoderFactory::CreateVideoEncoder(false, true);
    video_decoder_ = VideoDecoderFactory::CreateVideoDecoder(false, true);
  } else if (rtp::PAYLOAD_TYPE::H264 == video_pt) {
#ifdef __APPLE__
    if (hardware_acceleration_) {
      hardware_acceleration_ = false;
      LOG_WARN(
          "MacOS not support hardware acceleration, use default software "
          "codec");
      video_encoder_ = VideoEncoderFactory::CreateVideoEncoder(false, false);
      video_decoder_ = VideoDecoderFactory::CreateVideoDecoder(false, false);
    } else {
      video_encoder_ = VideoEncoderFactory::CreateVideoEncoder(false, false);
      video_decoder_ = VideoDecoderFactory::CreateVideoDecoder(false, false);
    }
#else
    if (hardware_acceleration_) {
      if (0 == LoadNvCodecDll()) {
        load_nvcodec_dll_success_ = true;
        video_encoder_ = VideoEncoderFactory::CreateVideoEncoder(true, false);
        video_decoder_ = VideoDecoderFactory::CreateVideoDecoder(true, false);
      } else {
        LOG_WARN(
            "Hardware accelerated codec not available, use default software "
            "codec");
        video_encoder_ = VideoEncoderFactory::CreateVideoEncoder(false, false);
        video_decoder_ = VideoDecoderFactory::CreateVideoDecoder(false, false);
      }
    } else {
      video_encoder_ = VideoEncoderFactory::CreateVideoEncoder(false, false);
      video_decoder_ = VideoDecoderFactory::CreateVideoDecoder(false, false);
    }
#endif
  }

  if (!video_encoder_) {
    video_encoder_ = VideoEncoderFactory::CreateVideoEncoder(false, false);
    LOG_ERROR("Create encoder failed, try to use software H.264 encoder");
  }
  if (!video_encoder_ || 0 != video_encoder_->Init()) {
    LOG_ERROR("Encoder init failed");
    return -1;
  }

  if (!video_decoder_) {
    video_decoder_ = VideoDecoderFactory::CreateVideoDecoder(false, false);
    LOG_ERROR("Create decoder failed, try to use software H.264 decoder");
  }
  if (!video_decoder_ || video_decoder_->Init()) {
    LOG_ERROR("Decoder init failed");
    return -1;
  }

  video_codec_inited_ = true;
  LOG_INFO("Create video codec [{}|{}] finish",
           video_encoder_->GetEncoderName(), video_decoder_->GetDecoderName());

  return 0;
}

int IceTransportController::CreateAudioCodec() {
  if (audio_codec_inited_) {
    return 0;
  }

  audio_encoder_ = std::make_unique<AudioEncoder>(AudioEncoder(48000, 1, 480));
  if (!audio_encoder_ || 0 != audio_encoder_->Init()) {
    LOG_ERROR("Audio encoder init failed");
    return -1;
  }

  audio_decoder_ = std::make_unique<AudioDecoder>(AudioDecoder(48000, 1, 480));
  if (!audio_decoder_ || 0 != audio_decoder_->Init()) {
    LOG_ERROR("Audio decoder init failed");
    return -1;
  }

  audio_codec_inited_ = true;
  LOG_INFO("Create audio codec [{}|{}] finish",
           audio_encoder_->GetEncoderName(), audio_decoder_->GetDecoderName());

  return 0;
}

void IceTransportController::OnSenderReport(const SenderReport& sender_report) {
  video_channel_receive_->OnSenderReport(sender_report);
  audio_channel_receive_->OnSenderReport(sender_report);
  data_channel_receive_->OnSenderReport(sender_report);
}

void IceTransportController::OnCongestionControlFeedback(
    const webrtc::rtcp::CongestionControlFeedback& feedback) {
  std::optional<webrtc::TransportPacketsFeedback> feedback_msg =
      transport_feedback_adapter_.ProcessCongestionControlFeedback(
          feedback, Timestamp::Micros(clock_->CurrentTimeUs()));
  if (feedback_msg) {
    HandleTransportPacketsFeedback(*feedback_msg);
  }
}

void IceTransportController::HandleTransportPacketsFeedback(
    const webrtc::TransportPacketsFeedback& feedback) {
  if (controller_)
    PostUpdates(controller_->OnTransportPacketsFeedback(feedback));

  UpdateCongestedState();
}

void IceTransportController::OnSentRtpPacket(
    const webrtc::RtpPacketToSend& packet) {
  webrtc::PacedPacketInfo pacing_info;
  size_t transport_overhead_bytes_per_packet_ = 0;
  webrtc::Timestamp creation_time =
      webrtc::Timestamp::Millis(clock_->CurrentTimeMs());
  transport_feedback_adapter_.AddPacket(
      packet, pacing_info, transport_overhead_bytes_per_packet_, creation_time);

  rtc::SentPacket sent_packet;
  sent_packet.packet_id = packet.transport_sequence_number().value();
  sent_packet.send_time_ms = clock_->CurrentTimeMs();
  sent_packet.info.included_in_feedback = true;
  sent_packet.info.included_in_allocation = true;
  sent_packet.info.packet_size_bytes = packet.size();
  sent_packet.info.packet_type = rtc::PacketType::kData;

  transport_feedback_adapter_.ProcessSentPacket(sent_packet);
}

void IceTransportController::PostUpdates(webrtc::NetworkControlUpdate update) {
  // UpdateControlState();

  target_bitrate_ = update.target_rate.has_value()
                        ? update.target_rate->target_rate.bps()
                        : 0;
  // LOG_WARN("Target bitrate [{}]bps", target_bitrate_);
  video_encoder_->SetTargetBitrate(target_bitrate_);
}

void IceTransportController::UpdateControlState() {
  if (controller_) {
  }
}

void IceTransportController::UpdateCongestedState() {
  if (controller_) {
  }
}
