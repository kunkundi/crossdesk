#ifndef _VIDEO_ENCODER_FACTORY_H_
#define _VIDEO_ENCODER_FACTORY_H_

#include <memory>

#include "video_encoder.h"
class VideoEncoderFactory {
 public:
  VideoEncoderFactory();
  ~VideoEncoderFactory();

  static std::unique_ptr<VideoEncoder> CreateVideoEncoder(
      std::shared_ptr<SystemClock> clock, bool hardware_acceleration,
      bool av1_encoding);

  static bool CheckIsHardwareAccerlerationSupported();
};

#endif