/*
 * @Author: DI JUNKUN
 * @Date: 2025-05-07
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_X11_H_
#define _SCREEN_CAPTURER_X11_H_

// forward declarations for X11 types
struct _XDisplay;
typedef struct _XDisplay Display;
typedef unsigned long Window;
struct _XRRScreenResources;
typedef struct _XRRScreenResources XRRScreenResources;
struct _XImage;
typedef struct _XImage XImage;

#include <atomic>
#include <cstring>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#include "screen_capturer.h"

namespace crossdesk {

class ScreenCapturerX11 : public ScreenCapturer {
 public:
  ScreenCapturerX11();
  ~ScreenCapturerX11();

 public:
  int Init(const int fps, cb_desktop_data cb) override;
  int Destroy() override;
  int Start(bool show_cursor) override;
  int Stop() override;

  int Pause(int monitor_index) override;
  int Resume(int monitor_index) override;

  int SwitchTo(int monitor_index) override;

  std::vector<DisplayInfo> GetDisplayInfoList() override;

  void OnFrame();

 private:
  void DrawCursor(XImage* image, int x, int y);

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
  std::atomic<bool> show_cursor_{true};
  int fps_ = 60;
  cb_desktop_data callback_;
  std::vector<DisplayInfo> display_info_list_;

  std::vector<uint8_t> y_plane_;
  std::vector<uint8_t> uv_plane_;
};
}  // namespace crossdesk
#endif