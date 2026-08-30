/*
 * @Author: DI JUNKUN
 * @Date: 2025-05-07
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MOUSE_CONTROLLER_H_
#define _MOUSE_CONTROLLER_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "device_controller.h"
#include "display_info.h"

struct DBusConnection;
struct DBusMessageIter;
struct _XDisplay;

namespace crossdesk {

class MouseController : public DeviceController {
 public:
  MouseController();
  virtual ~MouseController();

 public:
  virtual int Init(std::vector<DisplayInfo> display_info_list);
  virtual int Destroy();
  virtual int SendMouseCommand(RemoteAction remote_action, int display_index);
  void UpdateDisplayInfoList(const std::vector<DisplayInfo>& display_info_list);

 private:
  void SimulateKeyDown(int kval);
  void SimulateKeyUp(int kval);
  void SetMousePosition(int x, int y);
  void SimulateMouseWheel(int direction_button, int count);
  bool InitWaylandPortal();
  void CleanupWaylandPortal();
  int SendWaylandMouseCommand(RemoteAction remote_action, int display_index);
  void OnWaylandDisplayInfoListUpdated();
  bool NotifyWaylandPointerMotion(double dx, double dy);
  bool NotifyWaylandPointerMotionAbsolute(uint32_t stream, double x, double y);
  bool NotifyWaylandPointerButton(int button, uint32_t state);
  bool NotifyWaylandPointerAxisDiscrete(uint32_t axis, int32_t steps);
  bool SendWaylandPortalVoidCall(
      const char* method_name,
      const std::function<void(DBusMessageIter*)>& append_args);

  enum class WaylandAbsoluteMode { kUnknown, kPixels, kNormalized, kDisabled };

  _XDisplay* display_ = nullptr;
  unsigned long root_ = 0;
  std::vector<DisplayInfo> display_info_list_;
  int screen_width_ = 0;
  int screen_height_ = 0;
  bool use_wayland_portal_ = false;

  DBusConnection* dbus_connection_ = nullptr;
  std::string wayland_session_handle_;
  int last_display_index_ = -1;
  double last_norm_x_ = -1.0;
  double last_norm_y_ = -1.0;
  bool logged_wayland_display_info_ = false;
  uintptr_t last_logged_wayland_stream_ = 0;
  int last_logged_wayland_width_ = 0;
  int last_logged_wayland_height_ = 0;
  WaylandAbsoluteMode wayland_absolute_mode_ = WaylandAbsoluteMode::kUnknown;
  bool wayland_absolute_disabled_logged_ = false;
  uint32_t wayland_absolute_stream_id_ = 0;
  int wayland_portal_space_width_ = 0;
  int wayland_portal_space_height_ = 0;
  bool using_shared_wayland_session_ = false;
};
}  // namespace crossdesk
#endif
