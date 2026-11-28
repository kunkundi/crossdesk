#include "mouse_controller.h"

#include <remote_action.h>

#include <algorithm>
#include <cmath>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <unistd.h>

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
      Mouse up{};
      up.flag = static_cast<MouseFlag>(2 + i * 2);
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

  const Mouse& mouse = remote_action.m;
  const int flag = static_cast<int>(mouse.flag);
  const bool button = flag >= MouseFlag::left_down && flag <= MouseFlag::middle_up;
  const bool down = button && (flag & 1);
  if (mouse.flag == MouseFlag::move || button) {
    // Button packets carry their own position. Hover motion may be lost or
    // arrive on the other stream after the button.
    int result;
    if (use_wayland_portal_) {
      Mouse motion = mouse;
      motion.flag = MouseFlag::move;
      result = SendWaylandMouseCommand(motion, display_index, button);
    } else {
      result = PositionX11Pointer(mouse, display_index);
    }
    if (mouse.flag == MouseFlag::move || (down && result != 0)) return result;
    // Always release an existing press, even if its display disappeared.
  }

  if (use_wayland_portal_) {
    const int result = SendWaylandMouseCommand(mouse, display_index);
    if (result != 0) return result;
  } else {
    if (!display_) return -1;
    if (button) {
      constexpr unsigned x11_buttons[] = {1, 3, 2};  // Left, right, middle.
      if (!XTestFakeButtonEvent(display_, x11_buttons[(flag - 1) / 2], down,
                                CurrentTime)) return -1;
      XFlush(display_);
    } else if (mouse.flag == MouseFlag::wheel_vertical) {
      if (mouse.s > 0) SimulateMouseWheel(4, mouse.s);
      if (mouse.s < 0) SimulateMouseWheel(5, -mouse.s);
    } else if (mouse.flag == MouseFlag::wheel_horizontal) {
      if (mouse.s > 0) SimulateMouseWheel(6, mouse.s);
      if (mouse.s < 0) SimulateMouseWheel(7, -mouse.s);
    }
  }

  if (button) {
    const unsigned bit = 1u << ((flag - 1) / 2);
    if (down) pressed_buttons_ |= bit;
    else pressed_buttons_ &= ~bit;
  }
  return 0;
}

int PlatformMouseController::PositionX11Pointer(const Mouse& mouse, int display_index) {
  if (!display_ || display_index < 0 ||
      display_index >= static_cast<int>(display_info_list_.size())) return -1;
  const auto& display = display_info_list_[display_index];
  if (display.width <= 1 || display.height <= 1 ||
      !std::isfinite(mouse.x) || !std::isfinite(mouse.y)) return -2;
  const auto pixel = [](float value, int extent) {
    return std::min(static_cast<int>(std::clamp(value, 0.f, 1.f) * extent), extent - 1);
  };
  // XTest also updates consumers of raw motion, including SDL.
  if (!XTestFakeMotionEvent(display_, DefaultScreen(display_),
                            display.left + pixel(mouse.x, display.width),
                            display.top + pixel(mouse.y, display.height), CurrentTime)) return -1;
  XFlush(display_);
  return 0;
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
