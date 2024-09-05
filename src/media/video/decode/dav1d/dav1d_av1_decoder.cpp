#include "dav1d_av1_decoder.h"

#include "log.h"

#define SAVE_RECEIVED_AV1_STREAM 0
#define SAVE_DECODED_NV12_STREAM 0

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

  if (decoded_frame_yuv_) {
    delete decoded_frame_yuv_;
    decoded_frame_yuv_ = nullptr;
  }

  if (decoded_frame_nv12_) {
    delete decoded_frame_nv12_;
    decoded_frame_nv12_ = nullptr;
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

  decoded_frame_yuv_ = new VideoFrame(1280 * 720 * 3 / 2);
  // decoded_frame_nv12_ = new VideoFrame(1280 * 720 * 3 / 2);

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

void YUV420PtoNV12(unsigned char *SrcY, unsigned char *SrcU,
                   unsigned char *SrcV, unsigned char *Dst, int Width,
                   int Height) {
  memcpy(Dst, SrcY, Width * Height);
  unsigned char *DstU = Dst + Width * Height;
  for (int i = 0; i < Width * Height / 4; i++) {
    (*DstU++) = (*SrcU++);
    (*DstU++) = (*SrcV++);
  }
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

  if (0) {
    YUV420PtoNV12((unsigned char *)dav1d_picture.data[0],
                  (unsigned char *)dav1d_picture.data[1],
                  (unsigned char *)dav1d_picture.data[2],
                  decoded_frame_nv12_->GetBuffer(), dav1d_picture.p.w,
                  dav1d_picture.p.h);
  } else {
    if (!decoded_frame_nv12_) {
      decoded_frame_nv12_capacity_ =
          dav1d_picture.p.w * dav1d_picture.p.h * 3 / 2;
      decoded_frame_nv12_ = new VideoFrame(
          decoded_frame_nv12_capacity_, dav1d_picture.p.w, dav1d_picture.p.h);
    }

    if (decoded_frame_nv12_capacity_ <
        dav1d_picture.p.w * dav1d_picture.p.h * 3 / 2) {
      delete decoded_frame_nv12_;
      decoded_frame_nv12_capacity_ =
          dav1d_picture.p.w * dav1d_picture.p.h * 3 / 2;
      decoded_frame_nv12_ = new VideoFrame(
          decoded_frame_nv12_capacity_, dav1d_picture.p.w, dav1d_picture.p.h);
    }

    libyuv::I420ToNV12(
        (const uint8_t *)dav1d_picture.data[0], dav1d_picture.p.w,
        (const uint8_t *)dav1d_picture.data[1], dav1d_picture.p.w / 2,
        (const uint8_t *)dav1d_picture.data[2], dav1d_picture.p.w / 2,
        decoded_frame_nv12_->GetBuffer(), dav1d_picture.p.w,
        decoded_frame_nv12_->GetBuffer() +
            dav1d_picture.p.w * dav1d_picture.p.h,
        dav1d_picture.p.w, dav1d_picture.p.w, dav1d_picture.p.h);
  }

  on_receive_decoded_frame(*decoded_frame_nv12_);
  if (SAVE_DECODED_NV12_STREAM) {
    fwrite((unsigned char *)decoded_frame_nv12_->Buffer(), 1,
           decoded_frame_nv12_->Size(), file_nv12_);
  }

  return 0;
}