#include "speaker_capturer_wasapi.h"

#include "rd_log.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define SAVE_AUDIO_FILE 0

namespace crossdesk {

static ma_device_config device_config_;
static ma_device device_;
static ma_format format_ = ma_format_s16;
static ma_uint32 sample_rate_ = ma_standard_sample_rate_48000;
static ma_uint32 channels_ = 1;
static FILE* fp_ = nullptr;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput,
                   ma_uint32 frameCount) {
  SpeakerCapturerWasapi* ptr = (SpeakerCapturerWasapi*)pDevice->pUserData;
  if (ptr) {
    if (SAVE_AUDIO_FILE) {
      fwrite(pInput, frameCount * ma_get_bytes_per_frame(format_, channels_), 1,
             fp_);
    }

    ptr->GetCallback()((unsigned char*)pInput,
                       frameCount * ma_get_bytes_per_frame(format_, channels_),
                       "audio");
  }

  (void)pOutput;
}

SpeakerCapturerWasapi::speaker_data_cb SpeakerCapturerWasapi::GetCallback() {
  return cb_;
}

SpeakerCapturerWasapi::SpeakerCapturerWasapi() {}

SpeakerCapturerWasapi::~SpeakerCapturerWasapi() {
  if (SAVE_AUDIO_FILE) {
    fclose(fp_);
  }
}

int SpeakerCapturerWasapi::Init(speaker_data_cb cb) {
  if (inited_) {
    return 0;
  }

  cb_ = cb;

  if (SAVE_AUDIO_FILE) {
    fopen_s(&fp_, "system_audio.pcm", "wb");
  }

  ma_result result;
  ma_backend backends[] = {ma_backend_wasapi};

  device_config_ = ma_device_config_init(ma_device_type_loopback);
  device_config_.capture.pDeviceID = NULL;
  device_config_.capture.format = format_;
  device_config_.capture.channels = channels_;
  device_config_.sampleRate = sample_rate_;
  device_config_.dataCallback = data_callback;
  device_config_.pUserData = this;

  result = ma_device_init_ex(backends, sizeof(backends) / sizeof(backends[0]),
                             NULL, &device_config_, &device_);
  if (result != MA_SUCCESS) {
    LOG_ERROR("Failed to initialize loopback device");
    return -1;
  }

  inited_ = true;

  return 0;
}

int SpeakerCapturerWasapi::Start() {
  ma_result result = ma_device_start(&device_);
  if (result != MA_SUCCESS) {
    ma_device_uninit(&device_);
    LOG_ERROR("Failed to start device");
    return -1;
  }

  return 0;
}

int SpeakerCapturerWasapi::Stop() {
  ma_device_stop(&device_);
  return 0;
}

int SpeakerCapturerWasapi::Destroy() {
  ma_device_uninit(&device_);
  return 0;
}

int SpeakerCapturerWasapi::Pause() { return 0; }
}  // namespace crossdesk