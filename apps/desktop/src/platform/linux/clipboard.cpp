#include "clipboard.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <SDL3/SDL_clipboard.h>

#include <chrono>
#include <climits>
#include <memory>
#include <string>
#include <thread>

#include "platform/clipboard_backend.h"
#include "rd_log.h"
#include "clipboard_file_uri.h"

namespace crossdesk {

std::string Clipboard::GetText() {
  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    LOG_ERROR("Clipboard::GetText: failed to open X display");
    return {};
  }

  Window owner = XGetSelectionOwner(display, XA_PRIMARY);
  if (owner == None) {
    owner =
        XGetSelectionOwner(display, XInternAtom(display, "CLIPBOARD", False));
    if (owner == None) {
      XCloseDisplay(display);
      return {};
    }
  }

  Atom selection = XA_PRIMARY;
  Atom target = XInternAtom(display, "UTF8_STRING", False);
  if (target == None) target = XA_STRING;
  const Window window = XCreateSimpleWindow(
      display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
  XSelectInput(display, window, PropertyChangeMask);
  XConvertSelection(display, selection, target, XA_PRIMARY, window,
                    CurrentTime);

  std::string result;
  bool done = false;
  while (!done) {
    XEvent event = {};
    XNextEvent(display, &event);
    if (event.type != SelectionNotify) continue;
    if (event.xselection.property == None) {
      if (selection == XA_PRIMARY) {
        selection = XInternAtom(display, "CLIPBOARD", False);
        XConvertSelection(display, selection, target, XA_PRIMARY, window,
                          CurrentTime);
        continue;
      }
      break;
    }

    Atom actual_type = None;
    int actual_format = 0;
    unsigned long item_count = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(display, window, XA_PRIMARY, 0, LONG_MAX / 4,
                           False, AnyPropertyType, &actual_type,
                           &actual_format, &item_count, &bytes_after,
                           &data) == Success) {
      if (data) {
        result.assign(reinterpret_cast<char*>(data), item_count);
        XFree(data);
      }
      done = true;
    }
  }

  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return result;
}

bool Clipboard::SetText(const std::string& text) {
  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    LOG_ERROR("Clipboard::SetText: failed to open X display");
    return false;
  }

  const Window window = XCreateSimpleWindow(
      display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
  const Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
  const Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
  const Atom targets = XInternAtom(display, "TARGETS", False);
  XSetSelectionOwner(display, clipboard, window, CurrentTime);
  if (XGetSelectionOwner(display, clipboard) != window) {
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return false;
  }

  XChangeProperty(display, window, XA_PRIMARY, utf8, 8, PropModeReplace,
                  reinterpret_cast<const unsigned char*>(text.data()),
                  static_cast<int>(text.size()));
  while (true) {
    XEvent event = {};
    XNextEvent(display, &event);
    if (event.type == SelectionClear) break;
    if (event.type != SelectionRequest) continue;

    const XSelectionRequestEvent& request = event.xselectionrequest;
    XSelectionEvent response = {};
    response.type = SelectionNotify;
    response.display = request.display;
    response.requestor = request.requestor;
    response.selection = request.selection;
    response.time = request.time;
    response.target = request.target;
    response.property = request.property;
    if (request.target == targets) {
      Atom supported[] = {utf8, XA_STRING, targets};
      XChangeProperty(display, request.requestor, request.property, XA_ATOM,
                      32, PropModeReplace,
                      reinterpret_cast<unsigned char*>(supported), 3);
    } else if (request.target == utf8 || request.target == XA_STRING) {
      XChangeProperty(display, request.requestor, request.property,
                      request.target, 8, PropModeReplace,
                      reinterpret_cast<const unsigned char*>(text.data()),
                      static_cast<int>(text.size()));
    } else {
      response.property = None;
    }
    XSendEvent(display, request.requestor, False, 0,
               reinterpret_cast<XEvent*>(&response));
    XSync(display, False);
  }

  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return true;
}

bool Clipboard::HasText() {
  Display* display = XOpenDisplay(nullptr);
  if (!display) return false;
  const Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
  Window owner = XGetSelectionOwner(display, clipboard);
  if (owner == None) owner = XGetSelectionOwner(display, XA_PRIMARY);
  XCloseDisplay(display);
  return owner != None;
}

std::optional<std::vector<std::string>> Clipboard::GetFiles() {
  for (const char* type : {"x-special/gnome-copied-files", "text/uri-list"}) {
    if (!SDL_HasClipboardData(type)) continue;
    size_t size = 0;
    void* data = SDL_GetClipboardData(type, &size);
    std::vector<std::string> paths;
    if (data && size <= 1024 * 1024)
      paths = clipboard_uri::Decode(std::string_view(static_cast<const char*>(data), size));
    SDL_free(data);
    if (paths.empty()) return std::nullopt;
    return paths;
  }
  return std::vector<std::string>{};
}

uint64_t Clipboard::ChangeCount() { return 0; }

bool Clipboard::SetFiles(const std::vector<std::string>& paths) {
  if (paths.empty()) return false;
  struct Payload { std::string uri, gnome; };
  // SDL can retain userdata even when its backend reports a failed set. Keep
  // ownership here and clear SDL's reference before destroying failed offers.
  static std::unique_ptr<Payload> offered;
  auto payload = std::make_unique<Payload>();
  for (const auto& path : paths) {
    if (path.empty() || path.front() != '/') return false;
    payload->uri += clipboard_uri::Encode(path) + "\r\n";
    payload->gnome += "\n" + clipboard_uri::Encode(path);
  }
  payload->gnome = "copy" + payload->gnome;
  const char* types[] = {"text/uri-list", "x-special/gnome-copied-files"};
  const bool ok = SDL_SetClipboardData(
      [](void* user, const char* type, size_t* size) -> const void* {
        if (!type) return nullptr;
        auto* payload = static_cast<Payload*>(user);
        const auto& value = std::string_view(type) == "text/uri-list" ? payload->uri : payload->gnome;
        *size = value.size(); return value.data();
      }, nullptr, payload.get(), types, 2);
  if (ok) offered = std::move(payload);
  else SDL_ClearClipboardData();
  return ok;
}

namespace platform {

void RunClipboardMonitor(int check_interval_ms) {
  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    LOG_ERROR("Failed to open X display for clipboard monitoring");
    return;
  }

  int event_base = 0;
  int error_base = 0;
  if (!XFixesQueryExtension(display, &event_base, &error_base)) {
    LOG_WARN("XFixes extension not available, falling back to polling");
    XCloseDisplay(display);
    while (ClipboardMonitoring()) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(check_interval_ms > 0 ? check_interval_ms
                                                           : 100));
      if (ClipboardMonitoring()) HandleClipboardChange();
    }
    return;
  }

  const Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
  const Window event_window = XCreateSimpleWindow(
      display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
  XFixesSelectSelectionInput(display, event_window, clipboard,
                             XFixesSetSelectionOwnerNotifyMask |
                                 XFixesSelectionWindowDestroyNotifyMask |
                                 XFixesSelectionClientCloseNotifyMask);
  LOG_INFO("Clipboard event monitoring started (Linux XFixes)");

  while (ClipboardMonitoring()) {
    while (ClipboardMonitoring() && XPending(display) > 0) {
      XEvent event = {};
      XNextEvent(display, &event);
      if (event.type == event_base + XFixesSelectionNotify) {
        HandleClipboardChange();
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  XFixesSelectSelectionInput(display, event_window, clipboard, 0);
  XDestroyWindow(display, event_window);
  XCloseDisplay(display);
}

void WakeClipboardMonitor() {}

}  // namespace platform
}  // namespace crossdesk
