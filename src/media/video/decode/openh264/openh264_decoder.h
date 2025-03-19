/*
 * @Author: DI JUNKUN
 * @Date: 2023-11-03
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _OPENH264_DECODER_H_
#define _OPENH264_DECODER_H_

#include <wels/codec_api.h>
#include <wels/codec_app_def.h>
#include <wels/codec_def.h>
#include <wels/codec_ver.h>

#include <functional>

#include "video_decoder.h"

class OpenH264Decoder : public VideoDecoder {
 public:
  OpenH264Decoder(std::shared_ptr<SystemClock> clock);
  virtual ~OpenH264Decoder();

 public:
  int Init();

  int Decode(const ReceivedFrame& received_frame,
             std::function<void(const DecodedFrame&)> on_receive_decoded_frame);

  std::string GetDecoderName() { return "OpenH264"; }

 private:
  std::shared_ptr<SystemClock> clock_ = nullptr;
  ISVCDecoder* openh264_decoder_ = nullptr;
  bool get_first_keyframe_ = false;
  bool skip_frame_ = false;
  FILE* nv12_stream_ = nullptr;
  FILE* h264_stream_ = nullptr;
  uint8_t* decoded_frame_ = nullptr;
  int decoded_frame_size_ = 0;
  uint32_t frame_width_ = 1280;
  uint32_t frame_height_ = 720;

  unsigned char* yuv420p_planes_[3] = {nullptr, nullptr, nullptr};
  unsigned char* yuv420p_frame_ = nullptr;
  int yuv420p_frame_capacity_ = 0;
  int yuv420p_frame_size_ = 0;

  DecodedFrame* nv12_frame_ = 0;
  int nv12_frame_capacity_ = 0;
  int nv12_frame_size_ = 0;
};

#endif