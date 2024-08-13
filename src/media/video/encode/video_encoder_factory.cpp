#include "video_encoder_factory.h"

#if __APPLE__
#include "aom/aom_av1_encoder.h"
#include "openh264/openh264_encoder.h"
#else
#include "aom/aom_av1_encoder.h"
#include "nvcodec/nvidia_video_encoder.h"
#include "nvcodec_api.h"
#include "openh264/openh264_encoder.h"
#endif

#include "log.h"

VideoEncoderFactory::VideoEncoderFactory() {}

VideoEncoderFactory::~VideoEncoderFactory() {}

std::unique_ptr<VideoEncoder> VideoEncoderFactory::CreateVideoEncoder(
    bool hardware_acceleration, bool av1_encoding) {
  if (av1_encoding) {
    LOG_INFO("Use AOM encoder");
    return std::make_unique<AomAv1Encoder>(AomAv1Encoder());
  } else {
#if __APPLE__
    return std::make_unique<OpenH264Encoder>(OpenH264Encoder());
#else
    if (hardware_acceleration) {
      if (CheckIsHardwareAccerlerationSupported()) {
        LOG_INFO("Use Nvidia encoder");
        return std::make_unique<NvidiaVideoEncoder>(NvidiaVideoEncoder());
      } else {
        return nullptr;
      }
    } else {
      LOG_INFO("Use OpenH264 encoder");
      return std::make_unique<OpenH264Encoder>(OpenH264Encoder());
    }
#endif
  }
}

bool VideoEncoderFactory::CheckIsHardwareAccerlerationSupported() {
#if __APPLE__
  return false;
#else
  CUresult cuResult;
  NV_ENCODE_API_FUNCTION_LIST functionList = {NV_ENCODE_API_FUNCTION_LIST_VER};

  cuResult = cuInit_ld(0);
  if (cuResult != CUDA_SUCCESS) {
    LOG_WARN(
        "System not support hardware accelerated encode, use default software "
        "encoder");
    return false;
  }

  NVENCSTATUS nvEncStatus = NvEncodeAPICreateInstance_ld(&functionList);
  if (nvEncStatus != NV_ENC_SUCCESS) {
    LOG_WARN(
        "System not support hardware accelerated encode, use default software "
        "encoder");
    return false;
  }

  return true;
#endif
}