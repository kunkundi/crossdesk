/*
 * @Author: DI JUNKUN
 * @Date: 2023-11-03
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _OPENH264_ENCODER_H_
#define _OPENH264_ENCODER_H_

#include <wels/codec_api.h>
#include <wels/codec_app_def.h>
#include <wels/codec_def.h>
#include <wels/codec_ver.h>

#include <functional>
#include <vector>

#include "video_encoder.h"

class OpenH264Encoder : public VideoEncoder {
 public:
  OpenH264Encoder(std::shared_ptr<SystemClock> clock);
  virtual ~OpenH264Encoder();

  int Init();

  int Encode(const XVideoFrame* video_frame,
             std::function<int(std::shared_ptr<EncodedFrame> encoded_frame)>
                 on_encoded_image);

  int ForceIdr();

  int SetTargetBitrate(int bitrate);

  int GetResolution(int* width, int* height) {
    *width = frame_width_;
    *height = frame_height_;
    return 0;
  }

  std::string GetEncoderName() { return "OpenH264"; }

 private:
  int InitEncoderParams(int width, int height);
  int ResetEncodeResolution(unsigned int width, unsigned int height);

  int Release();

 private:
  std::shared_ptr<SystemClock> clock_ = nullptr;
  uint32_t frame_width_ = 1280;
  uint32_t frame_height_ = 720;
  int key_frame_interval_ = 3000;
  int target_bitrate_ = 10000000;
  int max_bitrate_ = 10000000;
  int max_payload_size_ = 1400;
  int max_frame_rate_ = 30;
  std::vector<std::vector<uint8_t>> encoded_packets_;
  unsigned char* encoded_image_ = nullptr;
  FILE* file_h264_ = nullptr;
  FILE* file_nv12_ = nullptr;
  unsigned char* nv12_data_ = nullptr;
  unsigned int seq_ = 0;

  // openh264
  ISVCEncoder* openh264_encoder_ = nullptr;
  SEncParamExt encoder_params_;
  int video_format_;
  SSourcePicture raw_frame_;
  unsigned char* yuv420p_frame_ = nullptr;
  size_t yuv420p_frame_capacity_ = 0;
  uint8_t* encoded_frame_ = nullptr;
  size_t encoded_frame_capacity_ = 0;
  size_t encoded_frame_size_ = 0;
  bool got_output = false;
  bool is_keyframe = false;
  int temporal_ = 1;
};

#endif