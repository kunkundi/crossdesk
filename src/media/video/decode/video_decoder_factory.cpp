#include "video_decoder_factory.h"

#if __APPLE__
#include "dav1d/dav1d_av1_decoder.h"
#include "openh264/openh264_decoder.h"
#else
#include "dav1d/dav1d_av1_decoder.h"
#include "nvcodec/nvidia_video_decoder.h"
#include "nvcodec_api.h"
#include "openh264/openh264_decoder.h"
#endif

#include "log.h"

VideoDecoderFactory::VideoDecoderFactory() {}

VideoDecoderFactory::~VideoDecoderFactory() {}

std::unique_ptr<VideoDecoder> VideoDecoderFactory::CreateVideoDecoder(
    bool hardware_acceleration, bool av1_encoding) {
  if (av1_encoding) {
    LOG_INFO("Use dav1d decoder");
    return std::make_unique<Dav1dAv1Decoder>(Dav1dAv1Decoder());
  } else {
#if __APPLE__
    return std::make_unique<OpenH264Decoder>(OpenH264Decoder());
#else
    if (hardware_acceleration) {
      if (CheckIsHardwareAccerlerationSupported()) {
        LOG_INFO("Use nvidia decoder");
        return std::make_unique<NvidiaVideoDecoder>(NvidiaVideoDecoder());
      } else {
        return nullptr;
      }
    } else {
      LOG_INFO("Use openh264 decoder");
      return std::make_unique<OpenH264Decoder>(OpenH264Decoder());
    }
#endif
  }
}

bool VideoDecoderFactory::CheckIsHardwareAccerlerationSupported() {
#if __APPLE__
  return false;
#else
  CUresult cuResult;
  CUvideoctxlock cudaCtxLock;
  cuResult = cuvidCtxLockCreate_ld(&cudaCtxLock, 0);
  if (cuResult != CUDA_SUCCESS) {
    LOG_WARN(
        "System not support hardware accelerated decode, use default software "
        "decoder");
    return false;
  }

  return true;
#endif
}