#include "screen_capturer_avf.h"

#include <ApplicationServices/ApplicationServices.h>

#include <iostream>

#include "rd_log.h"

#define USE_SCALE_FACTOR 0

ScreenCapturerAvf::ScreenCapturerAvf() {}

ScreenCapturerAvf::~ScreenCapturerAvf() {
  if (inited_ && capture_thread_.joinable()) {
    capture_thread_.join();
    inited_ = false;
  }

  if (nv12_frame_) {
    delete[] nv12_frame_;
    nv12_frame_ = nullptr;
  }

  if (pFormatCtx_) {
    avformat_close_input(&pFormatCtx_);
    pFormatCtx_ = nullptr;
  }

  if (pCodecCtx_) {
    avcodec_free_context(&pCodecCtx_);
    pCodecCtx_ = nullptr;
  }

  if (options_) {
    av_dict_free(&options_);
    options_ = nullptr;
  }

  if (pFrame_) {
    av_frame_free(&pFrame_);
    pFrame_ = nullptr;
  }

  if (packet_) {
    av_packet_free(&packet_);
    packet_ = nullptr;
  }

#if USE_SCALE_FACTOR
  if (img_convert_ctx_) {
    sws_freeContext(img_convert_ctx_);
    img_convert_ctx_ = nullptr;
  }
#endif
}

int ScreenCapturerAvf::Init(const int fps, cb_desktop_data cb) {
  if (cb) {
    _on_data = cb;
  }

  av_log_set_level(AV_LOG_QUIET);

  pFormatCtx_ = avformat_alloc_context();

  avdevice_register_all();

  // grabbing frame rate
  av_dict_set(&options_, "framerate", "60", 0);
  av_dict_set(&options_, "pixel_format", "nv12", 0);
  // show remote cursor
  av_dict_set(&options_, "capture_cursor", "0", 0);
  // Make the grabbed area follow the mouse
  // av_dict_set(&options_, "follow_mouse", "centered", 0);
  // Video frame size. The default is to capture the full screen
  // av_dict_set(&options_, "video_size", "1440x900", 0);
  ifmt_ = (AVInputFormat *)av_find_input_format("avfoundation");
  if (!ifmt_) {
    printf("Couldn't find_input_format\n");
  }

  // Grab at position 10,20
  if (avformat_open_input(&pFormatCtx_, "Capture screen 0", ifmt_, &options_) !=
      0) {
    printf("Couldn't open input stream.\n");
    return -1;
  }

  if (avformat_find_stream_info(pFormatCtx_, NULL) < 0) {
    printf("Couldn't find stream information.\n");
    return -1;
  }

  videoindex_ = -1;
  for (i_ = 0; i_ < pFormatCtx_->nb_streams; i_++)
    if (pFormatCtx_->streams[i_]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoindex_ = i_;
      break;
    }
  if (videoindex_ == -1) {
    printf("Didn't find a video stream.\n");
    return -1;
  }

  pCodecParam_ = pFormatCtx_->streams[videoindex_]->codecpar;

  pCodecCtx_ = avcodec_alloc_context3(NULL);
  avcodec_parameters_to_context(pCodecCtx_, pCodecParam_);

  pCodec_ = const_cast<AVCodec *>(avcodec_find_decoder(pCodecCtx_->codec_id));
  if (pCodec_ == NULL) {
    printf("Codec not found.\n");
    return -1;
  }
  if (avcodec_open2(pCodecCtx_, pCodec_, NULL) < 0) {
    printf("Could not open codec.\n");
    return -1;
  }

  const int screen_w = pFormatCtx_->streams[videoindex_]->codecpar->width;
  const int screen_h = pFormatCtx_->streams[videoindex_]->codecpar->height;

  pFrame_ = av_frame_alloc();
  pFrame_->width = screen_w;
  pFrame_->height = screen_h;

#if USE_SCALE_FACTOR
  pFrame_resized_ = av_frame_alloc();
  pFrame_resized_->width = CGDisplayPixelsWide(CGMainDisplayID());
  pFrame_resized_->height = CGDisplayPixelsHigh(CGMainDisplayID());

  img_convert_ctx_ =
      sws_getContext(pFrame_->width, pFrame_->height, pCodecCtx_->pix_fmt,
                     pFrame_resized_->width, pFrame_resized_->height,
                     AV_PIX_FMT_NV12, SWS_BICUBIC, NULL, NULL, NULL);
#endif

  if (!nv12_frame_) {
    nv12_frame_ = new unsigned char[screen_w * screen_h * 3 / 2];
  }

  packet_ = (AVPacket *)av_malloc(sizeof(AVPacket));

  inited_ = true;

  return 0;
}

int ScreenCapturerAvf::Destroy() {
  running_ = false;
  return 0;
}

int ScreenCapturerAvf::Start() {
  if (running_) {
    return 0;
  }

  running_ = true;
  capture_thread_ = std::thread([this]() {
    while (running_) {
      if (av_read_frame(pFormatCtx_, packet_) >= 0) {
        if (packet_->stream_index == videoindex_) {
          avcodec_send_packet(pCodecCtx_, packet_);
          av_packet_unref(packet_);
          got_picture_ = avcodec_receive_frame(pCodecCtx_, pFrame_);

          if (!got_picture_) {
#if USE_SCALE_FACTOR
            av_image_fill_arrays(pFrame_resized_->data,
                                 pFrame_resized_->linesize, nv12_frame_,
                                 AV_PIX_FMT_NV12, pFrame_resized_->width,
                                 pFrame_resized_->height, 1);

            sws_scale(img_convert_ctx_, pFrame_->data, pFrame_->linesize, 0,
                      pFrame_->height, pFrame_resized_->data,
                      pFrame_resized_->linesize);

            _on_data((unsigned char *)nv12_frame_,
                     pFrame_resized_->width * pFrame_resized_->height * 3 / 2,
                     pFrame_resized_->width, pFrame_resized_->height);
#else
            memcpy(nv12_frame_, pFrame_->data[0],
                   pFrame_->linesize[0] * pFrame_->height);
            memcpy(nv12_frame_ + pFrame_->linesize[0] * pFrame_->height,
                   pFrame_->data[1],
                   pFrame_->linesize[1] * pFrame_->height / 2);
            _on_data((unsigned char *)nv12_frame_,
                     pFrame_->width * pFrame_->height * 3 / 2, pFrame_->width,
                     pFrame_->height);
#endif
          }
        }
      }
    }
  });

  return 0;
}

int ScreenCapturerAvf::Stop() {
  running_ = false;
  return 0;
}

int ScreenCapturerAvf::Pause() { return 0; }

int ScreenCapturerAvf::Resume() { return 0; }

void ScreenCapturerAvf::OnFrame() {}

void ScreenCapturerAvf::CleanUp() {}
