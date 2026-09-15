#include "keyboard_capturer.h"

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <array>
#include <exception>

#include <errno.h>
#include <poll.h>

#include "keyboard_converter.h"
#include "linux_evdev_keycode.h"
#include "platform.h"
#include "rd_log.h"
#include "windows_key_metadata.h"

namespace crossdesk {

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

PlatformKeyboardCapturer::PlatformKeyboardCapturer()
    : display_(nullptr),
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

PlatformKeyboardCapturer::~PlatformKeyboardCapturer() {
  Unhook();
  CleanupWaylandPortal();

  if (display_) {
    std::lock_guard<std::mutex> lock(x11_injection_mutex_);
    XCloseDisplay(display_);
    display_ = nullptr;
    x11_xtest_available_ = false;
  }
}

bool PlatformKeyboardCapturer::RefreshXTestDevices() {
  // XTEST identifies injected input. Do not filter by names such as "virtual":
  // an xrdp keyboard is a legitimate controller input device.
  const Atom property = XInternAtom(capture_display_, "XTEST Device", True);
  if (property == None) return false;
  int count = 0;
  XIDeviceInfo* devices = XIQueryDevice(capture_display_, XIAllDevices, &count);
  if (!devices) return false;
  xtest_devices_.clear();
  for (int i = 0; i < count; ++i) {
    if (devices[i].use != XISlaveKeyboard) continue;
    Atom type = None;
    int format = 0;
    unsigned long items = 0, remaining = 0;
    unsigned char* value = nullptr;
    const int result = XIGetProperty(
        capture_display_, devices[i].deviceid, property, 0, 1, False,
        XA_INTEGER, &type, &format, &items, &remaining, &value);
    if (result == Success && type == XA_INTEGER && format == 8 &&
        items == 1 && value && value[0] == 1) {
      xtest_devices_.insert(devices[i].deviceid);
    }
    if (value) XFree(value);
  }
  XIFreeDeviceInfo(devices);
  return true;
}

void PlatformKeyboardCapturer::CaptureKey(int source_id, int x11_keycode,
                                          bool is_down) {
  if (!capture_has_focus_ || xtest_devices_.count(source_id) ||
      x11_keycode < 8 || x11_keycode > 255)
    return;

  // The GUI stops capture on focus loss, but its event loop may lag behind
  // the native input thread. Never forward keys typed into another window.
  Window focus = None;
  int revert = 0;
  XGetInputFocus(capture_display_, &focus, &revert);
  if (focus != capture_focus_) return;

  const uint64_t identity = (static_cast<uint64_t>(source_id) << 32) |
                            static_cast<uint32_t>(x11_keycode);
  auto found = captured_keys_.find(identity);
  if (found == captured_keys_.end()) {
    // A press can have reached the Slint fallback just before native capture
    // started. Forward its release too, so that handoff cannot leave a key down.
    CapturedKey key;
    // Use unshifted key identity, independently of the local input method.
    // Preserve it until release, even if the keyboard mapping changes.
    for (int level = 0; level < 2 && key.key_code == 0; ++level) {
      const KeySym symbol = NormalizeKeySym(XkbKeycodeToKeysym(
          capture_display_, static_cast<KeyCode>(x11_keycode), 0, level));
      if (symbol == XK_KP_Enter) {
        key = {0x0D, 0x1C, true};
      } else if (symbol == XK_ISO_Level3_Shift) {
        key.key_code = 0xA5;
      } else if (symbol == XK_grave) {
        key.key_code = 0xC0;
      } else if (const auto vk = x11KeySymToVkCode.find(symbol);
                 vk != x11KeySymToVkCode.end()) {
        key.key_code = vk->second;
      }
    }
    if (!key.key_code) return;
    if (!key.scan_code) {
      LookupWindowsKeyMetadataFromVk(key.key_code, &key.scan_code, &key.extended);
      if (!key.scan_code) {
        const int evdev = LinuxEvdevKeycodeFromWindowsVk(key.key_code);
        for (uint32_t scan = 1; scan <= 0x58; ++scan) {
          if (evdev >= 0 &&
              LinuxEvdevKeycodeFromWindowsScanCode(scan, false) == evdev) {
            key.scan_code = scan;
            break;
          }
        }
      }
    }
    found = captured_keys_.emplace(identity, key).first;
  }
  const CapturedKey key = found->second;
  if (!is_down) captured_keys_.erase(found);
  if (on_key_action_) {
    on_key_action_(key.key_code, is_down, key.scan_code, key.extended, user_ptr_);
  }
}

void PlatformKeyboardCapturer::RunCapture() {
  const int fd = ConnectionNumber(capture_display_);
  while (running_) {
    while (running_ && XPending(capture_display_) > 0) {
      XEvent event{};
      XNextEvent(capture_display_, &event);
      if (event.type == xkb_event_base_) {
        auto* xkb = reinterpret_cast<XkbEvent*>(&event);
        if (xkb->any.xkb_type == XkbMapNotify)
          XkbRefreshKeyboardMapping(&xkb->map);
      } else if (event.type == MappingNotify) {
        XRefreshKeyboardMapping(&event.xmapping);
      } else if (event.type == FocusOut) {
        // Keep focus changes in event order too: a fast switch away and back
        // may finish before either the GUI or XGetInputFocus observes it.
        capture_has_focus_ = false;
        for (const auto& [identity, key] : captured_keys_) {
          if (on_key_action_)
            on_key_action_(key.key_code, false, key.scan_code, key.extended,
                           user_ptr_);
        }
        captured_keys_.clear();
        raw_presses_.fill({});
      } else if (event.type == FocusIn) {
        capture_has_focus_ = true;
      } else if (event.type == KeyPress && !event.xkey.send_event &&
                 event.xkey.keycode < raw_presses_.size() &&
                 event.xkey.time != raw_presses_[event.xkey.keycode].time) {
        // Server-generated repeats have no matching raw press. Subscribe to
        // core events on the focused window so Slint's core/XIM delivery is
        // preserved (selecting XI_KeyPress here would take precedence over it).
        const int source = raw_presses_[event.xkey.keycode].source_id;
        const uint64_t identity = (static_cast<uint64_t>(source) << 32) |
                                  event.xkey.keycode;
        if (captured_keys_.count(identity))
          CaptureKey(source, event.xkey.keycode, true);
      } else if (event.type == GenericEvent &&
                 event.xcookie.extension == xi_opcode_ &&
                 XGetEventData(capture_display_, &event.xcookie)) {
        if (event.xcookie.evtype == XI_RawKeyPress ||
            event.xcookie.evtype == XI_RawKeyRelease) {
          const auto* key = static_cast<XIRawEvent*>(event.xcookie.data);
          const bool is_down = event.xcookie.evtype == XI_RawKeyPress;
          if (is_down && key->detail >= 0 &&
              static_cast<size_t>(key->detail) < raw_presses_.size()) {
            // Include filtered XTEST presses, so their core events cannot be
            // mistaken for repeats of a physically held key.
            raw_presses_[key->detail] = {key->time, key->sourceid};
          }
          CaptureKey(key->sourceid, key->detail, is_down);
        } else if (event.xcookie.evtype == XI_HierarchyChanged &&
                   !RefreshXTestDevices()) {
          LOG_ERROR("Failed to refresh XInput2 keyboard devices");
          running_ = false;
        }
        XFreeEventData(capture_display_, &event.xcookie);
      }
    }
    if (!running_) break;
    struct pollfd pfd = {fd, POLLIN, 0};
    const int result = poll(&pfd, 1, 50);
    if ((result < 0 && errno != EINTR) ||
        (result > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))) {
      LOG_ERROR("XInput2 keyboard capture event connection failed");
      break;
    }
  }
  running_ = false;
}

int PlatformKeyboardCapturer::Hook(OnKeyAction on_key_action, void* user_ptr) {
  if (running_) return 0;
  Unhook();
  capture_display_ = XOpenDisplay(nullptr);
  if (!capture_display_ || !on_key_action) {
    LOG_ERROR("Cannot open X11 keyboard capture display or callback is empty");
    Unhook();
    return -1;
  }

  int event = 0, error = 0;
  // XI 2.1 supplies raw-event source IDs, needed to exclude XTEST devices.
  int major = 2, minor = 1;
  if (!XQueryExtension(capture_display_, "XInputExtension", &xi_opcode_,
                       &event, &error) ||
      XIQueryVersion(capture_display_, &major, &minor) != Success || major < 2 ||
      (major == 2 && minor < 1)) {
    LOG_WARN("XInput2 keyboard capture is unavailable");
    Unhook();
    return -1;
  }
  major = XkbMajorVersion;
  minor = XkbMinorVersion;
  int xkb_opcode = 0;
  if (!XkbQueryExtension(capture_display_, &xkb_opcode, &xkb_event_base_,
                         &error, &major, &minor) || !RefreshXTestDevices()) {
    LOG_WARN("Cannot initialize XKB or enumerate XInput2 keyboard devices");
    Unhook();
    return -1;
  }

  int revert = 0;
  XGetInputFocus(capture_display_, &capture_focus_, &revert);
  if (capture_focus_ == None || capture_focus_ == PointerRoot) {
    LOG_WARN("XInput2 keyboard capture requires a focused application window");
    Unhook();
    return -1;
  }
  capture_has_focus_ = true;

  std::array<unsigned char, XIMaskLen(XI_LASTEVENT)> raw{};
  std::array<unsigned char, XIMaskLen(XI_LASTEVENT)> hierarchy{};
  XISetMask(raw.data(), XI_RawKeyPress);
  XISetMask(raw.data(), XI_RawKeyRelease);
  XISetMask(hierarchy.data(), XI_HierarchyChanged);
  XIEventMask masks[] = {
      {XIAllMasterDevices, static_cast<int>(raw.size()), raw.data()},
      {XIAllDevices, static_cast<int>(hierarchy.size()), hierarchy.data()}};
  for (int screen = 0; screen < ScreenCount(capture_display_); ++screen) {
    if (XISelectEvents(capture_display_, RootWindow(capture_display_, screen),
                       masks, 2) != Success) {
      LOG_WARN("Cannot subscribe to XInput2 keyboard events");
      Unhook();
      return -1;
    }
  }
  XSelectInput(capture_display_, capture_focus_, KeyPressMask | FocusChangeMask);
  XkbSelectEvents(capture_display_, XkbUseCoreKbd, XkbMapNotifyMask,
                   XkbMapNotifyMask);
  XSync(capture_display_, False);
  on_key_action_ = on_key_action;
  user_ptr_ = user_ptr;
  running_ = true;
  try {
    event_thread_ = std::thread([this] { RunCapture(); });
  } catch (const std::exception& error) {
    LOG_ERROR("Cannot start XInput2 keyboard capture thread: {}", error.what());
    Unhook();
    return -1;
  }
  LOG_INFO("XInput2 keyboard capture started");
  return 0;
}

int PlatformKeyboardCapturer::Unhook() {
  running_ = false;
  if (event_thread_.joinable()) event_thread_.join();
  // Closing the private connection removes every subscription and queued
  // event, so restarting capture cannot forward keys from the previous focus.
  if (capture_display_) {
    XCloseDisplay(capture_display_);
    capture_display_ = nullptr;
  }
  on_key_action_ = nullptr;
  user_ptr_ = nullptr;
  captured_keys_.clear();
  raw_presses_.fill({});
  xtest_devices_.clear();
  capture_focus_ = None;
  capture_has_focus_ = false;
  return 0;
}

int PlatformKeyboardCapturer::SendKeyboardCommand(int key_code, bool is_down,
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
