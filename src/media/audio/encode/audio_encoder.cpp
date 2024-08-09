#include "audio_encoder.h"

#include <chrono>
#include <cstdlib>
#include <cstring>

#include "log.h"

#define MAX_PACKET_SIZE 4000
unsigned char output_data[MAX_PACKET_SIZE] = {0};
static uint32_t last_ts = 0;
static unsigned char out_data[MAX_PACKET_SIZE] = {0};

AudioEncoder::AudioEncoder(int sample_rate, int channel_num, int frame_size)
    : sample_rate_(sample_rate),
      channel_num_(channel_num),
      frame_size_(frame_size) {}

AudioEncoder::~AudioEncoder() {
  if (opus_encoder_) {
    opus_encoder_destroy(opus_encoder_);
  }
}

int AudioEncoder::Init() {
  last_ts = static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  int err;

  opus_encoder_ = opus_encoder_create(sample_rate_, channel_num_,
                                      OPUS_APPLICATION_VOIP, &err);

  if (err != OPUS_OK || opus_encoder_ == NULL) {
    LOG_ERROR("Create opus encoder failed");
  }

  // opus_encoder_ctl(opus_encoder_, OPUS_SET_VBR(0));
  // opus_encoder_ctl(opus_encoder_, OPUS_SET_VBR_CONSTRAINT(true));
  // opus_encoder_ctl(opus_encoder_,
  //                  OPUS_SET_BITRATE(sample_rate_ * channel_num_));
  // opus_encoder_ctl(opus_encoder_, OPUS_SET_COMPLEXITY(0));
  // opus_encoder_ctl(opus_encoder_, OPUS_SET_SIGNAL(OPUS_APPLICATION_VOIP));
  opus_encoder_ctl(opus_encoder_, OPUS_SET_LSB_DEPTH(16));
  // opus_encoder_ctl(opus_encoder_, OPUS_SET_DTX(0));
  // opus_encoder_ctl(opus_encoder_, OPUS_SET_INBAND_FEC(1));
  opus_encoder_ctl(opus_encoder_,
                   OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_10_MS));

  return 0;
}

int AudioEncoder::Encode(
    const uint8_t *data, int size,
    std::function<int(char *encoded_audio_buffer, size_t size)>
        on_encoded_audio_buffer) {
  if (!on_encoded_audio_buffer_) {
    on_encoded_audio_buffer_ = on_encoded_audio_buffer;
  }

  // uint32_t now_ts = static_cast<uint32_t>(
  //     std::chrono::duration_cast<std::chrono::milliseconds>(
  //         std::chrono::steady_clock::now().time_since_epoch())
  //         .count());

  // printf("1 Time cost: %d size: %d\n", now_ts - last_ts, size);
  // last_ts = now_ts;

  auto ret = opus_encode(opus_encoder_, (opus_int16 *)data, size, out_data,
                         MAX_PACKET_SIZE);
  if (ret < 0) {
    printf("opus decode failed, %d\n", ret);
    return -1;
  }

  if (on_encoded_audio_buffer_) {
    on_encoded_audio_buffer_((char *)out_data, ret);
  } else {
    OnEncodedAudioBuffer((char *)out_data, ret);
  }

  return 0;
}

int AudioEncoder::OnEncodedAudioBuffer(char *encoded_audio_buffer,
                                       size_t size) {
  LOG_INFO("OnEncodedAudioBuffer not implemented");
  return 0;
}
