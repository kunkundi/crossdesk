#ifndef _NVIDIA_VIDEO_ENCODER_H_
#define _NVIDIA_VIDEO_ENCODER_H_

#include <functional>

#include "NvEncoderCuda.h"
#include "video_encoder.h"

class NvidiaVideoEncoder : public VideoEncoder {
 public:
  NvidiaVideoEncoder();
  virtual ~NvidiaVideoEncoder();

  int Init();
  int Encode(const uint8_t* pData, int nSize,
             std::function<int(char* encoded_packets, size_t size,
                               VideoFrameType frame_type)>
                 on_encoded_image);

  virtual int OnEncodedImage(char* encoded_packets, size_t size);

  void ForceIdr();

 private:
  int index_of_GPU = 0;
  GUID codec_guid = NV_ENC_CODEC_H264_GUID;
  GUID preset_guid = NV_ENC_PRESET_P2_GUID;
  NV_ENC_TUNING_INFO tuning_info =
      NV_ENC_TUNING_INFO::NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
  int frame_width_ = 1280;
  int frame_height_ = 720;
  int keyFrameInterval_ = 3000;
  int maxBitrate_ = 1000;
  int max_payload_size_ = 3000;
  NvEncoder* encoder_ = nullptr;
  CUcontext cuda_context_ = nullptr;
  std::vector<std::vector<uint8_t>> encoded_packets_;
  unsigned char* encoded_image_ = nullptr;
  FILE* file_ = nullptr;
  unsigned char* nv12_data_ = nullptr;
  unsigned int seq_ = 0;
};

#endif