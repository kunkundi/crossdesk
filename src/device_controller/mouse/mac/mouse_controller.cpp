#include "mouse_controller.h"

#include <ApplicationServices/ApplicationServices.h>
#include <algorithm>

#include "rd_log.h"

namespace crossdesk {

MouseController::MouseController() {}

MouseController::~MouseController() {}

int MouseController::Init(std::vector<DisplayInfo> display_info_list) {
  display_info_list_ = display_info_list;

  return 0;
}

int MouseController::Destroy() { return 0; }

int MouseController::SendMouseCommand(RemoteAction remote_action,
                                      int display_index) {
  if (remote_action.type != ControlType::mouse) {
    return 0;
  }

  if (display_index < 0 ||
      display_index >= static_cast<int>(display_info_list_.size())) {
    LOG_WARN("Mouse command skipped, invalid display_index={}, displays={}",
             display_index, display_info_list_.size());
    return -1;
  }

  const DisplayInfo& display_info = display_info_list_[display_index];
  if (display_info.width <= 0 || display_info.height <= 0) {
    LOG_WARN("Mouse command skipped, invalid display geometry: {}x{}",
             display_info.width, display_info.height);
    return -1;
  }

  const float normalized_x = std::clamp(remote_action.m.x, 0.0f, 1.0f);
  const float normalized_y = std::clamp(remote_action.m.y, 0.0f, 1.0f);
  int mouse_pos_x =
      normalized_x * display_info.width + display_info.left;
  int mouse_pos_y =
      normalized_y * display_info.height + display_info.top;

  CGEventRef mouse_event = nullptr;
  CGEventType mouse_type;
  CGMouseButton mouse_button;
  CGPoint mouse_point = CGPointMake(mouse_pos_x, mouse_pos_y);

  switch (remote_action.m.flag) {
    case MouseFlag::left_down:
      mouse_type = kCGEventLeftMouseDown;
      left_dragging_ = true;
      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            kCGMouseButtonLeft);
      break;
    case MouseFlag::left_up:
      mouse_type = kCGEventLeftMouseUp;
      left_dragging_ = false;
      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            kCGMouseButtonLeft);
      break;
    case MouseFlag::right_down:
      mouse_type = kCGEventRightMouseDown;
      right_dragging_ = true;
      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            kCGMouseButtonRight);
      break;
    case MouseFlag::right_up:
      mouse_type = kCGEventRightMouseUp;
      right_dragging_ = false;
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
      if (left_dragging_) {
        mouse_type = kCGEventLeftMouseDragged;
        mouse_button = kCGMouseButtonLeft;
      } else if (right_dragging_) {
        mouse_type = kCGEventRightMouseDragged;
        mouse_button = kCGMouseButtonRight;
      } else {
        mouse_type = kCGEventMouseMoved;
        mouse_button = kCGMouseButtonLeft;
      }

      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            mouse_button);
      break;
  }

  if (mouse_event) {
    CGEventPost(kCGHIDEventTap, mouse_event);
    CFRelease(mouse_event);
  }

  return 0;
}
}  // namespace crossdesk
