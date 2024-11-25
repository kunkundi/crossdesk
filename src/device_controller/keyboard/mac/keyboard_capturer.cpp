#include "keyboard_capturer.h"

#include "keyboard_converter.h"
#include "rd_log.h"

static OnKeyAction g_on_key_action = nullptr;
static void *g_user_ptr = nullptr;

CGEventRef eventCallback(CGEventTapProxy proxy, CGEventType type,
                         CGEventRef event, void *userInfo) {
  KeyboardCapturer *keyboard_capturer = (KeyboardCapturer *)userInfo;
  if (!keyboard_capturer) {
    LOG_ERROR("keyboard_capturer is nullptr");
    return event;
  }

  int vk_code = 0;

  if (type == kCGEventKeyDown || type == kCGEventKeyUp) {
    CGKeyCode key_code = static_cast<CGKeyCode>(
        CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
    if (CGKeyCodeToVkCode.find(key_code) != CGKeyCodeToVkCode.end()) {
      g_on_key_action(CGKeyCodeToVkCode[key_code], type == kCGEventKeyDown,
                      g_user_ptr);
    }
  } else if (type == kCGEventFlagsChanged) {
    CGEventFlags current_flags = CGEventGetFlags(event);
    CGKeyCode key_code = static_cast<CGKeyCode>(
        CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));

    // caps lock
    bool caps_lock_state = (current_flags & kCGEventFlagMaskAlphaShift) != 0;
    if (caps_lock_state != keyboard_capturer->caps_lock_flag_) {
      keyboard_capturer->caps_lock_flag_ = caps_lock_state;
      if (keyboard_capturer->caps_lock_flag_) {
        g_on_key_action(CGKeyCodeToVkCode[key_code], true, g_user_ptr);
      } else {
        g_on_key_action(CGKeyCodeToVkCode[key_code], false, g_user_ptr);
      }
    }

    // shift
    bool shift_state = (current_flags & kCGEventFlagMaskShift) != 0;
    if (shift_state != keyboard_capturer->shift_flag_) {
      keyboard_capturer->shift_flag_ = shift_state;
      if (keyboard_capturer->shift_flag_) {
        g_on_key_action(CGKeyCodeToVkCode[key_code], true, g_user_ptr);
      } else {
        g_on_key_action(CGKeyCodeToVkCode[key_code], false, g_user_ptr);
      }
    }

    // control
    bool control_state = (current_flags & kCGEventFlagMaskControl) != 0;
    if (control_state != keyboard_capturer->control_flag_) {
      keyboard_capturer->control_flag_ = control_state;
      if (keyboard_capturer->control_flag_) {
        g_on_key_action(CGKeyCodeToVkCode[key_code], true, g_user_ptr);
      } else {
        g_on_key_action(CGKeyCodeToVkCode[key_code], false, g_user_ptr);
      }
    }

    // option
    bool option_state = (current_flags & kCGEventFlagMaskAlternate) != 0;
    if (option_state != keyboard_capturer->option_flag_) {
      keyboard_capturer->option_flag_ = option_state;
      if (keyboard_capturer->option_flag_) {
        g_on_key_action(CGKeyCodeToVkCode[key_code], true, g_user_ptr);
      } else {
        g_on_key_action(CGKeyCodeToVkCode[key_code], false, g_user_ptr);
      }
    }

    // command
    bool command_state = (current_flags & kCGEventFlagMaskCommand) != 0;
    if (command_state != keyboard_capturer->command_flag_) {
      keyboard_capturer->command_flag_ = command_state;
      if (keyboard_capturer->command_flag_) {
        g_on_key_action(CGKeyCodeToVkCode[key_code], true, g_user_ptr);
      } else {
        g_on_key_action(CGKeyCodeToVkCode[key_code], false, g_user_ptr);
      }
    }
  }

  return nullptr;
}

KeyboardCapturer::KeyboardCapturer() {}

KeyboardCapturer::~KeyboardCapturer() {}

int KeyboardCapturer::Hook(OnKeyAction on_key_action, void *user_ptr) {
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

  CFRunLoopAddSource(CFRunLoopGetCurrent(), run_loop_source_,
                     kCFRunLoopCommonModes);

  CGEventTapEnable(event_tap_, true);
  return 0;
}

int KeyboardCapturer::Unhook() {
  CFRelease(run_loop_source_);
  CFRelease(event_tap_);
  return 0;
}

int KeyboardCapturer::SendKeyboardCommand(int key_code, bool is_down) {
  if (vkCodeToCGKeyCode.find(key_code) != vkCodeToCGKeyCode.end()) {
    CGKeyCode cg_key_code = vkCodeToCGKeyCode[key_code];
    CGEventRef event = CGEventCreateKeyboardEvent(NULL, cg_key_code, is_down);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
  }

  return 0;
}