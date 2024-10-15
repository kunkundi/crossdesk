#include "dav1d_av1_decoder.h"

#include "log.h"

#define SAVE_RECEIVED_AV1_STREAM 0
#define SAVE_DECODED_NV12_STREAM 1

#include "libyuv.h"

class ScopedDav1dPicture : public std::shared_ptr<ScopedDav1dPicture> {
 public:
  ~ScopedDav1dPicture() { dav1d_picture_unref(&picture_); }

  Dav1dPicture &Picture() { return picture_; }

 private:
  Dav1dPicture picture_ = {};
};

class ScopedDav1dData {
 public:
  ~ScopedDav1dData() { dav1d_data_unref(&data_); }

  Dav1dData &Data() { return data_; }

 private:
  Dav1dData data_ = {};
};

// Calling `dav1d_data_wrap` requires a `free_callback` to be registered.
void NullFreeCallback(const uint8_t *buffer, void *opaque) {}

void Yuv420pToNv12(unsigned char *SrcY, unsigned char *SrcU,
                   unsigned char *SrcV, int y_stride, int uv_stride,
                   unsigned char *Dst, int Width, int Height) {
  libyuv::I420ToNV12(SrcY, y_stride, SrcU, uv_stride, SrcV, uv_stride, Dst,
                     Width, Dst + Width * Height, Width / 2, Width, Height);
}

Dav1dAv1Decoder::Dav1dAv1Decoder() {}

Dav1dAv1Decoder::~Dav1dAv1Decoder() {
  if (SAVE_RECEIVED_AV1_STREAM && file_av1_) {
    fflush(file_av1_);
    fclose(file_av1_);
    file_av1_ = nullptr;
  }

  if (SAVE_DECODED_NV12_STREAM && file_nv12_) {
    fflush(file_nv12_);
    fclose(file_nv12_);
    file_nv12_ = nullptr;
  }

  if (nv12_frame_) {
    delete nv12_frame_;
    nv12_frame_ = nullptr;
  }
}

int Dav1dAv1Decoder::Init() {
  Dav1dSettings s;
  dav1d_default_settings(&s);

  s.n_threads = std::max(2, 8);
  s.max_frame_delay = 1;  // For low latency decoding.
  s.all_layers = 0;       // Don't output a frame for every spatial layer.
  // Limit max frame size to avoid OOM'ing fuzzers. crbug.com/325284120.
  s.frame_size_limit = 16384 * 16384;
  s.operating_point = 31;  // Decode all operating points.

  int ret = dav1d_open(&context_, &s);
  if (ret) {
    LOG_ERROR("Dav1d AV1 decoder open failed");
  }

  if (SAVE_RECEIVED_AV1_STREAM) {
    file_av1_ = fopen("received_av1_stream.ivf", "w+b");
    if (!file_av1_) {
      LOG_WARN("Fail to open received_av1_stream.ivf");
    }
  }

  if (SAVE_DECODED_NV12_STREAM) {
    file_nv12_ = fopen("decoded_nv12_stream.yuv", "w+b");
    if (!file_nv12_) {
      LOG_WARN("Fail to open decoded_nv12_stream.yuv");
    }
  }

  return 0;
}

int Dav1dAv1Decoder::Decode(
    const uint8_t *data, int size,
    std::function<void(VideoFrame)> on_receive_decoded_frame) {
  if (SAVE_RECEIVED_AV1_STREAM) {
    fwrite((unsigned char *)data, 1, size, file_av1_);
  }

  ScopedDav1dData scoped_dav1d_data;
  Dav1dData &dav1d_data = scoped_dav1d_data.Data();
  dav1d_data_wrap(&dav1d_data, data, size,
                  /*free_callback=*/&NullFreeCallback,
                  /*user_data=*/nullptr);

  if (int decode_res = dav1d_send_data(context_, &dav1d_data)) {
    // On EAGAIN, dav1d can not consume more data and
    // dav1d_get_picture needs to be called first, which
    // will happen below, so just keep going in that case
    // and do not error out.
    if (decode_res != DAV1D_ERR(EAGAIN)) {
      LOG_ERROR("Dav1dAv1Decoder::Decode decoding failed with error code {}",
                decode_res);
      return -1;
    }
  }

  std::shared_ptr<ScopedDav1dPicture> scoped_dav1d_picture(
      new ScopedDav1dPicture{});
  Dav1dPicture &dav1d_picture = scoped_dav1d_picture->Picture();
  if (int get_picture_res = dav1d_get_picture(context_, &dav1d_picture)) {
    // On EAGAIN, it means dav1d has not enough data to decode
    // therefore this is not a decoding error but just means
    // we need to feed it more data, which happens in the next
    // run of the decoder loop.
    if (get_picture_res == DAV1D_ERR(EAGAIN)) {
      return -2;
    } else {
      return -1;
    }
  }

  if (dav1d_picture.p.bpc != 8) {
    // Only accept 8 bit depth.
    LOG_ERROR("Dav1dDecoder::Decode unhandled bit depth: {}",
              dav1d_picture.p.bpc);
    return -1;
  }

  frame_width_ = dav1d_picture.p.w;
  frame_height_ = dav1d_picture.p.h;
  nv12_frame_size_ = dav1d_picture.p.w * dav1d_picture.p.h * 3 / 2;

  if (!nv12_frame_) {
    nv12_frame_capacity_ = nv12_frame_size_;
    nv12_frame_ =
        new VideoFrame(nv12_frame_capacity_, frame_width_, frame_height_);
  }

  if (nv12_frame_capacity_ < nv12_frame_size_) {
    nv12_frame_capacity_ = nv12_frame_size_;
    delete nv12_frame_;
    nv12_frame_ =
        new VideoFrame(nv12_frame_capacity_, frame_width_, frame_height_);
  }

  if (nv12_frame_->Size() != nv12_frame_size_ ||
      nv12_frame_->Width() != frame_width_ ||
      nv12_frame_->Height() != frame_height_) {
    nv12_frame_->SetSize(nv12_frame_size_);
    nv12_frame_->SetWidth(frame_width_);
    nv12_frame_->SetHeight(frame_height_);
  }

  // if (1) {
  //   Yuv420pToNv12((unsigned char *)dav1d_picture.data[0],
  //                 (unsigned char *)dav1d_picture.data[1],
  //                 (unsigned char *)dav1d_picture.data[2],
  //                 dav1d_picture.stride[0], dav1d_picture.stride[1],
  //                 (unsigned char *)nv12_frame_->Buffer(), frame_width_,
  //                 frame_height_);
  // } else {
  //   libyuv::I420ToNV12(
  //       (const uint8_t *)dav1d_picture.data[0], dav1d_picture.stride[0],
  //       (const uint8_t *)dav1d_picture.data[1], dav1d_picture.stride[1] / 2,
  //       (const uint8_t *)dav1d_picture.data[2], dav1d_picture.stride[1] / 2,
  //       (uint8_t *)nv12_frame_->Buffer(), frame_width_,
  //       (uint8_t *)nv12_frame_->Buffer() + frame_width_ * frame_height_,
  //       frame_width_ / 2, frame_width_, frame_height_);
  // }

  size_t y_size = frame_width_ * frame_height_;
  size_t u_size = frame_width_ * frame_height_ / 4;
  size_t v_size = frame_width_ * frame_height_ / 4;

  for (int i = 0; i < frame_height_; i++) {
    memcpy((void *)(nv12_frame_->Buffer() + i * frame_width_),
           (uint8_t *)(dav1d_picture.data[0]) + i * dav1d_picture.stride[0],
           frame_width_);
  }

  // for (int i = 0; i < frame_height_ / 2; i++) {
  //   memcpy((void *)(nv12_frame_->Buffer() + y_size + i * (frame_width_ / 2)),
  //          (uint8_t *)(dav1d_picture.data[1]) + i * dav1d_picture.stride[1],
  //          frame_width_ / 2);
  // }

  uint8_t *uv_plane = (uint8_t *)nv12_frame_->Buffer() + y_size;
  for (int i = 0; i < frame_height_ / 2; i++) {
    for (int j = 0; j < frame_width_ / 2; j++) {
      uv_plane[i * frame_width_ + 2 * j] =
          ((uint8_t *)(dav1d_picture.data[1]))[i * dav1d_picture.stride[1] + j];
      uv_plane[i * frame_width_ + 2 * j + 1] =
          ((uint8_t *)(dav1d_picture.data[2]))[i * dav1d_picture.stride[1] + j];
    }
  }

  for (int i = 0; i < frame_height_ / 2; i++) {
    memcpy((void *)(nv12_frame_->Buffer() + y_size + u_size +
                    i * (frame_width_ / 2)),
           (uint8_t *)(dav1d_picture.data[2]) + i * dav1d_picture.stride[1],
           frame_width_ / 2);
  }

  LOG_ERROR("{}x{}", dav1d_picture.stride[0], dav1d_picture.stride[1]);

  on_receive_decoded_frame(*nv12_frame_);

  if (SAVE_DECODED_NV12_STREAM) {
    fwrite((unsigned char *)nv12_frame_->Buffer(), 1, nv12_frame_->Size(),
           file_nv12_);
  }

  return 0;
}