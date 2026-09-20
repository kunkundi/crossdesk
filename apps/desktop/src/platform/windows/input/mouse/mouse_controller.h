/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-14
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MOUSE_CONTROLLER_H_
#define _MOUSE_CONTROLLER_H_

#include <vector>
#include <atomic>
#include <mutex>

#include <remote_action.h>

#include "device_controller.h"
#include "display_info.h"

namespace crossdesk {

class PlatformMouseController final : public MouseController {
 public:
  PlatformMouseController();
  virtual ~PlatformMouseController();

 public:
  virtual int Init(std::vector<DisplayInfo> display_info_list);
  virtual int Destroy();
  virtual int SendMouseCommand(RemoteAction remote_action, int display_index);
  void UpdateDisplayInfoList(
      const std::vector<DisplayInfo>& display_info_list) override;
  int ReleasePressedButtons();

 private:
  std::vector<DisplayInfo> display_info_list_;
  std::mutex display_info_mutex_;
  std::atomic<unsigned> pressed_buttons_{0};
};
}  // namespace crossdesk
#endif
