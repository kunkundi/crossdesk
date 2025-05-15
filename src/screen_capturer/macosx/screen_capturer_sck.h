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
  virtual int Init(const int fps, cb_desktop_data cb) override;
  virtual int Destroy() override;
  virtual int Start() override;
  virtual int Stop() override;

  int Pause(int monitor_index) override;

  int Resume(int monitor_index) override;

  int SwitchTo(int monitor_index) override { return 0; }

  std::vector<DisplayInfo> GetDisplayInfoList() override { return {}; }

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