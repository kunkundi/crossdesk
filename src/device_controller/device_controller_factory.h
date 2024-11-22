/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-14
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_CONTROLLER_FACTORY_H_
#define _DEVICE_CONTROLLER_FACTORY_H_

#include "device_controller.h"
#include "keyboard_capturer.h"
#include "mouse_controller.h"

class DeviceControllerFactory {
 public:
  enum Device { Mouse = 0, Keyboard };

 public:
  virtual ~DeviceControllerFactory() {}

 public:
  DeviceController* Create(Device device) {
    switch (device) {
      case Mouse:
        return new MouseController();
      case Keyboard:
        return new KeyboardCapturer();
      default:
        return nullptr;
    }
  }
};

#endif