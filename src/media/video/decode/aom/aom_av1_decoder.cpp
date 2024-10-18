#include "aom_av1_decoder.h"

#include "log.h"

#define SAVE_RECEIVED_AV1_STREAM 0
#define SAVE_DECODED_NV12_STREAM 0

AomAv1Decoder::AomAv1Decoder() {}

AomAv1Decoder::~AomAv1Decoder() {
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

int AomAv1Decoder::Init() {
  aom_av1_decoder_config_.threads = 8;
  aom_av1_decoder_config_.w = 2560;
  aom_av1_decoder_config_.h = 1440;
  aom_av1_decoder_config_.allow_lowbitdepth = 1;

  extern aom_codec_iface_t my_codec;
  if (AOM_CODEC_OK != aom_codec_dec_init(&aom_av1_decoder_ctx_,
                                         aom_codec_av1_dx(),
                                         &aom_av1_decoder_config_, 0)) {
    LOG_ERROR("Failed to initialize decoder");
    return -1;
  }

  aom_codec_control(&aom_av1_decoder_ctx_, AV1D_GET_IMG_FORMAT,
                    AOM_IMG_FMT_NV12);

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

int AomAv1Decoder::Decode(
    const uint8_t *data, int size,
    std::function<void(VideoFrame)> on_receive_decoded_frame) {
  if (SAVE_RECEIVED_AV1_STREAM) {
    fwrite((unsigned char *)data, 1, size, file_av1_);
  }

  aom_codec_iter_t iter = nullptr;
  aom_codec_err_t ret =
      aom_codec_decode(&aom_av1_decoder_ctx_, data, size, nullptr);
  if (AOM_CODEC_OK != ret) {
    const char *error = aom_codec_error(&aom_av1_decoder_ctx_);
    const char *detail = aom_codec_error_detail(&aom_av1_decoder_ctx_);
    if (detail && error) {
      LOG_ERROR("Failed to decode frame [{}], additional information [{}]",
                error, detail);
    } else {
      LOG_ERROR("Failed to decode frame, ret [{}]",
                aom_codec_err_to_string(ret));
    }
    return -1;
  }

  img_ = aom_codec_get_frame(&aom_av1_decoder_ctx_, &iter);
  if (img_) {
    {
      aom_codec_frame_flags_t flags;
      ret = aom_codec_control(&aom_av1_decoder_ctx_, AOMD_GET_FRAME_FLAGS,
                              &flags);
      if (ret == AOM_CODEC_OK) {
        if (flags & AOM_FRAME_IS_KEY) {
          LOG_ERROR("Key frame");
        } else {
          LOG_ERROR("Inter frame");
        }
        if (flags & (AOM_FRAME_IS_KEY | AOM_FRAME_IS_INTRAONLY)) {
          LOG_ERROR("Key frame");
        } else if (flags & AOM_FRAME_IS_SWITCH) {
          LOG_ERROR("Switch frame");
        } else {
          LOG_ERROR("P frame");
        }
      }
    }
    int corrupted = 0;
    int ret = aom_codec_control(&aom_av1_decoder_ctx_, AOMD_GET_FRAME_CORRUPTED,
                                &corrupted);
    if (ret != AOM_CODEC_OK) {
      LOG_ERROR("Failed to get frame corrupted");
      return -1;
    }

    frame_width_ = img_->d_w;
    frame_height_ = img_->d_h;

    nv12_frame_size_ = frame_width_ * frame_height_ * 3 / 2;

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

    on_receive_decoded_frame(*nv12_frame_);

    if (SAVE_DECODED_NV12_STREAM) {
      fwrite((unsigned char *)nv12_frame_->Buffer(), 1, nv12_frame_->Size(),
             file_nv12_);
    }

    return 0;
  }

  return -1;
}