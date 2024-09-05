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
  OpenH264Encoder();
  virtual ~OpenH264Encoder();

  int Init();
  int Encode(const uint8_t* pData, int nSize,
             std::function<int(char* encoded_packets, size_t size,
                               VideoFrameType frame_type)>
                 on_encoded_image);

  int Encode(const XVideoFrame* video_frame,
             std::function<int(char* encoded_packets, size_t size,
                               VideoFrameType frame_type)>
                 on_encoded_image) {
    return 0;
  }

  virtual int OnEncodedImage(char* encoded_packets, size_t size);

  void ForceIdr();

 private:
  SEncParamExt CreateEncoderParams() const;
  int Release();

 private:
  int frame_width_ = 1280;
  int frame_height_ = 720;
  int key_frame_interval_ = 300;
  int target_bitrate_ = 1000;
  int max_bitrate_ = 2500000;
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
  SSourcePicture raw_frame_;
  uint8_t* encoded_frame_ = nullptr;
  int encoded_frame_size_ = 0;
  bool got_output = false;
  bool is_keyframe = false;
  int temporal_ = 1;
};

#endif