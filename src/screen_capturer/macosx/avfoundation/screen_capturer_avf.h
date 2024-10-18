/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-01
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_AVF_H_
#define _SCREEN_CAPTURER_AVF_H_

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "screen_capturer.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
};
#endif

class ScreenCapturerAvf : public ScreenCapturer {
 public:
  ScreenCapturerAvf();
  ~ScreenCapturerAvf();

 public:
  virtual int Init(const int fps, cb_desktop_data cb);

  virtual int Destroy();

  virtual int Start();

  virtual int Stop();

  int Pause();
  int Resume();

  void OnFrame();

 protected:
  void CleanUp();

 private:
  std::atomic_bool _paused;
  std::atomic_bool _inited;

  std::thread _thread;

  std::string _device_name;

  int _fps;

  cb_desktop_data _on_data;

 private:
  int i_ = 0;
  int videoindex_ = 0;
  int got_picture_ = 0;
  bool inited_ = false;

  // ffmpeg
  AVFormatContext *pFormatCtx_ = nullptr;
  AVCodecContext *pCodecCtx_ = nullptr;
  AVCodec *pCodec_ = nullptr;
  AVCodecParameters *pCodecParam_ = nullptr;
  AVDictionary *options_ = nullptr;
  AVInputFormat *ifmt_ = nullptr;
  AVFrame *pFrame_ = nullptr;
  AVFrame *pFrame_resized_ = nullptr;
  AVPacket *packet_ = nullptr;
  struct SwsContext *img_convert_ctx_ = nullptr;
  unsigned char *nv12_frame_ = nullptr;

  // thread
  std::thread capture_thread_;
  std::atomic_bool running_;
};

#endif