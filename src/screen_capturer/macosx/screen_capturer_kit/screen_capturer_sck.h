/*
 * @Author: DI JUNKUN
 * @Date: 2024-10-17
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_SCK_H_
#define _SCREEN_CAPTURER_SCK_H_

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "screen_capturer.h"

class ScreenCapturerSck : public ScreenCapturer {
 public:
  ScreenCapturerSck();
  ~ScreenCapturerSck();

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
  std::unique_ptr<ScreenCapturer> CreateScreenCapturerSck();

 private:
  int _fps;
  cb_desktop_data on_data_;
  unsigned char* nv12_frame_ = nullptr;
  bool inited_ = false;

  // thread
  std::thread capture_thread_;
  std::atomic_bool running_;

 private:
  std::unique_ptr<ScreenCapturer> screen_capturer_sck_impl_;
};

#endif