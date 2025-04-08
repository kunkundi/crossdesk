/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-14
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MOUSE_CONTROLLER_H_
#define _MOUSE_CONTROLLER_H_

#include <fcntl.h>
#include <linux/uinput.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

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
  void SimulateKeyDown(int fd, int kval);
  void SimulateKeyUp(int fd, int kval);
  void SetMousePosition(int fd, int x, int y);

 private:
  int uinput_fd_;
  struct uinput_user_dev uinput_dev_;
  int screen_width_ = 0;
  int screen_height_ = 0;
};

#endif