#include "keyboard_capturer.h"

#include <errno.h>
#include <poll.h>

#include "keyboard_converter.h"
#include "rd_log.h"

namespace crossdesk {

static OnKeyAction g_on_key_action = nullptr;
static void* g_user_ptr = nullptr;

static int KeyboardEventHandler(Display* display, XEvent* event) {
  if (event->xkey.type == KeyPress || event->xkey.type == KeyRelease) {
    KeySym keySym = XLookupKeysym(&event->xkey, 0);
    int key_code = XKeysymToKeycode(display, keySym);
    bool is_key_down = (event->xkey.type == KeyPress);

    if (g_on_key_action) {
      g_on_key_action(key_code, is_key_down, g_user_ptr);
    }
  }
  return 0;
}

KeyboardCapturer::KeyboardCapturer()
    : display_(nullptr), root_(0), running_(false) {
  XInitThreads();
  display_ = XOpenDisplay(nullptr);
  if (!display_) {
    LOG_ERROR("Failed to open X display.");
  }
}

KeyboardCapturer::~KeyboardCapturer() {
  Unhook();

  if (display_) {
    XCloseDisplay(display_);
    display_ = nullptr;
  }
}

int KeyboardCapturer::Hook(OnKeyAction on_key_action, void* user_ptr) {
  if (!display_) {
    LOG_ERROR("Display not initialized.");
    return -1;
  }

  g_on_key_action = on_key_action;
  g_user_ptr = user_ptr;

  if (running_) {
    return 0;
  }

  root_ = DefaultRootWindow(display_);
  XSelectInput(display_, root_, KeyPressMask | KeyReleaseMask);
  XFlush(display_);

  running_ = true;
  const int x11_fd = ConnectionNumber(display_);
  event_thread_ = std::thread([this, x11_fd]() {
    while (running_) {
      while (running_ && XPending(display_) > 0) {
        XEvent event;
        XNextEvent(display_, &event);
        KeyboardEventHandler(display_, &event);
      }

      if (!running_) {
        break;
      }

      struct pollfd pfd = {x11_fd, POLLIN, 0};
      int poll_ret = poll(&pfd, 1, 50);
      if (poll_ret < 0) {
        if (errno == EINTR) {
          continue;
        }
        LOG_ERROR("poll for X11 events failed.");
        running_ = false;
        break;
      }

      if (poll_ret == 0) {
        continue;
      }

      if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        LOG_ERROR("poll got invalid X11 event fd state.");
        running_ = false;
        break;
      }

      if ((pfd.revents & POLLIN) == 0) {
        continue;
      }
    }
  });

  return 0;
}

int KeyboardCapturer::Unhook() {
  running_ = false;

  if (event_thread_.joinable()) {
    event_thread_.join();
  }

  g_on_key_action = nullptr;
  g_user_ptr = nullptr;

  if (display_ && root_ != 0) {
    XSelectInput(display_, root_, 0);
    XFlush(display_);
  }

  return 0;
}

int KeyboardCapturer::SendKeyboardCommand(int key_code, bool is_down) {
  if (!display_) {
    LOG_ERROR("Display not initialized.");
    return -1;
  }

  if (vkCodeToX11KeySym.find(key_code) != vkCodeToX11KeySym.end()) {
    int x11_key_code = vkCodeToX11KeySym[key_code];
    KeyCode keycode = XKeysymToKeycode(display_, x11_key_code);
    XTestFakeKeyEvent(display_, keycode, is_down, CurrentTime);
    XFlush(display_);
  }
  return 0;
}
}  // namespace crossdesk