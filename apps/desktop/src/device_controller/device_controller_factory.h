/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-14
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_CONTROLLER_FACTORY_H_
#define _DEVICE_CONTROLLER_FACTORY_H_

#include "device_controller.h"

namespace crossdesk {

class DeviceControllerFactory {
 public:
  enum Device { Mouse = 0, Keyboard };

 public:
  virtual ~DeviceControllerFactory() {}

 public:
  DeviceController* Create(Device device);
};
}  // namespace crossdesk
#endif
