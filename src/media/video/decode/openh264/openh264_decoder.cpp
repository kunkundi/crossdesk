#include "openh264_decoder.h"

#include <cstring>

#include "libyuv.h"
#include "log.h"

// #define SAVE_DECODED_NV12_STREAM
// #define SAVE_RECEIVED_H264_STREAM

void CopyYuvWithStride(uint8_t *src_y, uint8_t *src_u, uint8_t *src_v,
                       int width, int height, int stride_y, int stride_u,
                       int stride_v, uint8_t *yuv420p_frame) {
  int actual_stride_y = width;
  int actual_stride_u = width / 2;
  int actual_stride_v = width / 2;

  for (int row = 0; row < height; row++) {
    memcpy(yuv420p_frame, src_y, actual_stride_y);
    src_y += stride_y;
    yuv420p_frame += actual_stride_y;
  }

  for (int row = 0; row < height / 2; row++) {
    memcpy(yuv420p_frame, src_u, actual_stride_u);
    src_u += stride_u;
    yuv420p_frame += actual_stride_u;
  }

  for (int row = 0; row < height / 2; row++) {
    memcpy(yuv420p_frame, src_v, actual_stride_v);
    src_v += stride_v;
    yuv420p_frame += actual_stride_v;
  }
}

void ConvertYuv420pToNv12(const unsigned char *yuv_data,
                          unsigned char *nv12_data, int width, int height) {
  int y_size = width * height;
  int uv_size = y_size / 4;
  const unsigned char *y_data = yuv_data;
  const unsigned char *u_data = y_data + y_size;
  const unsigned char *v_data = u_data + uv_size;

  std::memcpy(nv12_data, y_data, y_size);

  for (int i = 0; i < uv_size; i++) {
    nv12_data[y_size + i * 2] = u_data[i];
    nv12_data[y_size + i * 2 + 1] = v_data[i];
  }
}

OpenH264Decoder::OpenH264Decoder(std::shared_ptr<SystemClock> clock)
    : clock_(clock) {}
OpenH264Decoder::~OpenH264Decoder() {
  if (openh264_decoder_) {
    openh264_decoder_->Uninitialize();
    WelsDestroyDecoder(openh264_decoder_);
  }

  if (nv12_frame_) {
    delete nv12_frame_;
  }

  if (yuv420p_frame_) {
    delete[] yuv420p_frame_;
  }

#ifdef SAVE_DECODED_NV12_STREAM
  if (nv12_stream_) {
    fflush(nv12_stream_);
    nv12_stream_ = nullptr;
  }
#endif

#ifdef SAVE_RECEIVED_H264_STREAM
  if (h264_stream_) {
    fflush(h264_stream_);
    h264_stream_ = nullptr;
  }
#endif
}

int OpenH264Decoder::Init() {
#ifdef SAVE_DECODED_NV12_STREAM
  nv12_stream_ = fopen("nv12_receive_.yuv", "w+b");
  if (!nv12_stream_) {
    LOG_WARN("Fail to open nv12_receive_.yuv");
  }
#endif

#ifdef SAVE_RECEIVED_H264_STREAM
  h264_stream_ = fopen("h264_receive.h264", "w+b");
  if (!h264_stream_) {
    LOG_WARN("Fail to open h264_receive.h264");
  }
#endif

  frame_width_ = 1280;
  frame_height_ = 720;

  if (WelsCreateDecoder(&openh264_decoder_) != 0) {
    LOG_ERROR("Failed to create OpenH264 decoder");
    return -1;
  }

  SDecodingParam sDecParam;

  memset(&sDecParam, 0, sizeof(SDecodingParam));
  sDecParam.uiTargetDqLayer = UCHAR_MAX;
  sDecParam.eEcActiveIdc = ERROR_CON_SLICE_COPY;
  sDecParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;

  openh264_decoder_->Initialize(&sDecParam);

  int trace_level = WELS_LOG_QUIET;
  openh264_decoder_->SetOption(DECODER_OPTION_TRACE_LEVEL, &trace_level);

  return 0;
}

int OpenH264Decoder::Decode(
    const ReceivedFrame &received_frame,
    std::function<void(const DecodedFrame &)> on_receive_decoded_frame) {
  if (!openh264_decoder_) {
    return -1;
  }

  const uint8_t *data = received_frame.Buffer();
  size_t size = received_frame.Size();

#ifdef SAVE_RECEIVED_H264_STREAM
  fwrite((unsigned char *)data, 1, size, h264_stream_);
#endif

  if ((*(data + 4) & 0x1f) == 0x07) {
    // LOG_WARN("Receive key frame");
  }

  SBufferInfo sDstBufInfo;
  memset(&sDstBufInfo, 0, sizeof(SBufferInfo));

  openh264_decoder_->DecodeFrameNoDelay(data, (int)size, yuv420p_planes_,
                                        &sDstBufInfo);

  frame_width_ = sDstBufInfo.UsrData.sSystemBuffer.iWidth;
  frame_height_ = sDstBufInfo.UsrData.sSystemBuffer.iHeight;
  yuv420p_frame_size_ = frame_width_ * frame_height_ * 3 / 2;
  nv12_frame_size_ = frame_width_ * frame_height_ * 3 / 2;

  if (!yuv420p_frame_) {
    yuv420p_frame_capacity_ = yuv420p_frame_size_;
    yuv420p_frame_ = new unsigned char[yuv420p_frame_capacity_];
  }

  if (yuv420p_frame_capacity_ < yuv420p_frame_size_) {
    yuv420p_frame_capacity_ = yuv420p_frame_size_;
    delete[] yuv420p_frame_;
    yuv420p_frame_ = new unsigned char[yuv420p_frame_capacity_];
  }

  if (!nv12_frame_) {
    nv12_frame_capacity_ = yuv420p_frame_size_;
    nv12_frame_ =
        new DecodedFrame(nv12_frame_capacity_, frame_width_, frame_height_);
  }

  if (nv12_frame_capacity_ < yuv420p_frame_size_) {
    nv12_frame_capacity_ = yuv420p_frame_size_;
    delete nv12_frame_;
    nv12_frame_ =
        new DecodedFrame(nv12_frame_capacity_, frame_width_, frame_height_);
  }

  if (nv12_frame_->Size() != nv12_frame_size_ ||
      nv12_frame_->Width() != frame_width_ ||
      nv12_frame_->Height() != frame_height_) {
    nv12_frame_->SetSize(nv12_frame_size_);
    nv12_frame_->SetWidth(frame_width_);
    nv12_frame_->SetHeight(frame_height_);
  }

  if (sDstBufInfo.iBufferStatus == 1) {
    if (on_receive_decoded_frame) {
      CopyYuvWithStride(
          yuv420p_planes_[0], yuv420p_planes_[1], yuv420p_planes_[2],
          sDstBufInfo.UsrData.sSystemBuffer.iWidth,
          sDstBufInfo.UsrData.sSystemBuffer.iHeight,
          sDstBufInfo.UsrData.sSystemBuffer.iStride[0],
          sDstBufInfo.UsrData.sSystemBuffer.iStride[1],
          sDstBufInfo.UsrData.sSystemBuffer.iStride[1], yuv420p_frame_);

      if (0) {
        ConvertYuv420pToNv12(yuv420p_frame_,
                             (unsigned char *)nv12_frame_->Buffer(),
                             frame_width_, frame_height_);
      } else {
        libyuv::I420ToNV12(
            (const uint8_t *)yuv420p_frame_, frame_width_,
            (const uint8_t *)yuv420p_frame_ + frame_width_ * frame_height_,
            frame_width_ / 2,
            (const uint8_t *)yuv420p_frame_ +
                frame_width_ * frame_height_ * 5 / 4,
            frame_width_ / 2, (uint8_t *)nv12_frame_->Buffer(), frame_width_,
            (uint8_t *)nv12_frame_->Buffer() + frame_width_ * frame_height_,
            frame_width_, frame_width_, frame_height_);
      }

      nv12_frame_->SetReceivedTimestamp(received_frame.ReceivedTimestamp());
      nv12_frame_->SetCapturedTimestamp(received_frame.CapturedTimestamp());
      nv12_frame_->SetDecodedTimestamp(clock_->CurrentTime());
      on_receive_decoded_frame(*nv12_frame_);

#ifdef SAVE_DECODED_NV12_STREAM
      fwrite((unsigned char *)nv12_frame_->Buffer(), 1, nv12_frame_->Size(),
             nv12_stream_);
#endif
    }
  }

  return 0;
}