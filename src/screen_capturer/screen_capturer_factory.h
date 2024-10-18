/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-15
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_FACTORY_H_
#define _SCREEN_CAPTURER_FACTORY_H_

#ifdef _WIN32
#include "screen_capturer_wgc.h"
#elif __linux__
#include "screen_capturer_x11.h"
#elif __APPLE__
// #include "screen_capturer_avf.h"
#include "screen_capturer_sck.h"
#endif

class ScreenCapturerFactory {
 public:
  virtual ~ScreenCapturerFactory() {}

 public:
  ScreenCapturer* Create() {
#ifdef _WIN32
    return new ScreenCapturerWgc();
#elif __linux__
    return new ScreenCapturerX11();
#elif __APPLE__
    // return new ScreenCapturerAvf();
    return new ScreenCapturerSck();
#else
    return nullptr;
#endif
  }
};

#endif