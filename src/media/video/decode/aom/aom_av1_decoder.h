/*
 * @Author: DI JUNKUN
 * @Date: 2024-09-25
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _AOM_AV1_DECODER_H_
#define _AOM_AV1_DECODER_H_

#include <functional>

#include "aom/aom_codec.h"
#include "aom/aom_decoder.h"
#include "aom/aomdx.h"
#include "video_decoder.h"

class AomAv1Decoder : public VideoDecoder {
 public:
  AomAv1Decoder(std::shared_ptr<SystemClock> clock);
  virtual ~AomAv1Decoder();

 public:
  int Init();

  int Decode(
      const ReceivedFrame &received_frame,
      std::function<void(const DecodedFrame &)> on_receive_decoded_frame);

  std::string GetDecoderName() { return "AomAv1"; }

 private:
  std::shared_ptr<SystemClock> clock_ = nullptr;
  DecodedFrame *nv12_frame_ = 0;
  int nv12_frame_capacity_ = 0;
  int nv12_frame_size_ = 0;

  uint32_t frame_width_ = 0;
  uint32_t frame_height_ = 0;

  FILE *file_av1_ = nullptr;
  FILE *file_nv12_ = nullptr;

  bool first_ = false;

  // aom av1 decoder
  aom_codec_ctx_t aom_av1_decoder_ctx_;
  aom_codec_dec_cfg_t aom_av1_decoder_config_;
  aom_image_t *img_ = nullptr;
};

#endif