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

  int Encode(const XVideoFrame* video_frame,
             std::function<int(char* encoded_packets, size_t size,
                               VideoFrameType frame_type)>
                 on_encoded_image);

  int ForceIdr();

  int SetTargetBitrate(int bitrate);

  std::string GetEncoderName() { return "NvidiaH264"; }

 private:
  int ResetEncodeResolution(unsigned int width, unsigned int height);

 private:
  int index_of_gpu_ = 0;
  CUdevice cuda_device_ = 0;

  GUID codec_guid_ = NV_ENC_CODEC_H264_GUID;
  GUID preset_guid_ = NV_ENC_PRESET_P3_GUID;
  NV_ENC_TUNING_INFO tuning_info_ =
      NV_ENC_TUNING_INFO::NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
  NV_ENC_BUFFER_FORMAT buffer_format_ =
      NV_ENC_BUFFER_FORMAT::NV_ENC_BUFFER_FORMAT_NV12;

  uint32_t frame_width_max_ = 0;
  uint32_t frame_height_max_ = 0;
  uint32_t frame_width_min_ = 0;
  uint32_t frame_height_min_ = 0;
  uint32_t encode_level_max_ = 0;
  uint32_t encode_level_min_ = 0;
  bool support_dynamic_resolution_ = false;
  bool support_dynamic_bitrate_ = false;

  uint32_t frame_width_ = 1280;
  uint32_t frame_height_ = 720;
  uint32_t key_frame_interval_ = 3000;
  uint32_t average_bitrate_ = 2000000;
  uint32_t max_bitrate_ = 10000000;
  uint32_t fps_ = 30;
  int max_payload_size_ = 3000;
  NvEncoder* encoder_ = nullptr;
  CUcontext cuda_context_ = nullptr;
  std::vector<std::vector<uint8_t>> encoded_packets_;
  unsigned char* encoded_image_ = nullptr;
  FILE* file_nv12_ = nullptr;
  FILE* file_h264_ = nullptr;
  unsigned char* nv12_data_ = nullptr;
  unsigned int seq_ = 0;
};

#endif