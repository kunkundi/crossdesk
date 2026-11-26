#include "mouse_controller.h"

#include <remote_action.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <unistd.h>

#include <algorithm>

#include "platform.h"
#include "rd_log.h"

namespace crossdesk {

PlatformMouseController::PlatformMouseController() {}

PlatformMouseController::~PlatformMouseController() { Destroy(); }

int PlatformMouseController::Init(std::vector<DisplayInfo> display_info_list) {
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

void PlatformMouseController::UpdateDisplayInfoList(
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

int PlatformMouseController::Destroy() {
  ReleasePressedButtons();
  CleanupWaylandPortal();

  if (display_) {
    XCloseDisplay(display_);
    display_ = nullptr;
  }

  return 0;
}

unsigned PlatformMouseController::ReleasePressedButtons(unsigned mask) {
  mask &= 7;
  const unsigned pressed = pressed_buttons_ & mask;
  for (unsigned i = 0; i < 3; ++i) {
    const unsigned bit = 1u << i;
    if (!(pressed & bit)) continue;
    bool released = false;
    if (use_wayland_portal_) {
      RemoteAction up{};
      up.type = ControlType::mouse;
      up.m.flag = static_cast<MouseFlag>(2 + i * 2);
      released = SendWaylandMouseCommand(up, 0) == 0;
    } else if (display_) {
      const unsigned buttons[] = {1, 3, 2};
      released = XTestFakeButtonEvent(display_, buttons[i], False, CurrentTime) != 0;
      XFlush(display_);
    }
    if (released) pressed_buttons_ &= ~bit;
  }
  return mask & ~pressed_buttons_;
}

int PlatformMouseController::SendMouseCommand(RemoteAction remote_action,
                                      int display_index) {
  if (remote_action.type != ControlType::mouse) {
    return 0;
  }

  if (use_wayland_portal_) {
    const int ret = SendWaylandMouseCommand(remote_action, display_index);
    if (ret == 0) {
      const int flag = static_cast<int>(remote_action.m.flag);
      if (flag >= 1 && flag <= 6) {
        const unsigned bit = 1u << ((flag - 1) / 2);
        if (flag & 1) pressed_buttons_ |= bit;
        else pressed_buttons_ &= ~bit;
      }
    }
    return ret;
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
          // A disconnected output has no coordinate space. In particular,
          // do not collapse every remote movement onto its former origin.
          if (display_info_list_[display_index].width <= 1 ||
              display_info_list_[display_index].height <= 1) return -2;

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
          if (!XTestFakeButtonEvent(display_, 1, True, CurrentTime)) return -1;
          XFlush(display_);
          break;
        case MouseFlag::left_up:
          if (!XTestFakeButtonEvent(display_, 1, False, CurrentTime)) return -1;
          XFlush(display_);
          break;
        case MouseFlag::right_down:
          if (!XTestFakeButtonEvent(display_, 3, True, CurrentTime)) return -1;
          XFlush(display_);
          break;
        case MouseFlag::right_up:
          if (!XTestFakeButtonEvent(display_, 3, False, CurrentTime)) return -1;
          XFlush(display_);
          break;
        case MouseFlag::middle_down:
          if (!XTestFakeButtonEvent(display_, 2, True, CurrentTime)) return -1;
          XFlush(display_);
          break;
        case MouseFlag::middle_up:
          if (!XTestFakeButtonEvent(display_, 2, False, CurrentTime)) return -1;
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
          // X11 maps button 6 to scroll left and button 7 to scroll right.
          if (remote_action.m.s > 0) {
            SimulateMouseWheel(7, (std::min)(remote_action.m.s, 1200));
          } else if (remote_action.m.s < 0) {
            SimulateMouseWheel(6, -(std::max)(remote_action.m.s, -1200));
          }
          break;
        }
      }
      break;
    default:
      break;
  }

  const int flag = static_cast<int>(remote_action.m.flag);
  if (flag >= 1 && flag <= 6) {
    const unsigned bit = 1u << ((flag - 1) / 2);
    if (flag & 1) pressed_buttons_ |= bit;
    else pressed_buttons_ &= ~bit;
  }
  return 0;
}

void PlatformMouseController::SetMousePosition(int x, int y) {
  if (!display_) {
    return;
  }
  // Inject motion through XTest so XInput2 raw-motion listeners (including
  // SDL's global pointer cache) see the movement.
  // XWarpPointer moves the cursor without producing those raw events.
  XTestFakeMotionEvent(display_, DefaultScreen(display_), x, y, CurrentTime);
  XFlush(display_);
}

void PlatformMouseController::SimulateKeyDown(int kval) {
  if (!display_) {
    return;
  }
  XTestFakeKeyEvent(display_, kval, True, CurrentTime);
  XFlush(display_);
}

void PlatformMouseController::SimulateKeyUp(int kval) {
  if (!display_) {
    return;
  }
  XTestFakeKeyEvent(display_, kval, False, CurrentTime);
  XFlush(display_);
}

void PlatformMouseController::SimulateMouseWheel(int direction_button, int count) {
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
