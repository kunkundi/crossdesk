#ifndef _NVIDIA_VIDEO_DECODER_H_
#define _NVIDIA_VIDEO_DECODER_H_

#include <functional>

#include "NvDecoder.h"
#include "video_decoder.h"

class NvidiaVideoDecoder : public VideoDecoder {
 public:
  NvidiaVideoDecoder(std::shared_ptr<SystemClock> clock);
  virtual ~NvidiaVideoDecoder();

 public:
  int Init();

  int Decode(std::unique_ptr<ReceivedFrame> received_frame,
             std::function<void(const DecodedFrame&)> on_receive_decoded_frame);

  std::string GetDecoderName() { return "NvidiaH264"; }

 private:
  std::shared_ptr<SystemClock> clock_ = nullptr;
  NvDecoder* decoder = nullptr;
  CUcontext cuda_context_ = NULL;
  CUdevice cuda_device_ = 0;
  DecodedFrame* decoded_frame_ = nullptr;
  bool get_first_keyframe_ = false;
  bool skip_frame_ = false;
  uint32_t frame_width_ = 1280;
  uint32_t frame_height_ = 720;

  FILE* file_h264_ = nullptr;
  FILE* file_nv12_ = nullptr;
};

#endif