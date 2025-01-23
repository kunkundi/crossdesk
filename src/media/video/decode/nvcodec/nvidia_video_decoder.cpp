#include "nvidia_video_decoder.h"

#include "log.h"
#include "nvcodec_api.h"

// #define SAVE_DECODED_NV12_STREAM
#define SAVE_RECEIVED_H264_STREAM

NvidiaVideoDecoder::NvidiaVideoDecoder() {}
NvidiaVideoDecoder::~NvidiaVideoDecoder() {
#ifdef SAVE_DECODED_NV12_STREAM
  if (file_nv12_) {
    fflush(file_nv12_);
    fclose(file_nv12_);
    file_nv12_ = nullptr;
  }
#endif

#ifdef SAVE_RECEIVED_H264_STREAM
  if (file_h264_) {
    fflush(file_h264_);
    fclose(file_h264_);
    file_h264_ = nullptr;
  }
#endif
}

int NvidiaVideoDecoder::Init() {
  ck(cuInit_ld(0));
  int nGpu = 0;
  int iGpu = 0;

  ck(cuDeviceGetCount_ld(&nGpu));
  if (nGpu < 1) {
    return -1;
  }

  CUdevice cuDevice;
  cuDeviceGet_ld(&cuDevice, iGpu);

  CUcontext cuContext = NULL;
  cuCtxCreate_ld(&cuContext, 0, cuDevice);
  if (!cuContext) {
    return -1;
  }

  decoder = new NvDecoder(cuContext, false, cudaVideoCodec_H264, true);

#ifdef SAVE_DECODED_NV12_STREAM
  file_nv12_ = fopen("decoded_nv12_stream.yuv", "w+b");
  if (!file_nv12_) {
    LOG_WARN("Fail to open decoded_nv12_stream.yuv");
  }
#endif

#ifdef SAVE_RECEIVED_H264_STREAM
  file_h264_ = fopen("received_h264_stream.h264", "w+b");
  if (!file_h264_) {
    LOG_WARN("Fail to open received_h264_stream.h264");
  }
#endif

  return 0;
}

int NvidiaVideoDecoder::Decode(
    const uint8_t *data, size_t size,
    std::function<void(VideoFrame)> on_receive_decoded_frame) {
  if (!decoder) {
    return -1;
  }
#ifdef SAVE_RECEIVED_H264_STREAM
  fwrite((unsigned char *)data, 1, size, file_h264_);
#endif

  if ((*(data + 4) & 0x1f) == 0x07) {
    // LOG_WARN("Receive key frame");
  }

  int num_frame_returned = decoder->Decode(data, (int)size);
  for (size_t i = 0; i < num_frame_returned; ++i) {
    cudaVideoSurfaceFormat format = decoder->GetOutputFormat();
    if (format == cudaVideoSurfaceFormat_NV12) {
      uint8_t *decoded_frame_buffer = nullptr;
      decoded_frame_buffer = decoder->GetFrame();
      if (decoded_frame_buffer) {
        if (on_receive_decoded_frame) {
          VideoFrame decoded_frame(
              decoded_frame_buffer,
              decoder->GetWidth() * decoder->GetHeight() * 3 / 2,
              decoder->GetWidth(), decoder->GetHeight());
          on_receive_decoded_frame(decoded_frame);
#ifdef SAVE_DECODED_NV12_STREAM
          fwrite((unsigned char *)decoded_frame.Buffer(), 1,
                 decoded_frame.Size(), file_nv12_);
#endif
        }
      }
    }
  }

  return -1;
}