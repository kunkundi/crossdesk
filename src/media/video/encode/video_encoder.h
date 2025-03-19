#ifndef _VIDEO_ENCODER_H_
#define _VIDEO_ENCODER_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>

#include "clock/system_clock.h"
#include "encoded_frame.h"
#include "x.h"

#define I_FRAME_INTERVAL 3000
class VideoEncoder {
 public:
  virtual int Init() = 0;

  virtual int Encode(
      const XVideoFrame* video_frame,
      std::function<int(std::shared_ptr<EncodedFrame> encoded_frame)>
          on_encoded_image) = 0;

  virtual int ForceIdr() = 0;

  virtual int SetTargetBitrate(int bitrate) = 0;

  virtual int GetResolution(int* width, int* height) = 0;

  virtual std::string GetEncoderName() = 0;

  VideoEncoder() = default;
  virtual ~VideoEncoder() {}
};

#endif