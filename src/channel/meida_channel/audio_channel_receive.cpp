#include "audio_channel_receive.h"

#include "log.h"

AudioChannelReceive::AudioChannelReceive() {}

AudioChannelReceive::AudioChannelReceive(
    std::shared_ptr<IceAgent> ice_agent,
    std::shared_ptr<IOStatistics> ice_io_statistics,
    std::function<void(const char *, size_t)> on_receive_audio)
    : ice_agent_(ice_agent),
      ice_io_statistics_(ice_io_statistics),
      on_receive_audio_(on_receive_audio) {}

AudioChannelReceive::~AudioChannelReceive() {}

void AudioChannelReceive::Initialize(rtp::PAYLOAD_TYPE payload_type) {
  rtp_audio_receiver_ = std::make_unique<RtpAudioReceiver>(ice_io_statistics_);
  rtp_audio_receiver_->SetOnReceiveData(
      [this](const char *data, size_t size) -> void {
        if (on_receive_audio_) {
          on_receive_audio_(data, size);
        }
      });

  rtp_audio_receiver_->SetSendDataFunc([this](const char *data,
                                              size_t size) -> int {
    if (!ice_agent_) {
      LOG_ERROR("ice_agent_ is nullptr");
      return -1;
    }

    auto ice_state = ice_agent_->GetIceState();

    if (ICE_STATE_NULLPTR == ice_state || ICE_STATE_DESTROYED == ice_state) {
      LOG_ERROR("Ice is not connected, state = [{}]", (int)ice_state);
      return -2;
    }

    ice_io_statistics_->UpdateAudioOutboundBytes((uint32_t)size);
    return ice_agent_->Send(data, size);
  });
}

void AudioChannelReceive::Destroy() {}

int AudioChannelReceive::OnReceiveRtpPacket(const char *data, size_t size) {
  if (ice_io_statistics_) {
    ice_io_statistics_->UpdateAudioInboundBytes((uint32_t)size);
  }

  if (rtp_audio_receiver_) {
    RtpPacket rtp_packet;
    rtp_packet.Build((uint8_t *)data, (uint32_t)size);
    rtp_audio_receiver_->InsertRtpPacket(rtp_packet);
  }

  return 0;
}