/* Copyright (c) 2026 by DI JUNKUN, All Rights Reserved. */
#include "privacy_guard.h"

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace crossdesk {
namespace {
using Clock = std::chrono::steady_clock;
using Gamma = std::unique_ptr<XRRCrtcGamma, decltype(&XRRFreeGamma)>;
using Resources =
    std::unique_ptr<XRRScreenResources, decltype(&XRRFreeScreenResources)>;
using CrtcInfo = std::unique_ptr<XRRCrtcInfo, decltype(&XRRFreeCrtcInfo)>;
volatile std::sig_atomic_t stopping = 0;
int x_error = 0;

void StopGuard(int) { stopping = 1; }
int OnXError(Display*, XErrorEvent* event) {
  x_error = event->error_code;
  return 0;
}

template <size_t N>
void SetText(char (&dest)[N], const std::string& text) {
  std::strncpy(dest, text.c_str(), N - 1);
  dest[N - 1] = '\0';
}

struct Output {
  Window root = 0;
  RRCrtc crtc = 0;
  RRMode mode = 0;
  int x = 0, y = 0;
  unsigned width = 0, height = 0;
  Rotation rotation = 0;
  std::vector<RROutput> connectors;
  Gamma saved{nullptr, XRRFreeGamma};

  bool SameLayout(const Output& other) const {
    return std::tie(root, crtc, mode, x, y, width, height, rotation,
                    connectors) == std::tie(other.root, other.crtc, other.mode,
                                            other.x, other.y, other.width,
                                            other.height, other.rotation,
                                            other.connectors);
  }
};

struct Device {
  int id = 0, use = 0, attachment = 0;
  bool keyboard = false, touch = false;
  std::string name;

  bool operator==(const Device& other) const {
    return std::tie(id, use, attachment, keyboard, touch, name) ==
           std::tie(other.id, other.use, other.attachment, other.keyboard,
                    other.touch, other.name);
  }
};

bool IsBlack(const XRRCrtcGamma* gamma) {
  if (!gamma || gamma->size <= 0) return false;
  for (int i = 0; i < gamma->size; ++i)
    if (gamma->red[i] || gamma->green[i] || gamma->blue[i]) return false;
  return true;
}

// This class lives only in the exec'd guard process. Its Xlib error handler is
// isolated from the GUI and capture threads' process-global error handlers.
class NativePrivacy {
 public:
  ~NativePrivacy() {
    Recover();
    if (display_) XCloseDisplay(display_);
  }

  void Query() {
    reply_.supported = reply_.input_supported = false;
    std::string error;
    if (!Initialize(error)) {
      SetText(reply_.capability_reason, error);
      return;
    }
    std::vector<Output> outputs;
    std::vector<Device> devices;
    if (!ReadOutputs(outputs, true, error) || !ReadDevices(devices, error)) {
      SetText(reply_.capability_reason, error);
      return;
    }
    const bool keyboard = std::any_of(devices.begin(), devices.end(),
                                      [](const auto& d) { return d.keyboard; });
    if (!keyboard) {
      SetText(reply_.capability_reason,
              "No local keyboard available for privacy recovery");
      return;
    }
    reply_.supported = true;
    // XIGrabDevice does not suppress XI 2.2 touch delivery. Do not advertise
    // input protection for touchscreens without a separate touch grab.
    reply_.input_supported = std::none_of(
        devices.begin(), devices.end(), [](const auto& d) { return d.touch; });
    SetText(reply_.capability_reason,
            reply_.input_supported
                ? "X11 privacy black screen available; local recovery: "
                  "Ctrl+Alt+Shift+F12"
                : "X11 privacy black screen available; touchscreen input "
                  "blocking is unavailable");
  }

  void SetLocalUnlockAllowed(bool allowed) { allow_local_unlock_ = allowed; }

  void Enable(bool block_input) {
    ++reply_.generation;
    reply_.failure[0] = '\0';
    reply_.emergency = false;
    if (!reply_.recovered) {
      Fail("Linux privacy resources are still in use");
      return;
    }
    Query();
    if (!reply_.supported || (block_input && !reply_.input_supported)) {
      Fail(reply_.capability_reason);
      return;
    }
    std::string error;
    keys_.clear();
    if (!Acquire(error) || !ReadOutputs(outputs_, true, error) ||
        !ReadDevices(devices_, error) || !ProtectInput(block_input, error)) {
      Fail(error);
      return;
    }
    // No display ramp is changed until every original ramp has been saved.
    // Gamma is applied at scanout, after XGetImage/DRM framebuffer capture.
    // There is deliberately no cover window to contaminate remote frames.
    x_error = 0;
    for (const auto& output : outputs_) {
      if (!Blank(output.crtc, output.saved->size)) {
        Fail("Cannot blank Linux display");
        return;
      }
    }
    for (int i = 0; i < ScreenCount(display_); ++i) {
      XFixesHideCursor(display_, RootWindow(display_, i));
      hidden_roots_.push_back(RootWindow(display_, i));
    }
    XSync(display_, False);
    if (x_error) {
      Fail("Cannot blank Linux displays or hide the local cursor");
      return;
    }
    // Active slave grabs temporarily detach devices. Snapshot after grabbing,
    // and monitor subsequent hardware/hierarchy changes during protection.
    if (!ReadDevices(devices_, error)) {
      Fail(error);
      return;
    }
    reply_.active = true;
    Check();
  }

  void Recover() {
    reply_.active = false;
    if (!display_) {
      reply_.recovered = true;
      return;
    }
    for (int id : grabbed_) XIUngrabDevice(display_, id, CurrentTime);
    grabbed_.clear();
    for (Window root : hidden_roots_) XFixesShowCursor(display_, root);
    hidden_roots_.clear();
    // Keep failed restorations for retry. Removed CRTCs no longer own a local
    // output; all still-present CRTCs must acknowledge the original ramp.
    for (auto it = outputs_.begin(); it != outputs_.end();) {
      x_error = 0;
      Resources resources(XRRGetScreenResourcesCurrent(display_, it->root),
                          XRRFreeScreenResources);
      if (!resources) {
        ++it;
        continue;
      }
      const bool exists =
          std::find(resources->crtcs, resources->crtcs + resources->ncrtc,
                    it->crtc) != resources->crtcs + resources->ncrtc;
      if (exists && it->saved) {
        if (!RestoreRamp(*it)) {
          ++it;
          continue;
        }
        XSync(display_, False);
        if (x_error) {
          ++it;
          continue;
        }
      }
      it = outputs_.erase(it);
    }
    keys_.clear();
    if (outputs_.empty() && owner_) {
      XDestroyWindow(display_, owner_);
      owner_ = 0;
    }
    XSync(display_, False);
    reply_.recovered = outputs_.empty() && !owner_;
  }

  void Fail(const std::string& reason) {
    SetText(reply_.failure, reason);
    Recover();
  }

  void Poll() {
    if (!display_) return;
    bool display_event = false;
    while (XPending(display_)) {
      XEvent event{};
      XNextEvent(display_, &event);
      if (event.type == randr_event_base_ + RRScreenChangeNotify ||
          event.type == randr_event_base_ + RRNotify) {
        display_event = true;
        continue;
      }
      if (event.type != GenericEvent || event.xcookie.extension != xi_opcode_ ||
          !XGetEventData(display_, &event.xcookie))
        continue;
      if (reply_.active) {
        const int type = event.xcookie.evtype;
        if (type == XI_KeyPress || type == XI_KeyRelease) {
          const auto* key = static_cast<XIDeviceEvent*>(event.xcookie.data);
          Key(key->sourceid, key->detail, type == XI_KeyPress);
        } else if (!blocking_ &&
                   (type == XI_RawKeyPress || type == XI_RawKeyRelease)) {
          const auto* key = static_cast<XIRawEvent*>(event.xcookie.data);
          Key(key->sourceid, key->detail, type == XI_RawKeyPress);
        }
      }
      XFreeEventData(display_, &event.xcookie);
    }
    if (reply_.active &&
        (display_event || Clock::now() - checked_ >= std::chrono::milliseconds(100)))
      Check(display_event);
    if (!reply_.active && !reply_.recovered) Recover();
  }

  const LinuxPrivacyReply& Reply() const { return reply_; }
  int Connection() const { return display_ ? ConnectionNumber(display_) : -1; }
  void AcknowledgeEmergency() { reply_.emergency = false; }

 private:
  bool Initialize(std::string& error) {
    if (initialized_) return true;
    const char* session = std::getenv("XDG_SESSION_TYPE");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    if ((session && std::strcmp(session, "wayland") == 0) ||
        (wayland && *wayland)) {
      error =
          "Linux privacy screen requires an X11 session; Wayland is "
          "unsupported";
      return false;
    }
    const char* address = std::getenv("DISPLAY");
    if (!address ||
        (*address != ':' && std::strncmp(address, "unix:", 5) != 0)) {
      error = "Linux privacy screen requires a local X11 display";
      return false;
    }
    if (!display_) display_ = XOpenDisplay(nullptr);
    if (!display_) {
      error = "Cannot open the local X11 display";
      return false;
    }
    int event = 0, code = 0, opcode = 0;
    int major = 1, minor = 3;
    if (XQueryExtension(display_, "XWAYLAND", &opcode, &event, &code)) {
      error = "XWayland cannot protect native Wayland displays";
      return false;
    }
    if (!XRRQueryExtension(display_, &randr_event_base_, &code) ||
        !XRRQueryVersion(display_, &major, &minor) || major < 1 ||
        (major == 1 && minor < 3)) {
      error = "Linux privacy screen requires RandR 1.3 display gamma control";
      return false;
    }
    major = 4;
    minor = 0;
    if (!XFixesQueryExtension(display_, &event, &code) ||
        !XFixesQueryVersion(display_, &major, &minor) || major < 4) {
      error = "Linux privacy screen requires XFixes 4 cursor hiding";
      return false;
    }
    major = 2;
    minor = 2;
    if (!XQueryExtension(display_, "XInputExtension", &xi_opcode_, &event,
                         &code) ||
        XIQueryVersion(display_, &major, &minor) != Success || major < 2) {
      error = "Linux privacy screen requires XInput 2 local input monitoring";
      return false;
    }
    xtest_property_ = XInternAtom(display_, "XTEST Device", True);
    if (!xtest_property_ ||
        !XTestQueryExtension(display_, &event, &code, &major, &minor)) {
      error = "Cannot identify X11 remote input devices";
      return false;
    }
    selection_ = XInternAtom(display_, "_CROSSDESK_PRIVACY_GUARD", False);
    std::array<unsigned char, XIMaskLen(XI_LASTEVENT)> bits{};
    XISetMask(bits.data(), XI_RawKeyPress);
    XISetMask(bits.data(), XI_RawKeyRelease);
    XIEventMask mask{XIAllDevices, static_cast<int>(bits.size()), bits.data()};
    x_error = 0;
    for (int i = 0; i < ScreenCount(display_); ++i) {
      const auto root = RootWindow(display_, i);
      XISelectEvents(display_, root, &mask, 1);
      XRRSelectInput(display_, root, RRScreenChangeNotifyMask |
                     RRCrtcChangeNotifyMask | RROutputChangeNotifyMask);
    }
    XSync(display_, False);
    if (x_error) {
      error = "Cannot monitor the local privacy recovery shortcut";
      return false;
    }
    initialized_ = true;
    return true;
  }

  bool SaveRamp(Output& output, std::string& error) {
    output.saved.reset(XRRGetCrtcGamma(display_, output.crtc));
    if (output.saved && output.saved->size > 0 && !IsBlack(output.saved.get())) return true;
    error = "Display gamma control is unavailable or the display is already blank";
    return false;
  }

  bool Blank(RRCrtc crtc, int size) {
    if (size <= 0) return false;
    Gamma black(XRRAllocGamma(size), XRRFreeGamma);
    if (!black) return false;
    std::fill_n(black->red, size, 0);
    std::fill_n(black->green, size, 0);
    std::fill_n(black->blue, size, 0);
    XRRSetCrtcGamma(display_, crtc, black.get());
    return true;
  }

  bool RestoreRamp(const Output& output) {
    const int size = XRRGetCrtcGammaSize(display_, output.crtc);
    if (size <= 0) return false;
    if (size == output.saved->size) {
      XRRSetCrtcGamma(display_, output.crtc, output.saved.get());
      return true;
    }
    // A mode change can change the hardware LUT size. Preserve the saved
    // channel curves, including their endpoints, in the new table.
    Gamma scaled(XRRAllocGamma(size), XRRFreeGamma);
    if (!scaled) return false;
    for (int i = 0; i < size; ++i) {
      const double position = double(i) * (output.saved->size - 1) / std::max(1, size - 1);
      const int lo = static_cast<int>(position), hi = std::min(lo + 1, output.saved->size - 1);
      const auto sample = [&](const unsigned short* channel) {
        return static_cast<unsigned short>(channel[lo] + (position - lo) * (channel[hi] - channel[lo]));
      };
      scaled->red[i] = sample(output.saved->red);
      scaled->green[i] = sample(output.saved->green);
      scaled->blue[i] = sample(output.saved->blue);
    }
    XRRSetCrtcGamma(display_, output.crtc, scaled.get());
    return true;
  }

  bool ReadOutputs(std::vector<Output>& outputs, bool save,
                   std::string& error) {
    outputs.clear();
    x_error = 0;
    for (int screen = 0; screen < ScreenCount(display_); ++screen) {
      const Window root = RootWindow(display_, screen);
      Resources resources(XRRGetScreenResourcesCurrent(display_, root),
                          XRRFreeScreenResources);
      if (!resources) {
        error = "Cannot enumerate Linux display outputs";
        return false;
      }
      for (int i = 0; i < resources->ncrtc; ++i) {
        CrtcInfo info(
            XRRGetCrtcInfo(display_, resources.get(), resources->crtcs[i]),
            XRRFreeCrtcInfo);
        if (!info) {
          error = "Cannot inspect Linux display controller";
          return false;
        }
        if (!info->mode || !info->noutput) continue;
        Output output;
        output.root = root;
        output.crtc = resources->crtcs[i];
        output.mode = info->mode;
        output.x = info->x;
        output.y = info->y;
        output.width = info->width;
        output.height = info->height;
        output.rotation = info->rotation;
        output.connectors.assign(info->outputs, info->outputs + info->noutput);
        std::sort(output.connectors.begin(), output.connectors.end());
        if (save && !SaveRamp(output, error)) return false;
        outputs.push_back(std::move(output));
      }
    }
    XSync(display_, False);
    if (x_error || (save && outputs.empty())) {
      error = "No controllable Linux display outputs";
      return false;
    }
    return true;
  }

  bool ReadDevices(std::vector<Device>& devices, std::string& error) {
    devices.clear();
    x_error = 0;
    int count = 0;
    XIDeviceInfo* info = XIQueryDevice(display_, XIAllDevices, &count);
    if (!info) {
      error = "Cannot enumerate local X11 input devices";
      return false;
    }
    for (int i = 0; i < count; ++i) {
      const auto& d = info[i];
      if (!d.enabled || d.use == XIMasterPointer || d.use == XIMasterKeyboard)
        continue;
      Atom type = None;
      int format = 0;
      unsigned long items = 0, remaining = 0;
      unsigned char* value = nullptr;
      const int result =
          XIGetProperty(display_, d.deviceid, xtest_property_, 0, 1, False,
                        XA_INTEGER, &type, &format, &items, &remaining, &value);
      const bool injected = result == Success && type == XA_INTEGER &&
                            format == 8 && items == 1 && value && value[0] == 1;
      if (value) XFree(value);
      if (injected) continue;
      Device device;
      device.id = d.deviceid;
      device.use = d.use;
      device.attachment = d.attachment;
      device.name = d.name ? d.name : "";
      for (int c = 0; c < d.num_classes; ++c) {
        device.keyboard |= d.classes[c]->type == XIKeyClass;
        device.touch |= d.classes[c]->type == XITouchClass;
      }
      devices.push_back(std::move(device));
    }
    XIFreeDeviceInfo(info);
    XSync(display_, False);
    if (x_error) {
      error = "Cannot verify local X11 input devices";
      return false;
    }
    return true;
  }

  bool Acquire(std::string& error) {
    // A second instance must not save the first instance's black ramps as its
    // originals. Selection ownership and creation are atomic on this server.
    XGrabServer(display_);
    if (XGetSelectionOwner(display_, selection_) == None) {
      owner_ = XCreateSimpleWindow(display_, DefaultRootWindow(display_), 0, 0,
                                   1, 1, 0, 0, 0);
      XSetSelectionOwner(display_, selection_, owner_, CurrentTime);
    }
    XUngrabServer(display_);
    XSync(display_, False);
    if (!owner_ || XGetSelectionOwner(display_, selection_) != owner_) {
      error = "Another CrossDesk instance owns Linux privacy protection";
      return false;
    }
    reply_.recovered = false;
    return true;
  }

  bool ProtectInput(bool block_input, std::string& error) {
    blocking_ = block_input;
    if (!block_input) return true;
    for (const auto& device : devices_) {
      if (device.touch) {
        error = "Touchscreen privacy input blocking is unavailable";
        return false;
      }
      if (device.keyboard) {
        // Preserve held local modifiers for the emergency chord independently
        // of the releases sent to the master keyboard below.
        XDevice* keyboard = XOpenDevice(display_, device.id);
        XDeviceState* state =
            keyboard ? XQueryDeviceState(display_, keyboard) : nullptr;
        if (!state) {
          if (keyboard) XCloseDevice(display_, keyboard);
          error = "Cannot inspect held local keyboard input";
          return false;
        }
        XInputClass* input = state->data;
        for (int i = 0; i < state->num_classes; ++i) {
          if (input->c_class == KeyClass) {
            const auto* keys = reinterpret_cast<XKeyState*>(input);
            for (int key = 0; key < 256; ++key)
              keys_[device.id][key] = keys->keys[key / 8] & (1 << (key % 8));
          }
          input = reinterpret_cast<XInputClass*>(
              reinterpret_cast<char*>(input) + input->length);
        }
        XFreeDeviceState(state);
        XCloseDevice(display_, keyboard);
      }
      std::array<unsigned char, XIMaskLen(XI_LASTEVENT)> bits{};
      for (int type : {XI_KeyPress, XI_KeyRelease, XI_ButtonPress,
                       XI_ButtonRelease, XI_Motion})
        XISetMask(bits.data(), type);
      XIEventMask mask{device.id, static_cast<int>(bits.size()), bits.data()};
      const int result = XIGrabDevice(
          display_, device.id, DefaultRootWindow(display_), CurrentTime, None,
          GrabModeAsync, GrabModeAsync, False, &mask);
      if (result != GrabSuccess) {
        error = "Cannot grab every local X11 input device";
        return false;
      }
      grabbed_.push_back(device.id);
    }
    return ReleaseHeldInput(error);
  }

  bool ReleaseHeldInput(std::string& error) {
    // Slave grabs float physical devices, but their previous presses can
    // remain on the master. Release them after all grabs are installed so a
    // held local Ctrl/button cannot alter the remote controller's next input.
    int original_pointer = 0, count = 0;
    XIGetClientPointer(display_, None, &original_pointer);
    XIDeviceInfo* masters = XIQueryDevice(display_, XIAllMasterDevices, &count);
    if (!masters) {
      error = "Cannot inspect held X11 input";
      return false;
    }
    x_error = 0;
    bool ok = true;
    for (int i = 0; i < count; ++i) {
      if (masters[i].use != XIMasterPointer || !masters[i].enabled) continue;
      XISetClientPointer(display_, None, masters[i].deviceid);
      char keys[32]{};
      XQueryKeymap(display_, keys);
      for (int key = 8; key < 256; ++key)
        if (keys[key / 8] & (1 << (key % 8)))
          ok &= XTestFakeKeyEvent(display_, key, False, CurrentTime) != 0;
      Window root = None, child = None;
      double root_x = 0, root_y = 0, win_x = 0, win_y = 0;
      XIButtonState buttons{};
      XIModifierState modifiers{};
      XIGroupState group{};
      XIQueryPointer(display_, masters[i].deviceid, DefaultRootWindow(display_),
                     &root, &child, &root_x, &root_y, &win_x, &win_y, &buttons,
                     &modifiers, &group);
      for (int button = 1; button < buttons.mask_len * 8; ++button)
        if (XIMaskIsSet(buttons.mask, button))
          ok &= XTestFakeButtonEvent(display_, button, False, CurrentTime) != 0;
      if (buttons.mask) XFree(buttons.mask);
      XSync(display_, False);
      XQueryKeymap(display_, keys);
      for (char pressed : keys) ok &= pressed == 0;
      buttons = {};
      XIQueryPointer(display_, masters[i].deviceid, DefaultRootWindow(display_),
                     &root, &child, &root_x, &root_y, &win_x, &win_y, &buttons,
                     &modifiers, &group);
      for (int byte = 0; byte < buttons.mask_len; ++byte)
        ok &= buttons.mask[byte] == 0;
      if (buttons.mask) XFree(buttons.mask);
    }
    XIFreeDeviceInfo(masters);
    if (original_pointer) XISetClientPointer(display_, None, original_pointer);
    XSync(display_, False);
    if (!ok || x_error) {
      error = "Cannot release held X11 input for privacy";
      return false;
    }
    return true;
  }

  void Key(int source, int keycode, bool down) {
    if (keycode < 0 || keycode >= 256 ||
        std::none_of(devices_.begin(), devices_.end(), [source](const auto& d) {
          return d.id == source && d.keyboard;
        }))
      return;
    keys_[source][keycode] = down;
    bool ctrl = false, alt = false, shift = false;
    for (const auto& [id, keys] : keys_) {
      for (int code = 0; code < 256; ++code) {
        if (!keys[code]) continue;
        const KeySym symbol = XkbKeycodeToKeysym(display_, code, 0, 0);
        ctrl |= symbol == XK_Control_L || symbol == XK_Control_R;
        alt |= symbol == XK_Alt_L || symbol == XK_Alt_R;
        shift |= symbol == XK_Shift_L || symbol == XK_Shift_R;
      }
    }
    if (allow_local_unlock_ && down && ctrl && alt && shift &&
        XkbKeycodeToKeysym(display_, keycode, 0, 0) == XK_F12) {
      reply_.emergency = true;
      Recover();
    }
  }

  void Check(bool display_event = false) {
    checked_ = Clock::now();
    std::string error;
    std::vector<Output> current;
    std::vector<Device> devices;
    if (!ReadOutputs(current, false, error) || !ReadDevices(devices, error)) {
      Fail(error);
      return;
    }
    if (devices != devices_) {
      Fail("Local input devices changed; Linux privacy has been turned off");
      return;
    }
    x_error = 0;
    const auto active_before = std::count_if(outputs_.begin(), outputs_.end(),
        [](const Output& old) { return old.mode != None; });
    const bool layout_changed = display_event || static_cast<size_t>(active_before) != current.size() ||
        std::any_of(current.begin(), current.end(), [&](const Output& output) {
          return std::none_of(outputs_.begin(), outputs_.end(),
                              [&](const Output& old) { return old.SameLayout(output); });
        });
    // A modeset can reset the LUT on other CRTCs as well. Reapply every
    // active ramp, retaining originals even while a CRTC is disconnected.
    if (layout_changed) {
      for (auto& old : outputs_) old.mode = None;
      for (auto& output : current) {
        const auto crtc = output.crtc;
        auto old = std::find_if(outputs_.begin(), outputs_.end(), [&](const Output& saved) {
          return saved.crtc == crtc && saved.root == output.root;
        });
        if (old == outputs_.end()) {
          if (!SaveRamp(output, error)) {
            Fail(error);
            return;
          }
          outputs_.push_back(std::move(output));
        } else {
          output.saved = std::move(old->saved);
          *old = std::move(output);
        }
        if (!Blank(crtc, XRRGetCrtcGammaSize(display_, crtc))) {
          Fail("Cannot protect a changed Linux display");
          return;
        }
      }
    }
    for (const auto& output : outputs_) {
      if (!output.mode) continue;
      Gamma ramp(XRRGetCrtcGamma(display_, output.crtc), XRRFreeGamma);
      if (!IsBlack(ramp.get())) {
        Fail(
            "Display gamma protection was changed; Linux privacy is "
            "unavailable");
        return;
      }
    }
    XSync(display_, False);
    if (x_error || XGetSelectionOwner(display_, selection_) != owner_)
      Fail("Cannot verify Linux privacy protection");
  }

  Display* display_ = nullptr;
  bool initialized_ = false;
  bool blocking_ = false;
  int xi_opcode_ = 0;
  int randr_event_base_ = 0;
  Atom xtest_property_ = None, selection_ = None;
  Window owner_ = 0;
  std::vector<Output> outputs_;
  std::vector<Device> devices_;
  std::vector<int> grabbed_;
  std::vector<Window> hidden_roots_;
  std::map<int, std::array<bool, 256>> keys_;
  LinuxPrivacyReply reply_;
  bool allow_local_unlock_ = true;
  Clock::time_point checked_{};
};
}  // namespace

int RunLinuxPrivacyGuard() {
  const int socket = kLinuxPrivacyGuardSocket;
  int type = 0;
  socklen_t length = sizeof(type);
  if (getsockopt(socket, SOL_SOCKET, SO_TYPE, &type, &length) != 0 ||
      type != SOCK_SEQPACKET)
    return 1;
  fcntl(socket, F_SETFD, FD_CLOEXEC);
  fcntl(socket, F_SETFL, O_NONBLOCK);
  struct sigaction action {};
  action.sa_handler = StopGuard;
  sigemptyset(&action.sa_mask);
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGHUP, &action, nullptr);
  XSetErrorHandler(OnXError);
  NativePrivacy privacy;
  auto heartbeat = Clock::now();
  bool connected = true;
  bool dirty = false;
  while (connected && !stopping) {
    pollfd fds[] = {{socket, POLLIN, 0}, {privacy.Connection(), POLLIN, 0}};
    poll(fds, 2, privacy.Reply().active ? 25 : 250);
    if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
    for (int i = 0; i < 64; ++i) {
      LinuxPrivacyCommand command;
      const auto bytes = recv(socket, &command, sizeof(command), 0);
      if (bytes < 0 &&
          (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        break;
      if (bytes != sizeof(command)) {
        connected = false;
        break;
      }
      heartbeat = Clock::now();
      switch (command) {
        case LinuxPrivacyCommand::query:
          if (!privacy.Reply().active) privacy.Query();
          break;
        case LinuxPrivacyCommand::enable:
          privacy.Enable(false);
          break;
        case LinuxPrivacyCommand::enable_blocking:
          privacy.Enable(true);
          break;
        case LinuxPrivacyCommand::recover:
          privacy.Recover();
          break;
        case LinuxPrivacyCommand::allow_local_unlock:
          privacy.SetLocalUnlockAllowed(true);
          break;
        case LinuxPrivacyCommand::forbid_local_unlock:
          privacy.SetLocalUnlockAllowed(false);
          break;
        case LinuxPrivacyCommand::heartbeat:
          break;
        default:
          connected = false;
          break;
      }
      dirty = true;
    }
    if (!connected || stopping) break;
    const auto before = privacy.Reply();
    privacy.Poll();
    if (privacy.Reply().active &&
        Clock::now() - heartbeat > std::chrono::seconds(3))
      privacy.Fail(
          "Privacy controller stopped responding; local desktop and input "
          "restored");
    const auto& after = privacy.Reply();
    dirty |= before.active != after.active ||
             before.recovered != after.recovered ||
             before.emergency != after.emergency ||
             std::strcmp(before.failure, after.failure) != 0;
    if (dirty) {
      const auto& reply = privacy.Reply();
      if (send(socket, &reply, sizeof(reply), MSG_NOSIGNAL) == sizeof(reply)) {
        dirty = false;
        privacy.AcknowledgeEmergency();
      } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        break;
    }
  }
  // Keep trying if the X server temporarily rejected restoration, even after
  // the parent exited. Disconnecting releases XI grabs and cursor hiding too.
  do {
    privacy.Recover();
    if (!privacy.Reply().recovered) poll(nullptr, 0, 250);
  } while (!privacy.Reply().recovered);
  const auto& reply = privacy.Reply();
  const auto ignored = send(socket, &reply, sizeof(reply), MSG_NOSIGNAL);
  (void)ignored;
  close(socket);
  return 0;
}
}  // namespace crossdesk
