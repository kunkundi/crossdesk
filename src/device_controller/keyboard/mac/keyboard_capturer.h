/*
 * @Author: DI JUNKUN
 * @Date: 2024-11-22
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _KEYBOARD_CAPTURER_H_
#define _KEYBOARD_CAPTURER_H_

#include "device_controller.h"

class KeyboardCapturer : public DeviceController {
 public:
  KeyboardCapturer();
  virtual ~KeyboardCapturer();

 public:
  virtual int Hook();
  virtual int Unhook();

 private:
};

#endif