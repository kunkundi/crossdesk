#include "ffmpeg_video_encoder.h"

#include <chrono>

#include "log.h"

#define SAVE_NV12_STREAM 0
#define SAVE_H264_STREAM 0

#define YUV420P_BUFFER_SIZE 1280 * 720 * 3 / 2
static unsigned char yuv420p_buffer[YUV420P_BUFFER_SIZE];

static int NV12ToYUV420PFFmpeg(unsigned char *src_buffer, int width, int height,
                               unsigned char *dst_buffer) {
  AVFrame *Input_pFrame = av_frame_alloc();
  AVFrame *Output_pFrame = av_frame_alloc();
  struct SwsContext *img_convert_ctx = sws_getContext(
      width, height, AV_PIX_FMT_NV12, 1280, 720, AV_PIX_FMT_YUV420P,
      SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

  av_image_fill_arrays(Input_pFrame->data, Input_pFrame->linesize, src_buffer,
                       AV_PIX_FMT_NV12, width, height, 1);
  av_image_fill_arrays(Output_pFrame->data, Output_pFrame->linesize, dst_buffer,
                       AV_PIX_FMT_YUV420P, 1280, 720, 1);

  sws_scale(img_convert_ctx, (uint8_t const **)Input_pFrame->data,
            Input_pFrame->linesize, 0, height, Output_pFrame->data,
            Output_pFrame->linesize);

  if (Input_pFrame) av_free(Input_pFrame);
  if (Output_pFrame) av_free(Output_pFrame);
  if (img_convert_ctx) sws_freeContext(img_convert_ctx);

  return 0;
}

FFmpegVideoEncoder::FFmpegVideoEncoder() {}
FFmpegVideoEncoder::~FFmpegVideoEncoder() {
  if (SAVE_NV12_STREAM && file_nv12_) {
    fflush(file_nv12_);
    fclose(file_nv12_);
    file_nv12_ = nullptr;
  }

  if (SAVE_H264_STREAM && file_h264_) {
    fflush(file_h264_);
    fclose(file_h264_);
    file_h264_ = nullptr;
  }

  if (nv12_data_) {
    free(nv12_data_);
    nv12_data_ = nullptr;
  }

  if (packet_) {
    av_packet_free(&packet_);
  }
}

int FFmpegVideoEncoder::Init() {
  av_log_set_level(AV_LOG_ERROR);

  codec_ = avcodec_find_encoder(AV_CODEC_ID_H264);
  if (!codec_) {
    LOG_ERROR("Failed to find H.264 encoder");
    return -1;
  } else {
#ifdef __linux__
    if (0 != strcmp(codec_->name, "openh264")) {
      use_openh264_ = true;
      LOG_INFO("Use H264 encoder [OpenH264]");
    }
#else
    LOG_INFO("Use H264 encoder [{}]", codec_->name);
#endif
  }
  // use_openh264_ = true;

  codec_ctx_ = avcodec_alloc_context3(codec_);
  if (!codec_ctx_) {
    LOG_ERROR("Failed to allocate codec context");
    return -1;
  }

  codec_ctx_->codec_id = AV_CODEC_ID_H264;
  codec_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
  codec_ctx_->width = frame_width_;
  codec_ctx_->height = frame_height;
  codec_ctx_->time_base.num = 1;
  codec_ctx_->time_base.den = fps_;
  if (use_openh264_) {
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
  } else {
    codec_ctx_->pix_fmt = AV_PIX_FMT_NV12;
  }
  codec_ctx_->gop_size = keyFrameInterval_;
  codec_ctx_->keyint_min = keyFrameInterval_;
  codec_ctx_->max_b_frames = 0;
  codec_ctx_->bit_rate = maxBitrate_ * 2000;
  codec_ctx_->qmin = 15;
  codec_ctx_->qmax = 35;
  codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  codec_ctx_->flags2 |= AV_CODEC_FLAG2_LOCAL_HEADER;

  // av_opt_set_int(codec_ctx_->priv_data, "qp", 51, 0);
  // av_opt_set_int(codec_ctx_->priv_data, "crf", 23, 0);

  if (!use_openh264_) {
    av_opt_set(codec_ctx_->priv_data, "profile", "baseline", 0);
  }
  av_opt_set(codec_ctx_->priv_data, "preset", "ultrafast", 0);
  av_opt_set(codec_ctx_->priv_data, "tune", "zerolatency", 0);

  if (avcodec_open2(codec_ctx_, codec_, nullptr) < 0) {
    LOG_ERROR("Failed to open codec");
    return -1;
  }

  frame_ = av_frame_alloc();
  frame_->format = codec_ctx_->pix_fmt;
  frame_->width = codec_ctx_->width;
  frame_->height = codec_ctx_->height;

  int ret = av_frame_get_buffer(frame_, 0);
  if (ret < 0) {
    LOG_ERROR("Could not allocate the raw frame");
    return -1;
  }

  packet_ = av_packet_alloc();

  if (SAVE_H264_STREAM) {
    file_h264_ = fopen("encoded_stream.h264", "w+b");
    if (!file_h264_) {
      LOG_WARN("Fail to open encoded_stream.h264");
    }
  }

  if (SAVE_NV12_STREAM) {
    file_nv12_ = fopen("raw_stream.yuv", "w+b");
    if (!file_nv12_) {
      LOG_WARN("Fail to open raw_stream.yuv");
    }
  }

  return 0;
}

int FFmpegVideoEncoder::Encode(
    const uint8_t *pData, int nSize,
    std::function<int(char *encoded_packets, size_t size,
                      VideoFrameType frame_type)>
        on_encoded_image) {
  if (!codec_ctx_) {
    LOG_ERROR("Invalid codec context");
    return -1;
  }

  if (use_openh264_) {
    NV12ToYUV420PFFmpeg((unsigned char *)pData, frame_->width, frame_->height,
                        (unsigned char *)yuv420p_buffer);

    frame_->data[0] = yuv420p_buffer;
    frame_->data[1] = yuv420p_buffer + frame_->width * frame_->height;
    frame_->data[2] = yuv420p_buffer + frame_->width * frame_->height * 5 / 4;

    if (SAVE_NV12_STREAM) {
      fwrite(yuv420p_buffer, 1, nSize, file_nv12_);
    }
  } else {
    memcpy(frame_->data[0], pData, frame_->width * frame_->height);
    memcpy(frame_->data[1], pData + frame_->width * frame_->height,
           frame_->width * frame_->height / 2);

    if (SAVE_NV12_STREAM) {
      fwrite(pData, 1, nSize, file_nv12_);
    }
  }

  frame_->pts = pts_++;

  int ret = avcodec_send_frame(codec_ctx_, frame_);

  // frame_->pict_type = AV_PICTURE_TYPE_I;
  VideoFrameType frame_type;
  if (0 == seq_++ % 300) {
    frame_type = VideoFrameType::kVideoFrameKey;
  } else {
    frame_type = VideoFrameType::kVideoFrameDelta;
  }

  while (ret >= 0) {
    ret = avcodec_receive_packet(codec_ctx_, packet_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      return 0;
    } else if (ret < 0) {
      return -1;
    }

    // Remove first 6 bytes in I frame, SEI ?
    if (0x00 == packet_->data[0] && 0x00 == packet_->data[1] &&
        0x00 == packet_->data[2] && 0x01 == packet_->data[3] &&
        0x09 == packet_->data[4] && 0x10 == packet_->data[5]) {
      packet_->data += 6;
      packet_->size -= 6;
    }

    if (on_encoded_image) {
      on_encoded_image((char *)packet_->data, packet_->size, frame_type);
      if (SAVE_H264_STREAM) {
        fwrite(packet_->data, 1, packet_->size, file_h264_);
      }
    } else {
      OnEncodedImage((char *)packet_->data, packet_->size);
    }
    av_packet_unref(packet_);
  }

  return 0;
}

int FFmpegVideoEncoder::OnEncodedImage(char *encoded_packets, size_t size) {
  LOG_INFO("OnEncodedImage not implemented");
  return 0;
}

void FFmpegVideoEncoder::ForceIdr() { frame_->pict_type = AV_PICTURE_TYPE_I; }
