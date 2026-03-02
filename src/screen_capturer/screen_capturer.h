/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-15
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_H_
#define _SCREEN_CAPTURER_H_

#include <functional>

#include "display_info.h"

namespace crossdesk {

class ScreenCapturer {
 public:
  typedef std::function<void(unsigned char*, int, int, int, const char*)>
      cb_desktop_data;

 public:
  virtual ~ScreenCapturer() {}

 public:
  virtual int Init(const int fps, cb_desktop_data cb) = 0;
  virtual int Destroy() = 0;
  virtual int Start(bool show_cursor) = 0;
  virtual int Stop() = 0;
  virtual int Pause(int monitor_index) = 0;
  virtual int Resume(int monitor_index) = 0;

  virtual std::vector<DisplayInfo> GetDisplayInfoList() = 0;
  virtual int SwitchTo(int monitor_index) = 0;
  virtual int ResetToInitialMonitor() = 0;
};
}  // namespace crossdesk
#endif