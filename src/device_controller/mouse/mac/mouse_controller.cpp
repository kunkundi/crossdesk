#include "mouse_controller.h"

#include <ApplicationServices/ApplicationServices.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include "rd_log.h"

namespace crossdesk {
namespace {

constexpr auto kDoubleClickInterval = std::chrono::milliseconds(500);
constexpr int kDoubleClickMaxDistance = 8;
constexpr int kMaxClickState = 3;

bool IsWithinClickDistance(int x1, int y1, int x2, int y2) {
  return std::abs(x1 - x2) <= kDoubleClickMaxDistance &&
         std::abs(y1 - y2) <= kDoubleClickMaxDistance;
}

void SetClickState(CGEventRef event, int click_state) {
  if (!event) {
    return;
  }

  CGEventSetIntegerValueField(
      event, kCGMouseEventClickState,
      std::max(1, std::min(click_state, kMaxClickState)));
}

}  // namespace

MouseController::MouseController() {}

MouseController::~MouseController() {}

int MouseController::Init(std::vector<DisplayInfo> display_info_list) {
  display_info_list_ = display_info_list;

  return 0;
}

int MouseController::Destroy() { return 0; }

int MouseController::BeginClick(ClickTracker& tracker, int x, int y) {
  const auto now = std::chrono::steady_clock::now();
  const bool continues_previous_click =
      tracker.has_last_down &&
      now - tracker.last_down_time <= kDoubleClickInterval &&
      IsWithinClickDistance(tracker.last_down_x, tracker.last_down_y, x, y);

  tracker.click_state = continues_previous_click
                            ? std::min(tracker.click_state + 1, kMaxClickState)
                            : 1;
  tracker.active_click_state = tracker.click_state;
  tracker.has_last_down = true;
  tracker.last_down_time = now;
  tracker.last_down_x = x;
  tracker.last_down_y = y;

  return tracker.active_click_state;
}

int MouseController::EndClick(ClickTracker& tracker, int x, int y) {
  const int click_state = tracker.active_click_state;
  if (!IsWithinClickDistance(tracker.last_down_x, tracker.last_down_y, x, y)) {
    tracker.has_last_down = false;
    tracker.click_state = 0;
    tracker.active_click_state = 1;
  }

  return click_state;
}

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
  int mouse_pos_x = normalized_x * display_info.width + display_info.left;
  int mouse_pos_y = normalized_y * display_info.height + display_info.top;

  CGEventRef mouse_event = nullptr;
  CGEventType mouse_type;
  CGMouseButton mouse_button;
  CGPoint mouse_point = CGPointMake(mouse_pos_x, mouse_pos_y);
  int click_state = 1;

  switch (remote_action.m.flag) {
    case MouseFlag::left_down:
      mouse_type = kCGEventLeftMouseDown;
      left_dragging_ = true;
      click_state = BeginClick(left_click_tracker_, mouse_pos_x, mouse_pos_y);
      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            kCGMouseButtonLeft);
      SetClickState(mouse_event, click_state);
      break;
    case MouseFlag::left_up:
      mouse_type = kCGEventLeftMouseUp;
      left_dragging_ = false;
      click_state = EndClick(left_click_tracker_, mouse_pos_x, mouse_pos_y);
      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            kCGMouseButtonLeft);
      SetClickState(mouse_event, click_state);
      break;
    case MouseFlag::right_down:
      mouse_type = kCGEventRightMouseDown;
      right_dragging_ = true;
      click_state = BeginClick(right_click_tracker_, mouse_pos_x, mouse_pos_y);
      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            kCGMouseButtonRight);
      SetClickState(mouse_event, click_state);
      break;
    case MouseFlag::right_up:
      mouse_type = kCGEventRightMouseUp;
      right_dragging_ = false;
      click_state = EndClick(right_click_tracker_, mouse_pos_x, mouse_pos_y);
      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            kCGMouseButtonRight);
      SetClickState(mouse_event, click_state);
      break;
    case MouseFlag::middle_down:
      mouse_type = kCGEventOtherMouseDown;
      click_state = BeginClick(middle_click_tracker_, mouse_pos_x, mouse_pos_y);
      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            kCGMouseButtonCenter);
      SetClickState(mouse_event, click_state);
      break;
    case MouseFlag::middle_up:
      mouse_type = kCGEventOtherMouseUp;
      click_state = EndClick(middle_click_tracker_, mouse_pos_x, mouse_pos_y);
      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            kCGMouseButtonCenter);
      SetClickState(mouse_event, click_state);
      break;
    case MouseFlag::wheel_vertical:
      mouse_event = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine,
                                                  2, remote_action.m.s, 0);
      break;
    case MouseFlag::wheel_horizontal:
      mouse_event = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine,
                                                  2, 0, remote_action.m.s);
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

      mouse_event =
          CGEventCreateMouseEvent(NULL, mouse_type, mouse_point, mouse_button);
      break;
  }

  if (mouse_event) {
    CGEventPost(kCGHIDEventTap, mouse_event);
    CFRelease(mouse_event);
  }

  return 0;
}
}  // namespace crossdesk
