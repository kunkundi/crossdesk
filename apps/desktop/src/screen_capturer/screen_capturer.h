/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-15
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_H_
#define _SCREEN_CAPTURER_H_

#include <functional>

#include "display_info.h"
#include "minirtc.h"

namespace crossdesk {

class ScreenCapturer {
 public:
  // |stream_id| is a logical MiniRTC stream ID (DisplayN), not a platform
  // display name or physical handle. |native_frame| is borrowed for the
  // duration of the callback; retain its owner before using it asynchronously.
  typedef std::function<void(unsigned char* data, int size, int width,
                             int height, const char* stream_id,
                             const XNativeVideoFrame* native_frame)>
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
