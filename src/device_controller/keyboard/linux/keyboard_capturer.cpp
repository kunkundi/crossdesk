#include "keyboard_capturer.h"

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <errno.h>
#include <poll.h>

#include "keyboard_converter.h"
#include "platform.h"
#include "rd_log.h"
#include "windows_key_metadata.h"

namespace crossdesk {

static OnKeyAction g_on_key_action = nullptr;
static void* g_user_ptr = nullptr;

static KeySym NormalizeKeySym(KeySym key_sym) {
  if (key_sym >= XK_a && key_sym <= XK_z) {
    return key_sym - XK_a + XK_A;
  }
  return key_sym;
}

static int KeyboardEventHandler(Display* display, XEvent* event) {
  (void)display;
  if (event->xkey.type == KeyPress || event->xkey.type == KeyRelease) {
    KeySym key_sym = NormalizeKeySym(XLookupKeysym(&event->xkey, 0));
    auto key_it = x11KeySymToVkCode.find(static_cast<int>(key_sym));
    if (key_it == x11KeySymToVkCode.end()) {
      key_sym = NormalizeKeySym(XLookupKeysym(&event->xkey, 1));
      key_it = x11KeySymToVkCode.find(static_cast<int>(key_sym));
    }

    if (key_it == x11KeySymToVkCode.end()) {
      return 0;
    }

    int key_code = key_it->second;
    bool is_key_down = (event->xkey.type == KeyPress);
    uint32_t scan_code = 0;
    bool extended = false;
    LookupWindowsKeyMetadataFromVk(key_code, &scan_code, &extended);

    if (g_on_key_action) {
      g_on_key_action(key_code, is_key_down, scan_code, extended, g_user_ptr);
    }
  }
  return 0;
}

KeyboardCapturer::KeyboardCapturer()
    : display_(nullptr),
      root_(0),
      running_(false),
      use_wayland_portal_(false),
      wayland_init_attempted_(false),
      dbus_connection_(nullptr) {
  XInitThreads();
  display_ = XOpenDisplay(nullptr);
  if (!display_) {
    LOG_ERROR("Failed to open X display.");
  }
}

KeyboardCapturer::~KeyboardCapturer() {
  Unhook();
  CleanupWaylandPortal();

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

int KeyboardCapturer::SendKeyboardCommand(int key_code, bool is_down,
                                          uint32_t scan_code, bool extended) {
  (void)scan_code;
  (void)extended;
  if (IsWaylandSession()) {
    if (!use_wayland_portal_ && !wayland_init_attempted_) {
      wayland_init_attempted_ = true;
      if (InitWaylandPortal()) {
        use_wayland_portal_ = true;
        LOG_INFO("Keyboard controller initialized with Wayland portal backend");
      } else {
        LOG_WARN(
            "Wayland keyboard control init failed, falling back to X11/XTest "
            "backend");
      }
    }

    if (use_wayland_portal_) {
      return SendWaylandKeyboardCommand(key_code, is_down, scan_code, extended);
    }
  }

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
