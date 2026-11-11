#include "display_resolution_linux.h"

#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <utility>

#include "platform.h"
#include "x11_error_trap.h"

namespace crossdesk {
namespace {

template <typename T, auto Free>
using XOwner = std::unique_ptr<T, decltype(Free)>;
using Resources = XOwner<XRRScreenResources, XRRFreeScreenResources>;
using Output = XOwner<XRROutputInfo, XRRFreeOutputInfo>;
using Crtc = XOwner<XRRCrtcInfo, XRRFreeCrtcInfo>;

struct ModeState {
  RRCrtc crtc = None;
  RRMode mode = None;
  int x = 0, y = 0;
  Rotation rotation = RR_Rotate_0;
  std::vector<RROutput> outputs;
};

// Each transaction owns a connection and a short server grab. This keeps a
// hotplug or another settings client from invalidating a checked output before
// the mode switch, and also lets restoration outlive the capture connection.
class RandrSession {
 public:
  explicit RandrSession(const char* address = nullptr)
      : display(IsWaylandSession() ? nullptr : XOpenDisplay(address)) {
    if (!display) return;
    trap = std::make_unique<ScopedX11ErrorTrap>(display);
    int opcode = 0, event = 0, error = 0;
    int major = 1, minor = 3;
    if (XQueryExtension(display, "XWAYLAND", &opcode, &event, &error) ||
        !XRRQueryVersion(display, &major, &minor) ||
        major < 1 || (major == 1 && minor < 3)) return;
    root = DefaultRootWindow(display);
    XGrabServer(display);
    grabbed = true;
    XRRSelectInput(display, root, RRScreenChangeNotifyMask);
    resources.reset(XRRGetScreenResources(display, root));
  }
  ~RandrSession() {
    resources.reset();
    if (grabbed) XUngrabServer(display);
    trap.reset();
    if (display) XCloseDisplay(display);
  }
  RandrSession(const RandrSession&) = delete;
  RandrSession& operator=(const RandrSession&) = delete;

  Output GetOutput(RROutput id) const {
    return Output(XRRGetOutputInfo(display, resources.get(), id),
                  XRRFreeOutputInfo);
  }
  Crtc GetCrtc(RRCrtc id) const {
    return Crtc(XRRGetCrtcInfo(display, resources.get(), id), XRRFreeCrtcInfo);
  }
  const XRRModeInfo* FindMode(RRMode id) const {
    for (int i = 0; i < resources->nmode; ++i)
      if (resources->modes[i].id == id) return &resources->modes[i];
    return nullptr;
  }
  std::string OutputKey(RROutput id, const XRROutputInfo& output) const {
    // EDID distinguishes a replacement monitor on the same connector. Virtual
    // outputs without EDID still have a server-local output ID and name.
    uint64_t hash = 14695981039346656037ull;
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char* data = nullptr;
    const Atom edid = XInternAtom(display, "EDID", True);
    if (edid != None &&
        XRRGetOutputProperty(display, id, edid, 0, 1024, False, False,
                             AnyPropertyType, &type, &format, &count,
                             &remaining, &data) == Success &&
        type == XA_INTEGER && format == 8 && data) {
      for (unsigned long i = 0; i < count; ++i)
        hash = (hash ^ data[i]) * 1099511628211ull;
    }
    if (data) XFree(data);
    return std::to_string(id) + ":" +
           std::string(output.name, output.nameLen) + ":" + std::to_string(hash);
  }

  bool SimpleGeometry(RRCrtc id) const {
    // Changing a scaled/panning desktop also requires compositor-specific
    // policy. Do not overwrite those settings with an unscaled mode.
    XRRCrtcTransformAttributes* transform = nullptr;
    if (!XRRGetCrtcTransform(display, id, &transform) || !transform) return false;
    bool identity = true;
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        const XFixed expected = row == col ? 65536 : 0;
        identity &= transform->currentTransform.matrix[row][col] == expected &&
                    transform->pendingTransform.matrix[row][col] == expected;
      }
    }
    XFree(transform);
    XOwner<XRRPanning, XRRFreePanning> panning(
        XRRGetPanning(display, resources.get(), id), XRRFreePanning);
    return identity && (!panning || (!panning->width && !panning->height));
  }

  bool Resolve(RROutput id, const std::string& name, ModeState& state,
               std::string& key) const {
    if (!resources ||
        std::find(resources->outputs, resources->outputs + resources->noutput,
                  id) == resources->outputs + resources->noutput) return false;
    auto output = GetOutput(id);
    if (!output || output->connection != RR_Connected || !output->crtc ||
        name != std::string(output->name, output->nameLen)) return false;
    auto crtc = GetCrtc(output->crtc);
    if (!crtc || !crtc->mode || crtc->noutput <= 0 ||
        !SimpleGeometry(output->crtc)) return false;
    state = {output->crtc, crtc->mode, crtc->x, crtc->y, crtc->rotation,
             {crtc->outputs, crtc->outputs + crtc->noutput}};
    key = DisplayString(display);
    // A mirrored CRTC changes all its outputs together. Validate every member
    // on apply and restore, including replacements on a cloned connector. Use
    // the same key for every member so requests through another mirrored slot
    // share the original restoration baseline instead of saving an interim mode.
    auto sorted = state.outputs;
    std::sort(sorted.begin(), sorted.end());
    for (auto member : sorted) {
      auto clone = GetOutput(member);
      if (!clone || clone->connection != RR_Connected) return false;
      key += "|" + OutputKey(member, *clone);
    }
    return key.size() <= 1024;
  }

  Display* display = nullptr;
  Window root = None;
  Resources resources{nullptr, XRRFreeScreenResources};
  std::unique_ptr<ScopedX11ErrorTrap> trap;
  bool grabbed = false;
};

DisplayResolution ModeSize(const XRRModeInfo& mode, Rotation rotation) {
  if (rotation & (RR_Rotate_90 | RR_Rotate_270))
    return {int(mode.height), int(mode.width)};
  return {int(mode.width), int(mode.height)};
}

double RefreshRate(const XRRModeInfo& mode) {
  if (!mode.hTotal || !mode.vTotal) return 0;
  return double(mode.dotClock) / mode.hTotal / mode.vTotal;
}

std::vector<RRMode> Choices(const RandrSession& session, const ModeState& state) {
  const auto* current = session.FindMode(state.mode);
  if (!current) return {};
  std::vector<Output> outputs;
  for (auto id : state.outputs) {
    auto output = session.GetOutput(id);
    if (!output) return {};
    outputs.push_back(std::move(output));
  }
  std::map<std::pair<int, int>, const XRRModeInfo*> unique;
  for (int i = 0; i < session.resources->nmode; ++i) {
    const auto& mode = session.resources->modes[i];
    const auto size = ModeSize(mode, state.rotation);
    if (!ValidDisplayResolution(size) ||
        (mode.modeFlags & (RR_Interlace | RR_DoubleScan))) continue;
    bool supported = true;
    for (const auto& output : outputs) {
      if (std::find(output->modes, output->modes + output->nmode,
                               mode.id) == output->modes + output->nmode) {
        supported = false;
        break;
      }
    }
    if (!supported) continue;
    const auto key = std::make_pair(size.width, size.height);
    const auto rank = [&](const XRRModeInfo& candidate) {
      return std::make_pair(std::abs(RefreshRate(candidate) - RefreshRate(*current)),
                            candidate.id != current->id);
    };
    auto found = unique.find(key);
    if (found == unique.end() || rank(mode) < rank(*found->second))
      unique[key] = &mode;
  }
  std::vector<RRMode> result;
  for (auto it = unique.rbegin(); it != unique.rend(); ++it) {
    result.push_back(it->second->id);
    if (result.size() == kMaxDisplayResolutionModes) break;
  }
  return result;
}

struct ScreenSize {
  int width, height, mm_width, mm_height;
};

ScreenSize CurrentScreen(const RandrSession& session) {
  XWindowAttributes attr{};
  XGetWindowAttributes(session.display, session.root, &attr);
  // Refresh Xlib's cached physical dimensions after our own resize as well.
  // A stale DPI value can otherwise make rollback skip restoring millimeters.
  while (XPending(session.display)) {
    XEvent event{};
    XNextEvent(session.display, &event);
    XRRUpdateConfiguration(&event);
  }
  const int screen = DefaultScreen(session.display);
  return {attr.width, attr.height, DisplayWidthMM(session.display, screen),
          DisplayHeightMM(session.display, screen)};
}

bool SetScreen(RandrSession& session, ScreenSize size) {
  const auto current = CurrentScreen(session);
  if (current.width != size.width || current.height != size.height ||
      current.mm_width != size.mm_width || current.mm_height != size.mm_height) {
    XRRSetScreenSize(session.display, session.root, size.width, size.height,
                     std::max(1, size.mm_width), std::max(1, size.mm_height));
  }
  const auto applied = CurrentScreen(session);
  return session.trap->SyncAndGetError() == 0 &&
         applied.width == size.width && applied.height == size.height;
}

bool SetCrtc(RandrSession& session, const ModeState& state) {
  // SetScreenSize can advance the resource timestamp.
  Resources fresh(XRRGetScreenResourcesCurrent(session.display, session.root),
                  XRRFreeScreenResources);
  if (!fresh) return false;
  auto outputs = state.outputs;
  if (XRRSetCrtcConfig(session.display, fresh.get(), state.crtc, CurrentTime,
                       state.x, state.y, state.mode, state.rotation,
                       outputs.data(), int(outputs.size())) != RRSetConfigSuccess)
    return false;
  auto applied = session.GetCrtc(state.crtc);
  return applied && applied->mode == state.mode && applied->x == state.x &&
         applied->y == state.y && applied->rotation == state.rotation;
}

bool ChangeMode(RandrSession& session, const ModeState& before, RRMode mode,
                const ScreenSize* restore_screen = nullptr) {
  const auto* selected = session.FindMode(mode);
  if (!selected) return false;
  const auto size = ModeSize(*selected, before.rotation);
  int width = before.x + size.width, height = before.y + size.height;
  for (int i = 0; i < session.resources->ncrtc; ++i) {
    const auto id = session.resources->crtcs[i];
    auto other = session.GetCrtc(id);
    if (!other) return false;
    if (!other->mode) continue;
    if (!session.SimpleGeometry(id)) return false;
    if (id != before.crtc) {
      width = std::max(width, other->x + int(other->width));
      height = std::max(height, other->y + int(other->height));
    }
  }
  int min_width = 0, min_height = 0, max_width = 0, max_height = 0;
  if (!XRRGetScreenSizeRange(session.display, session.root, &min_width,
                            &min_height, &max_width, &max_height)) return false;
  width = std::max(width, min_width);
  height = std::max(height, min_height);
  if (restore_screen) {
    width = std::max(width, restore_screen->width);
    height = std::max(height, restore_screen->height);
  }
  if (before.x < 0 || before.y < 0 || width > max_width || height > max_height)
    return false;
  const auto original = CurrentScreen(session);
  if (original.width <= 0 || original.height <= 0) return false;
  const auto screen_size = [&](int w, int h) -> ScreenSize {
    return {w, h, std::max(1, int(std::lround(double(original.mm_width) * w /
                                            original.width))),
                  std::max(1, int(std::lround(double(original.mm_height) * h /
                                            original.height)))};
  };
  const auto expanded = screen_size(std::max(width, original.width),
                                     std::max(height, original.height));
  auto final = screen_size(width, height);
  if (restore_screen && width == restore_screen->width &&
      height == restore_screen->height) final = *restore_screen;
  auto target = before;
  target.mode = mode;
  // Grow first, switch the CRTC, then shrink. Other displays retain their
  // positions, rotations and modes, and are always inside the framebuffer.
  bool expanded_ok = SetScreen(session, expanded);
  // RandR 1.1 emulation (for example Xephyr) can expose modes through 1.2
  // while ignoring SetScreenSize. Its sole CRTC resizes the root itself. Only
  // allow this fallback for an origin-aligned, single-CRTC desktop with no X
  // error; a real allocation/configuration error must still fail and roll back.
  if (!expanded_ok && session.resources->ncrtc == 1 && !before.x && !before.y &&
      session.trap->SyncAndGetError() == 0)
    expanded_ok = true;
  if (expanded_ok && SetCrtc(session, target) &&
      SetScreen(session, final)) return true;
  // Keep the larger framebuffer until rollback has restored the old CRTC.
  SetScreen(session, expanded);
  if (SetCrtc(session, before)) SetScreen(session, original);
  return false;
}

bool ResolveChange(const RandrSession& session,
                   const std::vector<DisplayInfo>& displays,
                   const DisplayResolutionChange& change, ModeState& state) {
  if (change.display_id < 0 || change.display_id >= int(displays.size()) ||
      !ValidDisplayResolution(change.resolution)) return false;
  const auto& display = displays[change.display_id];
  std::string key;
  return session.Resolve(reinterpret_cast<uintptr_t>(display.handle),
                         display.name, state, key) && key == change.display_key &&
         session.trap->SyncAndGetError() == 0;
}

}  // namespace

std::vector<DisplayResolutionInfo> QueryLinuxDisplayResolutions(
    const std::vector<DisplayInfo>& displays) {
  if (displays.empty()) return {};
  RandrSession session;
  if (!session.resources) return {};
  std::vector<DisplayResolutionInfo> result;
  for (int i = 0; i < int(displays.size()) && i < kMaxResolutionDisplays; ++i) {
    DisplayResolutionInfo info;
    info.display_id = i;
    ModeState state;
    if (!session.Resolve(reinterpret_cast<uintptr_t>(displays[i].handle),
                         displays[i].name, state, info.display_key)) continue;
    const auto* current = session.FindMode(state.mode);
    if (!current) continue;
    info.current = ModeSize(*current, state.rotation);
    if (!ValidDisplayResolution(info.current)) continue;
    for (auto mode : Choices(session, state))
      info.modes.push_back(ModeSize(*session.FindMode(mode), state.rotation));
    result.push_back(std::move(info));
  }
  if (session.trap->SyncAndGetError()) return {};
  return result;
}

bool ApplyLinuxDisplayResolution(const std::vector<DisplayInfo>& displays,
                                 const DisplayResolutionChange& change) {
  RandrSession session;
  ModeState state;
  if (!ResolveChange(session, displays, change, state)) return false;
  const auto choices = Choices(session, state);
  if (session.trap->SyncAndGetError()) return false;
  for (auto mode : choices) {
    if (ModeSize(*session.FindMode(mode), state.rotation) == change.resolution)
      return mode == state.mode || ChangeMode(session, state, mode);
  }
  return false;
}

std::function<bool()> CaptureLinuxDisplayResolution(
    const std::vector<DisplayInfo>& displays,
    const DisplayResolutionChange& change) {
  RandrSession session;
  ModeState original;
  if (!ResolveChange(session, displays, change, original)) return {};
  const auto screen = CurrentScreen(session);
  const std::string address = DisplayString(session.display);
  const auto display = displays[change.display_id];
  const auto key = change.display_key;
  return [address, display, key, original, screen] {
    RandrSession session(address.c_str());
    ModeState current;
    std::string current_key;
    if (!session.Resolve(reinterpret_cast<uintptr_t>(display.handle),
                         display.name, current, current_key) || current_key != key ||
        session.trap->SyncAndGetError())
      return false;
    // Restore the exact refresh rate as well as dimensions, while retaining
    // any placement/rotation changes the local user made during the session.
    const auto actual_screen = CurrentScreen(session);
    if (current.mode == original.mode && actual_screen.width == screen.width &&
        actual_screen.height == screen.height &&
        actual_screen.mm_width == screen.mm_width &&
        actual_screen.mm_height == screen.mm_height) return true;
    return ChangeMode(session, current, original.mode, &screen);
  };
}

}  // namespace crossdesk
