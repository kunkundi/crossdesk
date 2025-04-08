#include "mouse_controller.h"

#include "rd_log.h"

MouseController::MouseController() {}

MouseController::~MouseController() {}

int MouseController::Init(int screen_width, int screen_height) {
  screen_width_ = screen_width;
  screen_height_ = screen_height;

  return 0;
}

int MouseController::Destroy() { return 0; }

int MouseController::SendMouseCommand(RemoteAction remote_action) {
  INPUT ip;

  if (remote_action.type == ControlType::mouse) {
    ip.type = INPUT_MOUSE;
    ip.mi.dx = (LONG)remote_action.m.x;
    ip.mi.dy = (LONG)remote_action.m.y;
    if (remote_action.m.flag == MouseFlag::left_down) {
      ip.mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE;
    } else if (remote_action.m.flag == MouseFlag::left_up) {
      ip.mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE;
    } else if (remote_action.m.flag == MouseFlag::right_down) {
      ip.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_ABSOLUTE;
    } else if (remote_action.m.flag == MouseFlag::right_up) {
      ip.mi.dwFlags = MOUSEEVENTF_RIGHTUP | MOUSEEVENTF_ABSOLUTE;
    } else {
      ip.mi.dwFlags = MOUSEEVENTF_MOVE;
    }
    ip.mi.mouseData = 0;
    ip.mi.time = 0;

    SetCursorPos(ip.mi.dx, ip.mi.dy);

    if (ip.mi.dwFlags != MOUSEEVENTF_MOVE) {
      SendInput(1, &ip, sizeof(INPUT));
    }
  }

  return 0;
}