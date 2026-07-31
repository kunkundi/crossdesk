#include <X11/Xlib.h>

#include <chrono>
#include <cstdio>
#include <thread>

#include "keyboard_capturer.h"

int main() {
  Display* receiver = XOpenDisplay(nullptr);
  if (!receiver) {
    std::fprintf(stderr, "open display failed\n");
    return 1;
  }

  const int screen = DefaultScreen(receiver);
  Window window = XCreateSimpleWindow(receiver, RootWindow(receiver, screen),
                                      0, 0, 100, 100, 0, 0, 0);
  XSelectInput(receiver, window, KeyPressMask | KeyReleaseMask);
  XMapWindow(receiver, window);
  XSync(receiver, False);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  XSetInputFocus(receiver, window, RevertToParent, CurrentTime);
  XSync(receiver, False);

  crossdesk::KeyboardCapturer keyboard;
  // Exercise the scan-code fallback: the controller supplied no usable VK,
  // but did preserve the set-1 scan code for the A key.
  const int down = keyboard.SendKeyboardCommand(0, true, 0x1E, false);
  const int up = keyboard.SendKeyboardCommand(0, false, 0x1E, false);

  bool saw_down = false;
  bool saw_up = false;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline &&
         (!saw_down || !saw_up)) {
    while (XPending(receiver) > 0) {
      XEvent event{};
      XNextEvent(receiver, &event);
      saw_down = saw_down || event.type == KeyPress;
      saw_up = saw_up || event.type == KeyRelease;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  XDestroyWindow(receiver, window);
  XCloseDisplay(receiver);
  std::printf("down_ret=%d up_ret=%d saw_down=%d saw_up=%d\n", down, up,
              saw_down, saw_up);
  return down == 0 && up == 0 && saw_down && saw_up ? 0 : 2;
}
