/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-14
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MOUSE_CONTROLLER_H_
#define _MOUSE_CONTROLLER_H_

#include <vector>

#include "device_controller.h"

class MouseController : public DeviceController {
 public:
  MouseController();
  virtual ~MouseController();

 public:
  virtual int Init(std::vector<DisplayInfo> display_info_list);
  virtual int Destroy();
  virtual int SendMouseCommand(RemoteAction remote_action, int display_index);

 private:
  std::vector<DisplayInfo> display_info_list_;
};

#endif