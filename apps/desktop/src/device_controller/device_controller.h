/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-14
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_CONTROLLER_H_
#define _DEVICE_CONTROLLER_H_

#include <cstdint>
#include <vector>

#include <remote_action.h>

#include "display_info.h"

namespace crossdesk {

// int key_code, bool is_down, uint32_t scan_code, bool extended
using OnKeyAction = void (*)(int, bool, uint32_t, bool, void*);

class DeviceController {
 public:
  virtual ~DeviceController() = default;
};

class MouseController : public DeviceController {
 public:
  ~MouseController() override = default;

  virtual int Init(std::vector<DisplayInfo> display_info_list) = 0;
  virtual int Destroy() = 0;
  virtual int SendMouseCommand(RemoteAction remote_action,
                               int display_index) = 0;
  virtual void UpdateDisplayInfoList(
      const std::vector<DisplayInfo>&) {}
};

class KeyboardCapturer : public DeviceController {
 public:
  ~KeyboardCapturer() override = default;

  virtual int Hook(OnKeyAction on_key_action, void* user_ptr) = 0;
  virtual int Unhook() = 0;
  virtual int SendKeyboardCommand(int key_code, bool is_down,
                                  uint32_t scan_code = 0,
                                  bool extended = false) = 0;
};

}  // namespace crossdesk

#endif
