#include "mouse_controller.h"
#include "../mac_injected_event.h"

#include <remote_action.h>

#include <ApplicationServices/ApplicationServices.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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

PlatformMouseController::PlatformMouseController() {}

PlatformMouseController::~PlatformMouseController() {}

int PlatformMouseController::Init(std::vector<DisplayInfo> display_info_list) {
  display_info_list_ = display_info_list;

  return 0;
}

int PlatformMouseController::Destroy() {
  return ReleasePressedButtons() == 7 ? 0 : -1;
}

unsigned PlatformMouseController::ReleasePressedButtons(unsigned mask) {
  mask &= 7;
  const unsigned pressed = pressed_buttons_ & mask;
  if (!pressed) return mask;
  const auto cursor = CGEventCreate(nullptr);
  if (!cursor) return mask & ~pressed;
  const auto point = CGEventGetLocation(cursor);
  CFRelease(cursor);
  const CGEventType types[] = {kCGEventLeftMouseUp, kCGEventRightMouseUp,
                               kCGEventOtherMouseUp};
  const CGMouseButton buttons[] = {kCGMouseButtonLeft, kCGMouseButtonRight,
                                   kCGMouseButtonCenter};
  for (unsigned i = 0; i < 3; ++i) {
    const unsigned bit = 1u << i;
    if (!(pressed & bit)) continue;
    const auto event = CGEventCreateMouseEvent(nullptr, types[i], point, buttons[i]);
    if (!event) continue;
    CGEventSetIntegerValueField(event, kCGEventSourceUserData, kCrossDeskInjectedEvent);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    pressed_buttons_ &= ~bit;
    if (i == 0) { left_dragging_ = false; left_click_tracker_ = {}; }
    if (i == 1) { right_dragging_ = false; right_click_tracker_ = {}; }
    if (i == 2) middle_click_tracker_ = {};
  }
  return mask & ~pressed_buttons_;
}

void PlatformMouseController::UpdateDisplayInfoList(
    const std::vector<DisplayInfo>& display_info_list) {
  if (!display_info_list.empty()) {
    display_info_list_ = display_info_list;
  }
}

int PlatformMouseController::BeginClick(ClickTracker& tracker, int x, int y) {
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

int PlatformMouseController::EndClick(ClickTracker& tracker, int x, int y) {
  const int click_state = tracker.active_click_state;
  if (!IsWithinClickDistance(tracker.last_down_x, tracker.last_down_y, x, y)) {
    tracker.has_last_down = false;
    tracker.click_state = 0;
    tracker.active_click_state = 1;
  }

  return click_state;
}

int PlatformMouseController::SendMouseCommand(RemoteAction remote_action,
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
  if (!display_info.handle || display_info.width <= 0 || display_info.height <= 0) return -1;
  // A physical removal can precede both the capture callback and the UI tick.
  const auto id = static_cast<CGDirectDisplayID>(reinterpret_cast<uintptr_t>(display_info.handle));
  const CGRect bounds = CGDisplayBounds(id);
  if (CGRectIsEmpty(bounds)) return -1;

  const double normalized_x =
      std::clamp(static_cast<double>(remote_action.m.x), 0.0, 1.0);
  const double normalized_y =
      std::clamp(static_cast<double>(remote_action.m.y), 0.0, 1.0);
  // Keep the coordinate continuous until it reaches Core Graphics. This
  // avoids turning a one-pixel rounding error into a visible offset when the
  // client zooms the remote image. The upper bound remains just inside the
  // display because CG display rectangles use an exclusive right/bottom edge.
  const double left = CGRectGetMinX(bounds), top = CGRectGetMinY(bounds);
  const double max_x = std::nextafter(CGRectGetMaxX(bounds), left);
  const double max_y = std::nextafter(CGRectGetMaxY(bounds), top);
  const double mouse_pos_x = std::clamp(left + normalized_x * bounds.size.width, left, max_x);
  const double mouse_pos_y = std::clamp(top + normalized_y * bounds.size.height, top, max_y);
  const int tracked_mouse_pos_x = static_cast<int>(std::lround(mouse_pos_x));
  const int tracked_mouse_pos_y = static_cast<int>(std::lround(mouse_pos_y));

  CGEventRef mouse_event = nullptr;
  CGEventType mouse_type;
  CGMouseButton mouse_button;
  CGPoint mouse_point = CGPointMake(mouse_pos_x, mouse_pos_y);
  int click_state = 1;

  switch (remote_action.m.flag) {
    case MouseFlag::left_down:
      mouse_type = kCGEventLeftMouseDown;
      click_state = BeginClick(left_click_tracker_, tracked_mouse_pos_x,
                               tracked_mouse_pos_y);
      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            kCGMouseButtonLeft);
      SetClickState(mouse_event, click_state);
      break;
    case MouseFlag::left_up:
      mouse_type = kCGEventLeftMouseUp;
      click_state = EndClick(left_click_tracker_, tracked_mouse_pos_x,
                             tracked_mouse_pos_y);
      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            kCGMouseButtonLeft);
      SetClickState(mouse_event, click_state);
      break;
    case MouseFlag::right_down:
      mouse_type = kCGEventRightMouseDown;
      click_state = BeginClick(right_click_tracker_, tracked_mouse_pos_x,
                               tracked_mouse_pos_y);
      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            kCGMouseButtonRight);
      SetClickState(mouse_event, click_state);
      break;
    case MouseFlag::right_up:
      mouse_type = kCGEventRightMouseUp;
      click_state = EndClick(right_click_tracker_, tracked_mouse_pos_x,
                             tracked_mouse_pos_y);
      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            kCGMouseButtonRight);
      SetClickState(mouse_event, click_state);
      break;
    case MouseFlag::middle_down:
      mouse_type = kCGEventOtherMouseDown;
      click_state = BeginClick(middle_click_tracker_, tracked_mouse_pos_x,
                               tracked_mouse_pos_y);
      mouse_event = CGEventCreateMouseEvent(NULL, mouse_type, mouse_point,
                                            kCGMouseButtonCenter);
      SetClickState(mouse_event, click_state);
      break;
    case MouseFlag::middle_up:
      mouse_type = kCGEventOtherMouseUp;
      click_state = EndClick(middle_click_tracker_, tracked_mouse_pos_x,
                             tracked_mouse_pos_y);
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
    CGEventSetIntegerValueField(mouse_event, kCGEventSourceUserData,
                                kCrossDeskInjectedEvent);
    CGEventPost(kCGHIDEventTap, mouse_event);
    CFRelease(mouse_event);
    const int flag = static_cast<int>(remote_action.m.flag);
    if (flag >= 1 && flag <= 6) {
      const unsigned bit = 1u << ((flag - 1) / 2);
      if (flag & 1) pressed_buttons_ |= bit;
      else pressed_buttons_ &= ~bit;
      left_dragging_ = (pressed_buttons_ & 1) != 0;
      right_dragging_ = (pressed_buttons_ & 2) != 0;
    }
  } else {
    return -1;
  }

  return 0;
}
}  // namespace crossdesk
