#include "dav1d_av1_decoder.h"

#include "log.h"

#define SAVE_DECODER_STREAM 0

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
};

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
  if (SAVE_DECODER_STREAM && file_) {
    fflush(file_);
    fclose(file_);
    file_ = nullptr;
  }

  if (decoded_frame_) {
    delete decoded_frame_;
    decoded_frame_ = nullptr;
  }
}

int Dav1dAv1Decoder::Init() {
  Dav1dSettings s;
  dav1d_default_settings(&s);

  s.n_threads = std::max(2, 4);
  s.max_frame_delay = 1;   // For low latency decoding.
  s.all_layers = 0;        // Don't output a frame for every spatial layer.
  s.operating_point = 31;  // Decode all operating points.

  int ret = dav1d_open(&context_, &s);
  if (ret) {
    LOG_ERROR("Dav1d AV1 decoder open failed");
  }

  decoded_frame_ = new VideoFrame(1280 * 720 * 3 / 2);

  if (SAVE_DECODER_STREAM) {
    file_ = fopen("decode_stream.yuv", "w+b");
    if (!file_) {
      LOG_WARN("Fail to open stream.yuv");
    }
  }
  return 0;
}

int Dav1dAv1Decoder::Decode(
    const uint8_t *data, int size,
    std::function<void(VideoFrame)> on_receive_decoded_frame) {
  ScopedDav1dData scoped_dav1d_data;
  Dav1dData &dav1d_data = scoped_dav1d_data.Data();
  dav1d_data_wrap(&dav1d_data, data, size,
                  /*free_callback=*/&NullFreeCallback,
                  /*user_data=*/nullptr);

  if (int decode_res = dav1d_send_data(context_, &dav1d_data)) {
    LOG_ERROR("Dav1dAv1Decoder::Decode decoding failed with error code {}",
              decode_res);

    return -1;
  }

  std::shared_ptr<ScopedDav1dPicture> scoped_dav1d_picture(
      new ScopedDav1dPicture{});
  Dav1dPicture &dav1d_picture = scoped_dav1d_picture->Picture();
  if (int get_picture_res = dav1d_get_picture(context_, &dav1d_picture)) {
    LOG_ERROR("Dav1dDecoder::Decode getting picture failed with error code {}",
              get_picture_res);
    return -1;
  }

  if (dav1d_picture.p.bpc != 8) {
    // Only accept 8 bit depth.
    LOG_ERROR("Dav1dDecoder::Decode unhandled bit depth: {}",
              dav1d_picture.p.bpc);
    return -1;
  }

  memcpy(decoded_frame_->GetBuffer(), dav1d_picture.data[0],
         dav1d_picture.p.w * dav1d_picture.p.h);
  memcpy(decoded_frame_->GetBuffer() + dav1d_picture.p.w * dav1d_picture.p.h,
         dav1d_picture.data[1], dav1d_picture.p.w * dav1d_picture.p.h / 2);
  LOG_INFO("dav1d decode size {}", decoded_frame_->Size());
  on_receive_decoded_frame(*decoded_frame_);
  if (SAVE_DECODER_STREAM) {
    fwrite((unsigned char *)decoded_frame_->Buffer(), 1, decoded_frame_->Size(),
           file_);
  }

  return 0;
}