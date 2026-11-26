#include <ApplicationServices/ApplicationServices.h>

#include <iostream>

namespace {

bool tap_enabled = false;

CFMachPortRef CreateTestTap(CGEventTapLocation, CGEventTapPlacement,
                           CGEventTapOptions, CGEventMask, CGEventTapCallBack,
                           void*) {
  // Use a Mach port and run-loop source without installing a keyboard tap.
  return CFMachPortCreate(nullptr, [](CFMachPortRef, void*, CFIndex, void*) {},
                          nullptr, nullptr);
}

void EnableTestTap(CFMachPortRef, bool enabled) { tap_enabled = enabled; }
bool TestTapIsEnabled(CFMachPortRef tap) { return tap && tap_enabled; }

bool Expect(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

}  // namespace

#define CGEventTapCreate CreateTestTap
#define CGEventTapEnable EnableTestTap
#define CGEventTapIsEnabled TestTapIsEnabled
#define _RD_LOG_H_
#define LOG_ERROR(...) ((void)0)
#include "../src/platform/macos/input/keyboard/keyboard_capturer.cpp"

int main() {
  crossdesk::PlatformKeyboardCapturer capturer;
  crossdesk::KeyboardCapturer& capture = capturer;
  bool ok = Expect(!capture.IsHookActive(), "An uninstalled tap is inactive");
  if (!Expect(capture.Hook(nullptr, nullptr) == 0, "Failed to install test tap"))
    return 1;
  ok &= Expect(capture.IsHookActive(), "An enabled tap is active");

  tap_enabled = false;
  ok &= Expect(!capture.IsHookActive(),
               "A tap disabled by the operating system is inactive");

  capture.Unhook();
  ok &= Expect(!capture.IsHookActive(), "A removed tap is inactive");
  if (ok) std::cout << "macOS keyboard tap state: PASS\n";
  return ok ? 0 : 1;
}
