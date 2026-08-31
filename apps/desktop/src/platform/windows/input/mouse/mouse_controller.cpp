#include "mouse_controller.h"

#include <remote_action.h>

#include <Windows.h>

#include "rd_log.h"

namespace crossdesk {

PlatformMouseController::PlatformMouseController() {}

PlatformMouseController::~PlatformMouseController() {}

int PlatformMouseController::Init(std::vector<DisplayInfo> display_info_list) {
  display_info_list_ = display_info_list;

  return 0;
}

int PlatformMouseController::Destroy() { return 0; }

int PlatformMouseController::SendMouseCommand(RemoteAction remote_action,
                                      int display_index) {
  if (display_index < 0 ||
      display_index >= static_cast<int>(display_info_list_.size())) {
    LOG_WARN("Mouse command skipped, invalid display_index={}, displays={}",
             display_index, display_info_list_.size());
    return -1;
  }

  INPUT ip = {0};

  if (remote_action.type == ControlType::mouse) {
    ip.type = INPUT_MOUSE;
    ip.mi.dx =
        (LONG)(remote_action.m.x * display_info_list_[display_index].width) +
        display_info_list_[display_index].left;
    ip.mi.dy =
        (LONG)(remote_action.m.y * display_info_list_[display_index].height) +
        display_info_list_[display_index].top;

    switch (remote_action.m.flag) {
      case MouseFlag::left_down:
        ip.mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE;
        break;
      case MouseFlag::left_up:
        ip.mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE;
        break;
      case MouseFlag::right_down:
        ip.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_ABSOLUTE;
        break;
      case MouseFlag::right_up:
        ip.mi.dwFlags = MOUSEEVENTF_RIGHTUP | MOUSEEVENTF_ABSOLUTE;
        break;
      case MouseFlag::middle_down:
        ip.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_ABSOLUTE;
        break;
      case MouseFlag::middle_up:
        ip.mi.dwFlags = MOUSEEVENTF_MIDDLEUP | MOUSEEVENTF_ABSOLUTE;
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
        ip.mi.dwFlags = MOUSEEVENTF_MOVE;
        break;
    }

    ip.mi.time = 0;

    if (!SetCursorPos(ip.mi.dx, ip.mi.dy)) {
      LOG_WARN("SetCursorPos failed for mouse x={}, y={}, flag={}, err={}",
               ip.mi.dx, ip.mi.dy, static_cast<int>(remote_action.m.flag),
               GetLastError());
      return -1;
    }

    if (ip.mi.dwFlags != MOUSEEVENTF_MOVE) {
      UINT sent = SendInput(1, &ip, sizeof(INPUT));
      if (sent != 1) {
        LOG_WARN(
            "SendInput failed for mouse x={}, y={}, wheel={}, flag={}, err={}",
            ip.mi.dx, ip.mi.dy, remote_action.m.s,
            static_cast<int>(remote_action.m.flag), GetLastError());
        return -1;
      }
    }
  }

  return 0;
}
}  // namespace crossdesk
