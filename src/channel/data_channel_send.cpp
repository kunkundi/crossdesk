#include "data_channel_send.h"

#include "log.h"

DataChannelSend::DataChannelSend() {}

DataChannelSend::~DataChannelSend() {}

DataChannelSend::DataChannelSend(
    std::shared_ptr<IceAgent> ice_agent,
    std::shared_ptr<IOStatistics> ice_io_statistics)
    : ice_agent_(ice_agent), ice_io_statistics_(ice_io_statistics) {}

void DataChannelSend::Initialize(rtp::PAYLOAD_TYPE payload_type) {
  rtp_packetizer_ = RtpPacketizer::Create(payload_type);
  rtp_data_sender_ = std::make_unique<RtpDataSender>(ice_io_statistics_);
  rtp_data_sender_->SetSendDataFunc(
      [this](const char *data, size_t size) -> int {
        if (!ice_agent_) {
          LOG_ERROR("ice_agent_ is nullptr");
          return -1;
        }

        auto ice_state = ice_agent_->GetIceState();

        if (ice_state != NICE_COMPONENT_STATE_CONNECTED &&
            ice_state != NICE_COMPONENT_STATE_READY) {
          LOG_ERROR("Ice is not connected, state = [{}]",
                    nice_component_state_to_string(ice_state));
          return -2;
        }

        ice_io_statistics_->UpdateDataOutboundBytes((uint32_t)size);
        return ice_agent_->Send(data, size);
      });

  rtp_data_sender_->Start();
}

void DataChannelSend::Destroy() {
  if (rtp_data_sender_) {
    rtp_data_sender_->Stop();
  }
}

int DataChannelSend::SendData(const char *data, size_t size) {
  if (rtp_data_sender_ && rtp_packetizer_) {
    std::vector<RtpPacket> rtp_packets =
        rtp_packetizer_->Build((uint8_t *)data, (uint32_t)size);
    rtp_data_sender_->Enqueue(rtp_packets);
  }

  return 0;
}