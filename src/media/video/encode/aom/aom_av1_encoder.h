/*
 * @Author: DI JUNKUN
 * @Date: 2024-03-01
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _AOM_AV1_ENCODER_H_
#define _AOM_AV1_ENCODER_H_

#include <functional>
#include <vector>

#include "aom/aom_codec.h"
#include "aom/aom_encoder.h"
#include "aom/aomcx.h"
#include "video_encoder.h"

typedef struct {
  uint64_t obu_header_size;
  unsigned obu_type;
  uint64_t
      obu_size;  // leb128(), contains the size in bytes of the OBU not
                 // including the bytes within obu_header or the obu_size syntax
  int extension_flag;
  int has_size_field;

  // extension_flag == 1
  int temporal_id;
  int spatial_id;
} OBU_t;

class AomAv1Encoder : public VideoEncoder {
 public:
  AomAv1Encoder();
  virtual ~AomAv1Encoder();

 public:
  int Init();
  int Encode(const uint8_t* pData, int nSize,
             std::function<int(char* encoded_packets, size_t size,
                               VideoFrameType frame_type)>
                 on_encoded_image);

  virtual int OnEncodedImage(char* encoded_packets, size_t size);

  void ForceIdr();

 private:
  template <typename P>
  bool SetEncoderControlParameters(int param_id, P param_value);
  int NumberOfThreads(int width, int height, int number_of_cores);
  int GetCpuSpeed(int width, int height);

  int Release();

 private:
  int frame_width_ = 1280;
  int frame_height_ = 720;
  int key_frame_interval_ = 300;
  int target_bitrate_ = 1000;
  int max_bitrate_ = 2500000;
  int max_payload_size_ = 1400;
  int max_frame_rate_ = 30;
  int number_of_cores_ = 4;

  std::vector<std::vector<uint8_t>> encoded_packets_;
  unsigned char* encoded_image_ = nullptr;
  FILE* file_ivf_ = nullptr;
  FILE* file_nv12_ = nullptr;
  unsigned char* nv12_data_ = nullptr;
  unsigned int seq_ = 0;

  // aom av1 encoder
  aom_image_t* frame_for_encode_ = nullptr;
  aom_codec_ctx_t aom_av1_encoder_ctx_;
  aom_codec_enc_cfg_t aom_av1_encoder_config_;
  bool disable_frame_dropping_ = false;
  bool inited_ = false;
  int64_t timestamp_ = 0;
  aom_enc_frame_flags_t force_i_frame_flags_ = 0;
  uint8_t* encoded_frame_ = nullptr;
  int encoded_frame_size_ = 0;
};

#endif