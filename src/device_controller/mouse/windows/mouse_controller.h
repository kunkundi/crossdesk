/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-14
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MOUSE_CONTROLLER_H_
#define _MOUSE_CONTROLLER_H_

#include "device_controller.h"

class MouseController : public DeviceController {
 public:
  MouseController();
  virtual ~MouseController();

 public:
  virtual int Init(int screen_width, int screen_height);
  virtual int Destroy();
  virtual int SendMouseCommand(RemoteAction remote_action);

 private:
  int screen_width_ = 0;
  int screen_height_ = 0;
};

#endif