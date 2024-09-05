#ifndef _VIDEO_ENCODER_H_
#define _VIDEO_ENCODER_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>

#include "x.h"

class VideoEncoder {
 public:
  enum VideoFrameType {
    kEmptyFrame = 0,
    kVideoFrameKey = 3,
    kVideoFrameDelta = 4,
  };

 public:
  virtual int Init() = 0;
  virtual int Encode(const uint8_t* pData, int nSize,
                     std::function<int(char* encoded_packets, size_t size,
                                       VideoFrameType frame_type)>
                         on_encoded_image) = 0;

  virtual int Encode(const XVideoFrame* video_frame,
                     std::function<int(char* encoded_packets, size_t size,
                                       VideoFrameType frame_type)>
                         on_encoded_image) = 0;

  virtual int OnEncodedImage(char* encoded_packets, size_t size) = 0;
  virtual void ForceIdr() = 0;

  VideoEncoder() = default;
  virtual ~VideoEncoder() {}
};

#endif