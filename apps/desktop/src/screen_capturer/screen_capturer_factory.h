/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-15
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_FACTORY_H_
#define _SCREEN_CAPTURER_FACTORY_H_

#include "screen_capturer.h"

namespace crossdesk {

class ScreenCapturerFactory {
 public:
  virtual ~ScreenCapturerFactory() {}

 public:
  ScreenCapturer* Create();
};
}  // namespace crossdesk
#endif
