#include "mouse_controller.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <unistd.h>

#include "platform.h"
#include "rd_log.h"

namespace crossdesk {

MouseController::MouseController() {}

MouseController::~MouseController() { Destroy(); }

int MouseController::Init(std::vector<DisplayInfo> display_info_list) {
  display_info_list_ = display_info_list;

  if (IsWaylandSession()) {
    if (InitWaylandPortal()) {
      use_wayland_portal_ = true;
      LOG_INFO("Mouse controller initialized with Wayland portal backend");
      return 0;
    }
    LOG_WARN(
        "Wayland mouse control init failed, falling back to X11/XTest backend");
  }

  display_ = XOpenDisplay(NULL);
  if (!display_) {
    LOG_ERROR("Cannot connect to X server");
    return -1;
  }

  root_ = DefaultRootWindow(display_);

  int event_base, error_base, major_version, minor_version;
  if (!XTestQueryExtension(display_, &event_base, &error_base, &major_version,
                           &minor_version)) {
    LOG_ERROR("XTest extension not available");
    XCloseDisplay(display_);
    display_ = nullptr;
    return -2;
  }

  return 0;
}

void MouseController::UpdateDisplayInfoList(
    const std::vector<DisplayInfo>& display_info_list) {
  if (display_info_list.empty()) {
    return;
  }

  display_info_list_ = display_info_list;
  if (use_wayland_portal_) {
    OnWaylandDisplayInfoListUpdated();
  }

  if (last_display_index_ < 0 ||
      last_display_index_ >= static_cast<int>(display_info_list_.size())) {
    last_display_index_ = -1;
    last_norm_x_ = -1.0;
    last_norm_y_ = -1.0;
  }
}

int MouseController::Destroy() {
  CleanupWaylandPortal();

  if (display_) {
    XCloseDisplay(display_);
    display_ = nullptr;
  }

  return 0;
}

int MouseController::SendMouseCommand(RemoteAction remote_action,
                                      int display_index) {
  if (remote_action.type != ControlType::mouse) {
    return 0;
  }

  if (use_wayland_portal_) {
    return SendWaylandMouseCommand(remote_action, display_index);
  }

  if (!display_) {
    LOG_ERROR("X11 display not initialized");
    return -1;
  }

  switch (remote_action.type) {
    case mouse:
      switch (remote_action.m.flag) {
        case MouseFlag::move: {
          if (display_index < 0 ||
              display_index >= static_cast<int>(display_info_list_.size())) {
            LOG_ERROR("Invalid display index: {}", display_index);
            return -2;
          }

          SetMousePosition(
              static_cast<int>(remote_action.m.x *
                                   display_info_list_[display_index].width +
                               display_info_list_[display_index].left),
              static_cast<int>(remote_action.m.y *
                                   display_info_list_[display_index].height +
                               display_info_list_[display_index].top));
          break;
        }
        case MouseFlag::left_down:
          XTestFakeButtonEvent(display_, 1, True, CurrentTime);
          XFlush(display_);
          break;
        case MouseFlag::left_up:
          XTestFakeButtonEvent(display_, 1, False, CurrentTime);
          XFlush(display_);
          break;
        case MouseFlag::right_down:
          XTestFakeButtonEvent(display_, 3, True, CurrentTime);
          XFlush(display_);
          break;
        case MouseFlag::right_up:
          XTestFakeButtonEvent(display_, 3, False, CurrentTime);
          XFlush(display_);
          break;
        case MouseFlag::middle_down:
          XTestFakeButtonEvent(display_, 2, True, CurrentTime);
          XFlush(display_);
          break;
        case MouseFlag::middle_up:
          XTestFakeButtonEvent(display_, 2, False, CurrentTime);
          XFlush(display_);
          break;
        case MouseFlag::wheel_vertical: {
          if (remote_action.m.s > 0) {
            SimulateMouseWheel(4, remote_action.m.s);
          } else if (remote_action.m.s < 0) {
            SimulateMouseWheel(5, -remote_action.m.s);
          }
          break;
        }
        case MouseFlag::wheel_horizontal: {
          if (remote_action.m.s > 0) {
            SimulateMouseWheel(6, remote_action.m.s);
          } else if (remote_action.m.s < 0) {
            SimulateMouseWheel(7, -remote_action.m.s);
          }
          break;
        }
      }
      break;
    default:
      break;
  }

  return 0;
}

void MouseController::SetMousePosition(int x, int y) {
  if (!display_) {
    return;
  }
  XWarpPointer(display_, None, root_, 0, 0, 0, 0, x, y);
  XFlush(display_);
}

void MouseController::SimulateKeyDown(int kval) {
  if (!display_) {
    return;
  }
  XTestFakeKeyEvent(display_, kval, True, CurrentTime);
  XFlush(display_);
}

void MouseController::SimulateKeyUp(int kval) {
  if (!display_) {
    return;
  }
  XTestFakeKeyEvent(display_, kval, False, CurrentTime);
  XFlush(display_);
}

void MouseController::SimulateMouseWheel(int direction_button, int count) {
  if (!display_) {
    return;
  }

  for (int i = 0; i < count; ++i) {
    XTestFakeButtonEvent(display_, direction_button, True, CurrentTime);
    XTestFakeButtonEvent(display_, direction_button, False, CurrentTime);
  }
  XFlush(display_);
}

}  // namespace crossdesk
