/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-15
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_H_
#define _SCREEN_CAPTURER_H_

#include <functional>

class ScreenCapturer {
 public:
  typedef std::function<void(unsigned char*, int, int, int)> cb_desktop_data;

  class DisplayInfo {
   public:
    void* handle = nullptr;
    std::string name = "";
    bool is_primary = false;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
  };

 public:
  virtual ~ScreenCapturer() {}

 public:
  virtual int Init(const int fps, cb_desktop_data cb) = 0;
  virtual int Destroy() = 0;
  virtual int Start() = 0;
  virtual int Stop() = 0;

  virtual std::vector<DisplayInfo> GetDisplayList() = 0;
};

#endif