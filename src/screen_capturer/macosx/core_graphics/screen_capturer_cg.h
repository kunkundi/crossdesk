
/*
 * @Author: DI JUNKUN
 * @Date: 2024-10-16
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_CGD_H_
#define _SCREEN_CAPTURER_CGD_H_

#include <CoreGraphics/CoreGraphics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "screen_capturer.h"

class ScreenCapturerCg : public ScreenCapturer {
 public:
  ScreenCapturerCg();
  ~ScreenCapturerCg();

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
  int _fps;
  cb_desktop_data _on_data;

  // thread
  std::thread capture_thread_;
  std::atomic_bool running_;

 private:
};

#endif