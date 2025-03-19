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

  int Decode(const ReceivedFrame& received_frame,
             std::function<void(const DecodedFrame&)> on_receive_decoded_frame);

  std::string GetDecoderName() { return "NvidiaH264"; }

 private:
  std::shared_ptr<SystemClock> clock_ = nullptr;
  NvDecoder* decoder = nullptr;
  bool get_first_keyframe_ = false;
  bool skip_frame_ = false;

  FILE* file_h264_ = nullptr;
  FILE* file_nv12_ = nullptr;
};

#endif