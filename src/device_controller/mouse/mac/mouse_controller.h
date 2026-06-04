/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-14
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MOUSE_CONTROLLER_H_
#define _MOUSE_CONTROLLER_H_

#include <chrono>
#include <vector>

#include "device_controller.h"

namespace crossdesk {

class MouseController : public DeviceController {
 public:
  MouseController();
  virtual ~MouseController();

 public:
  virtual int Init(std::vector<DisplayInfo> display_info_list);
  virtual int Destroy();
  virtual int SendMouseCommand(RemoteAction remote_action, int display_index);

 private:
  struct ClickTracker {
    bool has_last_down = false;
    std::chrono::steady_clock::time_point last_down_time{};
    int last_down_x = 0;
    int last_down_y = 0;
    int click_state = 0;
    int active_click_state = 1;
  };

  int BeginClick(ClickTracker& tracker, int x, int y);
  int EndClick(ClickTracker& tracker, int x, int y);

  std::vector<DisplayInfo> display_info_list_;
  bool left_dragging_ = false;
  bool right_dragging_ = false;
  ClickTracker left_click_tracker_;
  ClickTracker right_click_tracker_;
  ClickTracker middle_click_tracker_;
};
}  // namespace crossdesk
#endif
