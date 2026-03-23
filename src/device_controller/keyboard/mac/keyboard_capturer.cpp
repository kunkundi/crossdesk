#include "keyboard_capturer.h"

#include "keyboard_converter.h"
#include "rd_log.h"

namespace crossdesk {

static OnKeyAction g_on_key_action = nullptr;
static void* g_user_ptr = nullptr;

CGEventRef eventCallback(CGEventTapProxy proxy, CGEventType type,
                         CGEventRef event, void* userInfo) {
  (void)proxy;
  if (!g_on_key_action) {
    return event;
  }

  KeyboardCapturer* keyboard_capturer = (KeyboardCapturer*)userInfo;
  if (!keyboard_capturer) {
    LOG_ERROR("keyboard_capturer is nullptr");
    return event;
  }

  if (type == kCGEventKeyDown || type == kCGEventKeyUp) {
    CGKeyCode key_code = static_cast<CGKeyCode>(
        CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
    auto key_it = CGKeyCodeToVkCode.find(key_code);
    if (key_it != CGKeyCodeToVkCode.end()) {
      g_on_key_action(key_it->second, type == kCGEventKeyDown, g_user_ptr);
    }
  } else if (type == kCGEventFlagsChanged) {
    CGEventFlags current_flags = CGEventGetFlags(event);
    CGKeyCode key_code = static_cast<CGKeyCode>(
        CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
    auto key_it = CGKeyCodeToVkCode.find(key_code);
    if (key_it == CGKeyCodeToVkCode.end()) {
      return nullptr;
    }
    const int vk_code = key_it->second;

    // caps lock
    bool caps_lock_state = (current_flags & kCGEventFlagMaskAlphaShift) != 0;
    if (caps_lock_state != keyboard_capturer->caps_lock_flag_) {
      keyboard_capturer->caps_lock_flag_ = caps_lock_state;
      g_on_key_action(vk_code, keyboard_capturer->caps_lock_flag_, g_user_ptr);
    }

    // shift
    bool shift_state = (current_flags & kCGEventFlagMaskShift) != 0;
    if (shift_state != keyboard_capturer->shift_flag_) {
      keyboard_capturer->shift_flag_ = shift_state;
      g_on_key_action(vk_code, keyboard_capturer->shift_flag_, g_user_ptr);
    }

    // control
    bool control_state = (current_flags & kCGEventFlagMaskControl) != 0;
    if (control_state != keyboard_capturer->control_flag_) {
      keyboard_capturer->control_flag_ = control_state;
      g_on_key_action(vk_code, keyboard_capturer->control_flag_, g_user_ptr);
    }

    // option
    bool option_state = (current_flags & kCGEventFlagMaskAlternate) != 0;
    if (option_state != keyboard_capturer->option_flag_) {
      keyboard_capturer->option_flag_ = option_state;
      g_on_key_action(vk_code, keyboard_capturer->option_flag_, g_user_ptr);
    }

    // command
    bool command_state = (current_flags & kCGEventFlagMaskCommand) != 0;
    if (command_state != keyboard_capturer->command_flag_) {
      keyboard_capturer->command_flag_ = command_state;
      g_on_key_action(vk_code, keyboard_capturer->command_flag_, g_user_ptr);
    }
  }

  return nullptr;
}

KeyboardCapturer::KeyboardCapturer()
    : event_tap_(nullptr), run_loop_source_(nullptr) {}

KeyboardCapturer::~KeyboardCapturer() { Unhook(); }

int KeyboardCapturer::Hook(OnKeyAction on_key_action, void* user_ptr) {
  if (event_tap_) {
    return 0;
  }

  g_on_key_action = on_key_action;
  g_user_ptr = user_ptr;

  CGEventMask eventMask = (1 << kCGEventKeyDown) | (1 << kCGEventKeyUp) |
                          (1 << kCGEventFlagsChanged);

  event_tap_ = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
                                kCGEventTapOptionDefault, eventMask,
                                eventCallback, this);

  if (!event_tap_) {
    LOG_ERROR("CGEventTapCreate failed");
    return -1;
  }

  run_loop_source_ =
      CFMachPortCreateRunLoopSource(kCFAllocatorDefault, event_tap_, 0);
  if (!run_loop_source_) {
    LOG_ERROR("CFMachPortCreateRunLoopSource failed");
    CFRelease(event_tap_);
    event_tap_ = nullptr;
    return -1;
  }

  CFRunLoopAddSource(CFRunLoopGetCurrent(), run_loop_source_,
                     kCFRunLoopCommonModes);

  const CGEventFlags current_flags =
      CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
  caps_lock_flag_ = (current_flags & kCGEventFlagMaskAlphaShift) != 0;
  shift_flag_ = (current_flags & kCGEventFlagMaskShift) != 0;
  control_flag_ = (current_flags & kCGEventFlagMaskControl) != 0;
  option_flag_ = (current_flags & kCGEventFlagMaskAlternate) != 0;
  command_flag_ = (current_flags & kCGEventFlagMaskCommand) != 0;

  CGEventTapEnable(event_tap_, true);
  return 0;
}

int KeyboardCapturer::Unhook() {
  g_on_key_action = nullptr;
  g_user_ptr = nullptr;

  if (event_tap_) {
    CGEventTapEnable(event_tap_, false);
  }

  if (run_loop_source_) {
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), run_loop_source_,
                          kCFRunLoopCommonModes);
    CFRelease(run_loop_source_);
    run_loop_source_ = nullptr;
  }

  if (event_tap_) {
    CFRelease(event_tap_);
    event_tap_ = nullptr;
  }

  return 0;
}

inline bool IsFunctionKey(int key_code) {
  switch (key_code) {
    case 0x7A:
    case 0x78:
    case 0x63:
    case 0x76:
    case 0x60:
    case 0x61:
    case 0x62:
    case 0x64:
    case 0x65:
    case 0x6D:
    case 0x67:
    case 0x6F:
      return true;
    default:
      return false;
  }
}

int KeyboardCapturer::SendKeyboardCommand(int key_code, bool is_down) {
  if (vkCodeToCGKeyCode.find(key_code) != vkCodeToCGKeyCode.end()) {
    CGKeyCode cg_key_code = vkCodeToCGKeyCode[key_code];
    CGEventRef event = CGEventCreateKeyboardEvent(NULL, cg_key_code, is_down);
    if (!event) {
      LOG_ERROR("CGEventCreateKeyboardEvent failed");
      return -1;
    }

    CGEventSetFlags(event, 0);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);

    // F1-F12 keys often require the FN key to be pressed on Mac keyboards, so
    // we simulate the FN key release when an F1-F12 key is released.
    if (IsFunctionKey(cg_key_code) && !is_down) {
      CGEventRef fn_release_event =
          CGEventCreateKeyboardEvent(NULL, fn_key_code_, false);
      CGEventPost(kCGHIDEventTap, fn_release_event);
      CFRelease(fn_release_event);
    }
  }

  return 0;
}
}  // namespace crossdesk
