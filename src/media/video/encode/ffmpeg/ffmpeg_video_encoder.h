#ifndef _FFMPEG_VIDEO_ENCODER_H_
#define _FFMPEG_VIDEO_ENCODER_H_

#ifdef _WIN32
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libswscale/swscale.h"
}
#else
#ifdef __cplusplus
extern "C" {
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
};
#ifdef __cplusplus
};
#endif
#endif
#include <functional>
#include <vector>

#include "video_encoder.h"

class FFmpegVideoEncoder : public VideoEncoder {
 public:
  FFmpegVideoEncoder();
  virtual ~FFmpegVideoEncoder();

  int Init();
  int Encode(const uint8_t* pData, int nSize,
             std::function<int(char* encoded_packets, size_t size,
                               VideoFrameType frame_type)>
                 on_encoded_image);

  virtual int OnEncodedImage(char* encoded_packets, size_t size);

  void ForceIdr();

 private:
  int frame_width_ = 1280;
  int frame_height = 720;
  int keyFrameInterval_ = 3000;
  int maxBitrate_ = 1000;
  int fps_ = 30;
  int max_payload_size_ = 3000;

  std::vector<std::vector<uint8_t>> encoded_packets_;
  unsigned char* encoded_image_ = nullptr;
  FILE* file_h264_ = nullptr;
  FILE* file_nv12_ = nullptr;
  unsigned char* nv12_data_ = nullptr;
  unsigned int seq_ = 0;
  bool use_openh264_ = false;

  const AVCodec* codec_ = nullptr;
  AVCodecContext* codec_ctx_ = nullptr;
  AVFrame* frame_ = nullptr;
  AVPacket* packet_ = nullptr;
  bool got_output_ = false;
  uint32_t pts_ = 0;
};

#endif