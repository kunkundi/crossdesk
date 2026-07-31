#include "keyboard_capturer.h"

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <errno.h>
#include <poll.h>

#include "keyboard_converter.h"
#include "linux_evdev_keycode.h"
#include "platform.h"
#include "rd_log.h"
#include "windows_key_metadata.h"

namespace crossdesk {

static OnKeyAction g_on_key_action = nullptr;
static void* g_user_ptr = nullptr;

static KeyCode ResolveX11Keycode(Display* display, int key_code,
                                 uint32_t scan_code, bool extended) {
  if (!display) {
    return 0;
  }

  const auto key_it = vkCodeToX11KeySym.find(key_code);
  if (key_it != vkCodeToX11KeySym.end()) {
    const KeyCode x11_keycode =
        XKeysymToKeycode(display, static_cast<KeySym>(key_it->second));
    if (x11_keycode != 0) {
      return x11_keycode;
    }
  }

  // Some controllers can preserve the physical Windows scan code even when
  // they cannot resolve a Windows virtual-key value. Xorg's evdev keycodes use
  // the Linux input code plus the protocol-reserved offset of 8.
  const int evdev_keycode =
      ResolveLinuxEvdevKeycodeFromWindowsKey(key_code, scan_code, extended);
  if (evdev_keycode < 0) {
    return 0;
  }

  int min_keycode = 0;
  int max_keycode = 0;
  XDisplayKeycodes(display, &min_keycode, &max_keycode);
  const int x11_keycode = evdev_keycode + 8;
  if (x11_keycode < min_keycode || x11_keycode > max_keycode) {
    return 0;
  }
  return static_cast<KeyCode>(x11_keycode);
}

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
    return;
  }

  int event_base = 0;
  int error_base = 0;
  int major_version = 0;
  int minor_version = 0;
  x11_xtest_available_ =
      XTestQueryExtension(display_, &event_base, &error_base, &major_version,
                          &minor_version) != 0;
}

KeyboardCapturer::~KeyboardCapturer() {
  Unhook();
  CleanupWaylandPortal();

  if (display_) {
    std::lock_guard<std::mutex> lock(x11_injection_mutex_);
    XCloseDisplay(display_);
    display_ = nullptr;
    x11_xtest_available_ = false;
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

  std::lock_guard<std::mutex> lock(x11_injection_mutex_);
  if (!x11_xtest_available_) {
    LOG_ERROR("XTest extension not available for keyboard injection");
    return -2;
  }

  const KeyCode x11_keycode =
      ResolveX11Keycode(display_, key_code, scan_code, extended);
  if (x11_keycode == 0) {
    LOG_WARN(
        "Cannot map remote keyboard event to X11 keycode, vk_code={}, "
        "scan_code={}, extended={}",
        key_code, scan_code, extended);
    return -3;
  }

  if (!XTestFakeKeyEvent(display_, x11_keycode, is_down, CurrentTime)) {
    LOG_ERROR(
        "XTest keyboard injection failed, vk_code={}, scan_code={}, "
        "extended={}, x11_keycode={}, is_down={}",
        key_code, scan_code, extended, static_cast<int>(x11_keycode), is_down);
    return -4;
  }

  // Complete the request before reporting success to the keyboard-state
  // reconciler. This also makes X11 protocol errors observable immediately.
  XSync(display_, False);
  return 0;
}
}  // namespace crossdesk
