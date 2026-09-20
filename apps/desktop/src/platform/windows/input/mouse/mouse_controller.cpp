#include "mouse_controller.h"

#include <Windows.h>
#include <remote_action.h>

#include <algorithm>
#include <cmath>

#include "rd_log.h"
#include "windows_input_injector.h"
#include "windows_input_marker.h"
#include "windows_thread_dpi.h"

namespace crossdesk {

PlatformMouseController::PlatformMouseController() {}

PlatformMouseController::~PlatformMouseController() { Destroy(); }

int PlatformMouseController::Init(std::vector<DisplayInfo> display_info_list) {
  std::lock_guard<std::mutex> lock(display_info_mutex_);
  display_info_list_ = display_info_list;

  return 0;
}

void PlatformMouseController::UpdateDisplayInfoList(
    const std::vector<DisplayInfo>& display_info_list) {
  if (display_info_list.empty()) return;
  std::lock_guard<std::mutex> lock(display_info_mutex_);
  if (std::equal(display_info_list.begin(), display_info_list.end(),
                 display_info_list_.begin(), display_info_list_.end(),
                 [](const auto& a, const auto& b) {
                   return a.handle == b.handle && a.left == b.left &&
                          a.top == b.top && a.width == b.width &&
                          a.height == b.height;
                 })) return;
  display_info_list_ = display_info_list;
  LOG_INFO("Windows mouse display geometry updated: displays={}",
           display_info_list_.size());
}

int PlatformMouseController::Destroy() { return ReleasePressedButtons(); }

int PlatformMouseController::ReleasePressedButtons() {
  const unsigned pressed = pressed_buttons_.load();
  if (!pressed) return 0;
  INPUT input{};
  input.type = INPUT_MOUSE;
  input.mi.dwExtraInfo = kInjectedMouseInputMarker;
  if (pressed & 1) input.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
  if (pressed & 2) input.mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
  if (pressed & 4) input.mi.dwFlags |= MOUSEEVENTF_MIDDLEUP;
  if (SendInputOnUserDesktop(input) != 1) {
    LOG_WARN("Release remote mouse buttons failed, error={}", GetLastError());
    return -1;
  }
  pressed_buttons_.fetch_and(~pressed);
  return 0;
}

int PlatformMouseController::SendMouseCommand(RemoteAction remote_action,
                                              int display_index) {
  std::unique_lock<std::mutex> geometry_lock(display_info_mutex_);
  if (display_index < 0 ||
      display_index >= static_cast<int>(display_info_list_.size()) ||
      display_info_list_[display_index].width <= 0 ||
      display_info_list_[display_index].height <= 0) {
    LOG_WARN("Mouse command skipped, invalid display_index={}, displays={}",
             display_index, display_info_list_.size());
    return -1;
  }

  const auto display = display_info_list_[display_index];
  geometry_lock.unlock();

  INPUT ip = {0};
  ScopedWindowsPhysicalCoordinates physical_coordinates;

  if (remote_action.type != ControlType::mouse) return 0;
  if (!std::isfinite(remote_action.m.x) || !std::isfinite(remote_action.m.y))
    return -1;
  ip.type = INPUT_MOUSE;
  ip.mi.dx = (LONG)(std::clamp(remote_action.m.x, 0.0f, 1.0f) *
                    (display.width - 1)) + display.left;
  ip.mi.dy = (LONG)(std::clamp(remote_action.m.y, 0.0f, 1.0f) *
                    (display.height - 1)) + display.top;

  switch (remote_action.m.flag) {
    case MouseFlag::left_down:
      ip.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
      break;
    case MouseFlag::left_up:
      ip.mi.dwFlags = MOUSEEVENTF_LEFTUP;
      break;
    case MouseFlag::right_down:
      ip.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
      break;
    case MouseFlag::right_up:
      ip.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
      break;
    case MouseFlag::middle_down:
      ip.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
      break;
    case MouseFlag::middle_up:
      ip.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
      break;
    case MouseFlag::wheel_vertical:
      ip.mi.dwFlags = MOUSEEVENTF_WHEEL;
      ip.mi.mouseData = remote_action.m.s * 120;
      break;
    case MouseFlag::wheel_horizontal:
      ip.mi.dwFlags = MOUSEEVENTF_HWHEEL;
      ip.mi.mouseData = remote_action.m.s * 120;
      break;
    default:
      break;
  }

  const int virtual_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int virtual_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int virtual_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int virtual_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (virtual_width <= 0 || virtual_height <= 0) {
    LOG_WARN("Remote mouse skipped: invalid virtual desktop geometry");
    return -1;
  }
  // Pixel centres in the virtual desktop, including negative monitor origins.
  // SetCursorPos cannot carry dwExtraInfo through the privacy mouse hook.
  const int virtual_width_range = (std::max)(1, virtual_width - 1);
  const int virtual_height_range = (std::max)(1, virtual_height - 1);
  ip.mi.dx = static_cast<LONG>(std::clamp<int64_t>(
      (static_cast<int64_t>(ip.mi.dx - virtual_left) * 65535 +
       virtual_width_range / 2) /
          virtual_width_range,
      0, 65535));
  ip.mi.dy = static_cast<LONG>(std::clamp<int64_t>(
      (static_cast<int64_t>(ip.mi.dy - virtual_top) * 65535 +
       virtual_height_range / 2) /
          virtual_height_range,
      0, 65535));
  ip.mi.dwFlags |=
      MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
  ip.mi.dwExtraInfo = kInjectedMouseInputMarker;
  const UINT sent = SendInputOnUserDesktop(ip);
  if (sent != 1) {
    LOG_WARN("SendInput failed for mouse x={}, y={}, wheel={}, flag={}, err={}",
             ip.mi.dx, ip.mi.dy, remote_action.m.s,
             static_cast<int>(remote_action.m.flag), GetLastError());
    return -1;
  }
  if (ip.mi.dwFlags & MOUSEEVENTF_LEFTDOWN) pressed_buttons_.fetch_or(1);
  if (ip.mi.dwFlags & MOUSEEVENTF_RIGHTDOWN) pressed_buttons_.fetch_or(2);
  if (ip.mi.dwFlags & MOUSEEVENTF_MIDDLEDOWN) pressed_buttons_.fetch_or(4);
  if (ip.mi.dwFlags & MOUSEEVENTF_LEFTUP) pressed_buttons_.fetch_and(~1u);
  if (ip.mi.dwFlags & MOUSEEVENTF_RIGHTUP) pressed_buttons_.fetch_and(~2u);
  if (ip.mi.dwFlags & MOUSEEVENTF_MIDDLEUP) pressed_buttons_.fetch_and(~4u);

  return 0;
}
}  // namespace crossdesk
