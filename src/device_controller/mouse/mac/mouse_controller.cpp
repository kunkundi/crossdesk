#include "mouse_controller.h"

#include <ApplicationServices/ApplicationServices.h>

#include "rd_log.h"

MouseController::MouseController() {}

MouseController::~MouseController() {}

int MouseController::Init(std::vector<DisplayInfo> display_info_list) {
  display_info_list_ = display_info_list;

  return 0;
}

int MouseController::Destroy() { return 0; }

int MouseController::SendMouseCommand(RemoteAction remote_action,
                                      int display_index) {
  int mouse_pos_x =
      remote_action.m.x * display_info_list_[display_index].width +
      display_info_list_[display_index].left;
  int mouse_pos_y =
      remote_action.m.y * display_info_list_[display_index].height +
      display_info_list_[display_index].top;

  if (remote_action.type == ControlType::mouse) {
    CGEventRef mouse_event = nullptr;
    CGEventType mouse_type;
    CGPoint mouse_point = CGPointMake(mouse_pos_x, mouse_pos_y);

    switch (remote_action.m.flag) {
      case MouseFlag::left_down:
        mouse_type = kCGEventLeftMouseDown;
        mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                              kCGMouseButtonLeft);
        break;
      case MouseFlag::left_up:
        mouse_type = kCGEventLeftMouseUp;
        mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                              kCGMouseButtonLeft);
        break;
      case MouseFlag::right_down:
        mouse_type = kCGEventRightMouseDown;
        mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                              kCGMouseButtonRight);
        break;
      case MouseFlag::right_up:
        mouse_type = kCGEventRightMouseUp;
        mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                              kCGMouseButtonRight);
        break;
      case MouseFlag::middle_down:
        mouse_type = kCGEventOtherMouseDown;
        mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                              kCGMouseButtonCenter);
        break;
      case MouseFlag::middle_up:
        mouse_type = kCGEventOtherMouseUp;
        mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                              kCGMouseButtonCenter);
        break;
      case MouseFlag::wheel_vertical:
        mouse_event = CGEventCreateScrollWheelEvent(
            NULL, kCGScrollEventUnitLine, 2, remote_action.m.s, 0);
        break;
      case MouseFlag::wheel_horizontal:
        mouse_event = CGEventCreateScrollWheelEvent(
            NULL, kCGScrollEventUnitLine, 2, 0, remote_action.m.s);
        break;
      default:
        mouse_type = kCGEventMouseMoved;
        mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                              kCGMouseButtonLeft);
        break;
    }

    if (mouse_event) {
      CGEventPost(kCGHIDEventTap, mouse_event);
      CFRelease(mouse_event);
    }
  }

  return 0;
}