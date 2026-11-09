#include "desktop_automation.h"

#include <xcb/xcb.h>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <cerrno>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#ifdef CROSSDESK_HAS_ATSPI
#include <atspi/atspi.h>
#include <dlfcn.h>
#endif
extern char** environ;

namespace crossdesk {
namespace {
using nlohmann::json;
class LaunchedProcesses {
 public:
  LaunchedProcesses() {
    children_.reserve(128);
    worker_ = std::thread([this] {
      std::unique_lock lock(mutex_);
      while (!stopping_) {
        wake_.wait_for(lock, std::chrono::milliseconds(500));
        children_.erase(std::remove_if(children_.begin(), children_.end(), [](pid_t child) {
          const auto result = waitpid(child, nullptr, WNOHANG);
          return result > 0 || (result < 0 && errno == ECHILD);
        }), children_.end());
      }
    });
  }
  ~LaunchedProcesses() {
    { std::lock_guard lock(mutex_); stopping_ = true; }
    wake_.notify_all();
    worker_.join();
  }
  pid_t Spawn(const std::string& executable, const std::string& argument) {
    std::lock_guard lock(mutex_);
    if (children_.size() >= 128) throw std::runtime_error("BUSY");
    char* args[] = {const_cast<char*>(executable.c_str()), argument.empty() ? nullptr : const_cast<char*>(argument.c_str()), nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, executable.c_str(), nullptr, nullptr, args, environ) != 0)
      throw std::runtime_error("APPLICATION_UNAVAILABLE");
    children_.push_back(pid);
    return pid;
  }
 private:
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_ = false;
  std::vector<pid_t> children_;
  std::thread worker_;
};
json Spawn(const std::string& executable, const std::string& argument = {}) {
  static LaunchedProcesses processes;
  return {{"accepted", true}, {"pid", processes.Spawn(executable, argument)}};
}
struct XConnection {
  std::shared_ptr<xcb_connection_t> connection;
  xcb_window_t root = 0;
  XConnection() {
    int screen_index = 0;
    connection = {xcb_connect(nullptr, &screen_index), xcb_disconnect};
    if (!connection || xcb_connection_has_error(connection.get())) throw std::runtime_error("X11_UNAVAILABLE");
    auto screen = xcb_setup_roots_iterator(xcb_get_setup(connection.get()));
    for (int index = 0; index < screen_index && screen.rem; ++index) xcb_screen_next(&screen);
    if (!screen.rem) throw std::runtime_error("X11_UNAVAILABLE");
    root = screen.data->root;
  }
  xcb_atom_t Atom(const char* name) const {
    auto reply = xcb_intern_atom_reply(connection.get(), xcb_intern_atom(connection.get(), 0, uint16_t(std::strlen(name)), name), nullptr);
    const auto atom = reply ? reply->atom : XCB_ATOM_NONE;
    std::free(reply);
    return atom;
  }
  std::vector<uint32_t> Property(xcb_window_t window, const char* name, xcb_atom_t type) const {
    auto reply = xcb_get_property_reply(connection.get(),
        xcb_get_property(connection.get(), 0, window, Atom(name), type, 0, 256), nullptr);
    std::vector<uint32_t> result;
    if (reply && reply->type == type && reply->format == 32) {
      const auto data = static_cast<const uint32_t*>(xcb_get_property_value(reply));
      result.assign(data, data + xcb_get_property_value_length(reply) / 4);
    }
    std::free(reply);
    return result;
  }
  std::string Title(xcb_window_t window) const {
    auto reply = xcb_get_property_reply(connection.get(),
        xcb_get_property(connection.get(), 0, window, Atom("_NET_WM_NAME"), Atom("UTF8_STRING"), 0, 256), nullptr);
    std::string text;
    if (reply && reply->format == 8) text.assign(static_cast<const char*>(xcb_get_property_value(reply)), xcb_get_property_value_length(reply));
    std::free(reply);
    return DesktopText(std::move(text));
  }
};
#ifdef CROSSDESK_HAS_ATSPI
// AT-SPI objects and its event loop belong to the system GLib, not MiniRTC's
// private static copy. Keep the handle for the lifetime of these objects.
struct AtspiGlib {
  decltype(&g_object_unref) object_unref;
  decltype(&g_free) free;
  decltype(&g_main_context_pending) context_pending;
  decltype(&g_main_context_iteration) context_iteration;

  AtspiGlib() {
    void* library = dlopen("libatspi.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!library) throw std::runtime_error("ACCESSIBILITY_UNAVAILABLE");
    object_unref = reinterpret_cast<decltype(object_unref)>(dlsym(library, "g_object_unref"));
    free = reinterpret_cast<decltype(free)>(dlsym(library, "g_free"));
    context_pending = reinterpret_cast<decltype(context_pending)>(dlsym(library, "g_main_context_pending"));
    context_iteration = reinterpret_cast<decltype(context_iteration)>(dlsym(library, "g_main_context_iteration"));
    if (!object_unref || !free || !context_pending || !context_iteration) {
      dlclose(library);
      throw std::runtime_error("ACCESSIBILITY_UNAVAILABLE");
    }
  }
};
AtspiGlib& SystemGlib() {
  static AtspiGlib api;
  return api;
}
template <class T> std::shared_ptr<T> Own(T* object) { return {object, [](T* value) { if (value) SystemGlib().object_unref(value); }}; }
using Accessible = std::shared_ptr<AtspiAccessible>;
std::string Text(char* value) {
  auto text = DesktopText(value ? value : "");
  SystemGlib().free(value);
  return text;
}
Accessible Desktop() {
  auto& glib = SystemGlib();
  static const int initialized = atspi_init();
  if (initialized > 1) throw std::runtime_error("ACCESSIBILITY_UNAVAILABLE");
  atspi_set_timeout(120, 120);
  for (int count = 0; count < 32 && glib.context_pending(nullptr); ++count) glib.context_iteration(nullptr, false);
  auto desktop = Own(atspi_get_desktop(0));
  if (!desktop) throw std::runtime_error("ACCESSIBILITY_UNAVAILABLE");
  return desktop;
}
json Perform(Accessible element, const std::string& operation, const std::string& value) {
  atspi_accessible_clear_cache(element.get());
  auto state = Own(atspi_accessible_get_state_set(element.get()));
  if (!state || atspi_state_set_contains(state.get(), ATSPI_STATE_DEFUNCT)) throw std::runtime_error("STALE_ELEMENT");
  const auto role = atspi_accessible_get_role(element.get(), nullptr);
  if (role == ATSPI_ROLE_INVALID) throw std::runtime_error("STALE_ELEMENT");
  if (role == ATSPI_ROLE_PASSWORD_TEXT) throw std::runtime_error("SECURE_ELEMENT");
  bool accepted = false;
  if (operation == "focus" || operation == "activate") {
    auto component = Own(atspi_accessible_get_component_iface(element.get()));
    if (component) accepted = atspi_component_grab_focus(component.get(), nullptr);
  } else if (operation == "set_value") {
    auto editable = Own(atspi_accessible_get_editable_text_iface(element.get()));
    if (editable) accepted = atspi_editable_text_set_text_contents(editable.get(), value.c_str(), nullptr);
  } else if (operation == "press") {
    auto action = Own(atspi_accessible_get_action_iface(element.get()));
    if (action) {
      const int count = std::min(16, atspi_action_get_n_actions(action.get(), nullptr));
      for (int index = 0; index < count; ++index) {
        const auto name = Text(atspi_action_get_action_name(action.get(), index, nullptr));
        if (name == "click" || name == "press" || name == "activate" || name == "toggle") {
          accepted = atspi_action_do_action(action.get(), index, nullptr);
          break;
        }
      }
    }
  }
  if (!accepted) throw std::runtime_error("UI_ACTION_FAILED");
  return {{"accepted", true}};
}
DesktopSnapshot Tree(Accessible root, int limit, int depth) {
  struct Entry { Accessible element; int depth, parent; };
  std::deque<Entry> queue{{root, 0, -1}};
  DesktopSnapshot result;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
  while (!queue.empty() && int(result.items.size()) < limit && std::chrono::steady_clock::now() < deadline) {
    auto entry = std::move(queue.front()); queue.pop_front();
    auto element = entry.element;
    atspi_accessible_clear_cache(element.get());
    const auto role = atspi_accessible_get_role(element.get(), nullptr);
    if (role == ATSPI_ROLE_INVALID) continue;
    const bool secure = role == ATSPI_ROLE_PASSWORD_TEXT;
    json data{{"role", Text(atspi_accessible_get_role_name(element.get(), nullptr))},
      {"name", secure ? std::string{} : Text(atspi_accessible_get_name(element.get(), nullptr))},
      {"pid", atspi_accessible_get_process_id(element.get(), nullptr)}, {"parent", entry.parent}, {"secure", secure}};
    auto state = Own(atspi_accessible_get_state_set(element.get()));
    if (state) {
      data["foreground"] = bool(atspi_state_set_contains(state.get(), ATSPI_STATE_ACTIVE));
      data["focused"] = bool(atspi_state_set_contains(state.get(), ATSPI_STATE_FOCUSED));
    }
    auto component = Own(atspi_accessible_get_component_iface(element.get()));
    if (component && state && atspi_state_set_contains(state.get(), ATSPI_STATE_SHOWING)) {
      const auto rect = atspi_component_get_extents(component.get(), ATSPI_COORD_TYPE_SCREEN, nullptr);
      if (rect) {
        data["bounds"] = {{"x", rect->x}, {"y", rect->y}, {"width", rect->width}, {"height", rect->height}};
        SystemGlib().free(rect);
      }
    }
    if (!secure) {
      auto text = Own(atspi_accessible_get_text_iface(element.get()));
      if (text) {
        const int count = std::clamp(atspi_text_get_character_count(text.get(), nullptr), 0, 256);
        if (count) data["value"] = Text(atspi_text_get_text(text.get(), 0, count, nullptr));
      }
    }
    const int parent = int(result.items.size());
    result.items.push_back({data,
      [element](const auto& operation, const auto& value) { return Perform(element, operation, value); },
      [element](int max, int levels) { return Tree(element, max, levels); }});
    const int count = std::max(0, atspi_accessible_get_child_count(element.get(), nullptr));
    if (entry.depth >= depth) { result.truncated |= count > 0; continue; }
    const int available = std::max(0, limit - int(result.items.size() + queue.size()));
    result.truncated |= count > available;
    for (int index = 0; index < std::min(count, available) && std::chrono::steady_clock::now() < deadline; ++index) {
      auto child = Own(atspi_accessible_get_child_at_index(element.get(), index, nullptr));
      if (child) queue.push_back({child, entry.depth + 1, parent});
    }
  }
  result.truncated |= !queue.empty();
  return result;
}
#endif
} // namespace
nlohmann::json DesktopCapabilities() {
  bool accessibility = false;
#ifdef CROSSDESK_HAS_ATSPI
  try { accessibility = bool(Desktop()); } catch (...) {}
#endif
  return {{"available", true}, {"backend", accessibility ? "linux-atspi" : "linux-x11"},
    {"tree", accessibility}, {"windows", accessibility || std::getenv("DISPLAY") != nullptr},
    {"launch", true}, {"open_url", access("/usr/bin/xdg-open", X_OK) == 0}};
}
DesktopSnapshot DesktopWindows() {
#ifdef CROSSDESK_HAS_ATSPI
  try { return Tree(Desktop(), 128, 2); }
  catch (const std::runtime_error&) { /* X11 remains usable when the accessibility bus is unavailable. */ }
#endif
  XConnection display;
  const auto windows = display.Property(display.root, "_NET_CLIENT_LIST_STACKING", XCB_ATOM_WINDOW);
  const auto active = display.Property(display.root, "_NET_ACTIVE_WINDOW", XCB_ATOM_WINDOW);
  DesktopSnapshot result;
  for (const auto window : windows) {
    const auto pid = display.Property(window, "_NET_WM_PID", XCB_ATOM_CARDINAL);
    json data{{"role", "window"}, {"name", display.Title(window)},
      {"pid", pid.empty() ? 0 : pid.front()}, {"foreground", !active.empty() && active.front() == window}};
    result.items.push_back({data, [window, pid](const std::string& operation, const std::string&) -> json {
      if (operation != "activate" && operation != "focus") throw std::runtime_error("UNSUPPORTED_OPERATION");
      XConnection display;
      const auto windows = display.Property(display.root, "_NET_CLIENT_LIST_STACKING", XCB_ATOM_WINDOW);
      if (std::find(windows.begin(), windows.end(), window) == windows.end() ||
          display.Property(window, "_NET_WM_PID", XCB_ATOM_CARDINAL) != pid) throw std::runtime_error("STALE_ELEMENT");
      xcb_client_message_event_t event{};
      event.response_type = XCB_CLIENT_MESSAGE;
      event.window = window;
      event.type = display.Atom("_NET_ACTIVE_WINDOW");
      event.format = 32;
      event.data.data32[0] = 2;
      event.data.data32[1] = XCB_CURRENT_TIME;
      const auto cookie = xcb_send_event_checked(display.connection.get(), 0, display.root,
          XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY, reinterpret_cast<const char*>(&event));
      const auto error = xcb_request_check(display.connection.get(), cookie);
      const bool accepted = !error;
      std::free(error);
      return {{"accepted", accepted}, {"state", "requested"}};
    }, {}});
    if (result.items.size() >= 128) { result.truncated = true; break; }
  }
  return result;
}
DesktopSnapshot DesktopTree(int pid, int limit, int depth) {
#ifdef CROSSDESK_HAS_ATSPI
  auto desktop = Desktop();
  if (!pid) return Tree(desktop, limit, depth);
  const int count = std::min(256, atspi_accessible_get_child_count(desktop.get(), nullptr));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
  for (int index = 0; index < count; ++index) {
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("UI_QUERY_TIMEOUT");
    auto app = Own(atspi_accessible_get_child_at_index(desktop.get(), index, nullptr));
    if (app && atspi_accessible_get_process_id(app.get(), nullptr) == unsigned(pid)) return Tree(app, limit, depth);
  }
  throw std::runtime_error("APPLICATION_UNAVAILABLE");
#else
  (void)pid; (void)limit; (void)depth;
  throw std::runtime_error("ATSPI_NOT_BUILT");
#endif
}
nlohmann::json DesktopLaunch(const std::string& application) {
  if (application.empty() || application.front() != '/' || access(application.c_str(), X_OK) != 0)
    throw std::invalid_argument("APPLICATION_UNAVAILABLE");
  return Spawn(application);
}
nlohmann::json DesktopOpenUrl(const std::string& url) { return Spawn("/usr/bin/xdg-open", url); }
} // namespace crossdesk
