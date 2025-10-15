/*
 * @Author: DI JUNKUN
 * @Date: 2025-05-07
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_X11_H_
#define _SCREEN_CAPTURER_X11_H_

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

#include <atomic>
#include <cstring>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#include "screen_capturer.h"

class ScreenCapturerX11 : public ScreenCapturer {
 public:
  ScreenCapturerX11();
  ~ScreenCapturerX11();

 public:
  int Init(const int fps, cb_desktop_data cb) override;
  int Destroy() override;
  int Start() override;
  int Stop() override;

  int Pause(int monitor_index) override;
  int Resume(int monitor_index) override;

  int SwitchTo(int monitor_index) override;

  std::vector<DisplayInfo> GetDisplayInfoList() override;

  void OnFrame();

 private:
  Display* display_ = nullptr;
  Window root_ = 0;
  XRRScreenResources* screen_res_ = nullptr;
  int left_ = 0;
  int top_ = 0;
  int width_ = 0;
  int height_ = 0;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<int> monitor_index_{0};
  int fps_ = 60;
  cb_desktop_data callback_;
  std::vector<DisplayInfo> display_info_list_;

  // 缓冲区
  std::vector<uint8_t> y_plane_;
  std::vector<uint8_t> uv_plane_;
};

#endif