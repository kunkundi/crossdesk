#include <Carbon/Carbon.h>

#include <iostream>
#include <utility>
#include <vector>

// Exercise the system event callback directly with unposted CoreGraphics events.
#define _RD_LOG_H_
#define LOG_ERROR(...) ((void)0)
#include "../src/platform/macos/input/keyboard/keyboard_capturer.cpp"

namespace {

using KeyEvents = std::vector<std::pair<int, bool>>;

void RecordKey(int key, bool down, uint32_t, bool, void* data) {
  static_cast<KeyEvents*>(data)->emplace_back(key, down);
}

bool ExpectFlagsChanged(const char* name,
                        crossdesk::PlatformKeyboardCapturer& capturer,
                        KeyEvents& forwarded, CGKeyCode key, CGEventFlags flags,
                        const KeyEvents& expected) {
  forwarded.clear();
  CGEventRef event = CGEventCreateKeyboardEvent(nullptr, key, true);
  if (!event) {
    std::cerr << name << ": failed to create keyboard event\n";
    return false;
  }
  CGEventSetType(event, kCGEventFlagsChanged);
  CGEventSetFlags(event, flags);
  crossdesk::eventCallback(nullptr, CGEventGetType(event), event, &capturer);
  CFRelease(event);

  if (forwarded == expected) return true;

  std::cerr << name << ": expected " << expected.size() << " key events, got";
  for (const auto& [code, down] : forwarded) {
    std::cerr << " 0x" << std::hex << code << (down ? " down" : " up");
  }
  std::cerr << '\n';
  return false;
}

}  // namespace

int main() {
  KeyEvents forwarded;
  crossdesk::PlatformKeyboardCapturer capturer;
  crossdesk::g_on_key_action = RecordKey;
  crossdesk::g_user_ptr = &forwarded;

  const KeyEvents caps_lock_press{{0x14, true}, {0x14, false}};
  bool ok = true;
  ok &= ExpectFlagsChanged("Caps Lock on", capturer, forwarded, kVK_CapsLock,
                           kCGEventFlagMaskAlphaShift, caps_lock_press);
  ok &= ExpectFlagsChanged("Repeated Caps Lock state", capturer, forwarded,
                           kVK_CapsLock, kCGEventFlagMaskAlphaShift, {});
  ok &= ExpectFlagsChanged("Shift down", capturer, forwarded, kVK_Shift,
                           kCGEventFlagMaskAlphaShift | kCGEventFlagMaskShift,
                           {{0xA0, true}});
  ok &= ExpectFlagsChanged("Caps Lock off with Shift held", capturer, forwarded,
                           kVK_CapsLock, kCGEventFlagMaskShift, caps_lock_press);
  ok &= ExpectFlagsChanged("Shift up", capturer, forwarded, kVK_Shift, 0,
                           {{0xA0, false}});
  if (ok) std::cout << "macOS keyboard capture: PASS\n";
  return ok ? 0 : 1;
}
