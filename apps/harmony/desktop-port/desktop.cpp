#include "bridge.h"
#include "keyboard.h"
#include "keyboard_hook.h"
#include "host_access.h"
#include "pointer.h"
#include "window_mode.h"
#include "platform/harmony/gui/tray/harmony_tray.h"
#include "platform/harmony/gui/window_state.h"
#include "platform/harmony/harmony_autostart.h"
#include "application/gui_application.h"
#include "SDL3/SDL.h"
#include "rd_log.h"
#include "platform/path_backend.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <hilog/log.h>
#include <napi/native_api.h>
#include <native_window/external_window.h>
#include <slint-platform.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace crossdesk::harmony::desktop {
namespace {
std::mutex bridge_mutex;
std::condition_variable wake;
Json environment;
std::deque<Json> incoming, outgoing;
std::map<uint64_t, Json> replies;
std::deque<slint::platform::Platform::Task> tasks;
std::atomic<bool> stopping{false};
std::thread gui_thread;
uint64_t request_id = 0;
bool services_cancelled = false;
}
Json Environment() { std::lock_guard lock(bridge_mutex); return environment; }
void Emit(Json command) { std::lock_guard lock(bridge_mutex); outgoing.push_back(std::move(command)); }
Json Request(Json command) {
  std::unique_lock lock(bridge_mutex);
  if (stopping || services_cancelled) return Json::object();
  const auto id = ++request_id;
  command["request"] = id; outgoing.push_back(std::move(command));
  // Picker dismissal and shutdown both reply/cancel; never block ArkUI.
  wake.wait(lock, [id] { return stopping || services_cancelled || replies.count(id); });
  if (stopping || services_cancelled) return Json::object();
  auto result = std::move(replies.at(id)); replies.erase(id); return result;
}
namespace {
struct Surface;
class DesktopWindow final : public slint::platform::WindowAdapter {
 public:
  int id;
  slint::platform::SoftwareRenderer software{slint::platform::SoftwareRenderer::RepaintBufferType::ReusedBuffer};
  slint::PhysicalSize extent;
  slint::PhysicalPosition origin{{0, 0}};
  bool dirty = true, visible = false, active = false, explicitly_sized = false;
  // The stage can report its initial focus before it acknowledges tray startup.
  bool can_restore_on_activation = false;
  bool positioned = false;
  float density = 1;
  int decor = 0;
  bool no_frame = false, topmost = false;
  KeyboardState keyboard;
  PointerState pointer;
  const void* ime_adapter = nullptr;
  uint64_t ime_session = 0;
  std::string surface_id;
  WindowMode window_mode;
  uint64_t state_revision = 0;
  bool scaled = false;
  std::unique_ptr<Surface> surface;
  explicit DesktopWindow(int value);
  ~DesktopWindow() override;
  slint::PhysicalSize size() override { return extent; }
  slint::platform::AbstractRenderer& renderer() override { return software; }
  void request_redraw() override { dirty = true; }
  void set_visible(bool value) override {
    if (!scaled) SetDensity(Environment().value("density", 1.f));
    if (!value) ReleaseInput();
    visible = value;
    if (value) can_restore_on_activation = true;
    Json command{{"op", "visible"}, {"id", id}, {"visible", value}, {"width", extent.width}, {"height", extent.height}};
    if (positioned) { command["x"] = origin.x; command["y"] = origin.y; }
    Emit(std::move(command));
  }
  void set_size(slint::PhysicalSize value) override {
    explicitly_sized = true; extent = value;
    Emit({{"op", "resize"}, {"id", id}, {"width", value.width}, {"height", value.height}});
  }
  std::optional<slint::PhysicalPosition> position() override { return origin; }
  void set_position(slint::PhysicalPosition value) override {
    positioned = true; origin = value;
    Emit({{"op", "move"}, {"id", id}, {"x", value.x}, {"y", value.y}});
  }
  void update_window_properties(const WindowProperties& properties) override {
    const auto constraints = properties.layout_constraints();
    if (!scaled) SetDensity(Environment().value("density", 1.f));
    if (!explicitly_sized && !visible)
      extent = slint::PhysicalSize({uint32_t(std::max(1.f, constraints.preferred.width * density)),
                                  uint32_t(std::max(1.f, constraints.preferred.height * density))});
    Json command{{"op", "properties"}, {"id", id}, {"title", std::string(properties.title())},
        {"stateRevision", state_revision}, {"noFrame", no_frame}, {"topmost", topmost}};
    const Json requested{{"fullscreen", properties.is_fullscreen()}, {"maximized", properties.is_maximized()},
                         {"minimized", properties.is_minimized()}};
    // Layout/title updates must not reapply an old window mode while the OS
    // is asynchronously reporting a titlebar maximize/restore transition.
    command.update(window_mode.Request(requested));
    if (constraints.min) { command["minWidth"] = constraints.min->width * density; command["minHeight"] = constraints.min->height * density; }
    if (constraints.max) { command["maxWidth"] = constraints.max->width * density; command["maxHeight"] = constraints.max->height * density; }
    Emit(std::move(command));
  }
  void SetDensity(float value);
  void ReleaseInput();
};
std::map<int, DesktopWindow*> windows;
#include "surface.inc"
DesktopWindow::DesktopWindow(int value) : id(value), extent{{1216, 903}}, surface(std::make_unique<Surface>(*this)) { windows[id] = this; }
DesktopWindow::~DesktopWindow() { windows.erase(id); Emit({{"op", "destroy"}, {"id", id}}); }
void DesktopWindow::SetDensity(float value) {
  if (!std::isfinite(value) || value <= 0 || (scaled && density == value)) return;
  density = value; scaled = true;
  surface->density = value;
  surface->rendered_width = surface->rendered_height = 0;
  window().dispatch_scale_factor_change_event(value);
  dirty = true;
}
slint::PointerEventButton PointerButton(int button) {
  return button == 1 ? slint::PointerEventButton::Right : button == 2 ? slint::PointerEventButton::Middle : slint::PointerEventButton::Left;
}
void DesktopWindow::ReleaseInput() {
  pointer.Cancel([this](int button, float x, float y) {
    window().dispatch_pointer_release_event(slint::LogicalPosition({x, y}), PointerButton(button));
  });
  window().dispatch_pointer_exit_event();
  for (const auto& [code, key] : keyboard.Pressed()) if (!key.empty()) window().dispatch_key_release_event(slint::SharedString(key));
  keyboard.Reset();
  std::lock_guard lock(bridge_mutex);
  environment["buttons"] = 0;
}

void OsRequest(void* data, const void* token, int kind, const uint8_t* text, size_t length,
    size_t cursor, size_t anchor, float x, float y, float width, float height, int type) {
  auto& adapter = *static_cast<DesktopWindow*>(data);
  if (kind == 4) { adapter.no_frame = type & 1; adapter.topmost = type & 2; return; }
  if (kind == 3) Emit({{"op", "cursor"}, {"id", adapter.id}, {"kind", type}});
  else {
    if (kind == 0 || kind == 1) ++adapter.ime_session;
    adapter.ime_adapter = kind == 0 ? nullptr : token;
    Emit({{"op", "ime"}, {"id", adapter.id}, {"imeSession", adapter.ime_session}, {"kind", kind}, {"text", std::string(reinterpret_cast<const char*>(text), length)},
      {"cursor", cursor}, {"anchor", anchor}, {"x", x}, {"y", y}, {"width", width}, {"height", height}, {"inputType", type}});
  }
}
void Key(DesktopWindow& adapter, const Json& command) {
  const bool down = command.value("down", false);
  const int code = command.value("code", 0);
  const bool repeat = down && adapter.keyboard.IsPressed(code);
  const auto lock = [&command](const char* name) -> std::optional<bool> {
    return command.contains(name) ? std::optional<bool>{command.at(name).get<bool>()} : std::nullopt;
  };
  const auto key = adapter.keyboard.Translate(code, down, command.value("unicode", 0u), lock("capsLock"), lock("numLock"));
  if (key.empty()) return;
  if (repeat) adapter.window().dispatch_key_press_repeat_event(slint::SharedString(key));
  else if (down) adapter.window().dispatch_key_press_event(slint::SharedString(key));
  else adapter.window().dispatch_key_release_event(slint::SharedString(key));
}
void Dispatch(const Json& command) {
  if (command.value("op", "") == "tray") { HarmonyTray::Dispatch(command.value("text", "")); return; }
  const auto found = windows.find(command.value("id", 0));
  if (found == windows.end()) return;
  auto& adapter = *found->second;
  const auto op = command.value("op", "");
  // ArkUI can finish a callback after Slint has focused another editor in the
  // same window. Window identity alone is not an input-session identity.
  if (command.contains("imeSession") && command.value("imeSession", uint64_t{0}) != adapter.ime_session) return;
  if (op != "attach" && command.contains("surface") && command.value("surface", "") != adapter.surface_id) return;
  if (op == "attach") {
    adapter.surface_id = command.at("surface").get<std::string>();
    if (!adapter.surface->AttachSurface(std::stoull(command.at("surface").get<std::string>()), command.at("density")))
      Emit({{"op", "error"}, {"message", "Slint surface initialization failed"}});
    adapter.dirty = true;
  } else if (op == "detach") { adapter.surface_id.clear(); adapter.ReleaseInput(); adapter.surface->DetachSurface(); }
  else if (op == "redraw") adapter.dirty = true;
  else if (op == "restore") { adapter.window().set_minimized(false); adapter.window().show(); adapter.dirty = true; }
  else if (op == "close") adapter.window().dispatch_close_requested_event();
  else if (op == "active") {
    adapter.active = command.value("active", false);
    if (!adapter.active) adapter.ReleaseInput();
    if (command.value("active", false) && adapter.can_restore_on_activation && !adapter.visible) {
      adapter.window().set_minimized(false);
      adapter.window().show();
    }
    adapter.window().dispatch_window_active_changed_event(command.value("active", false));
  } else if (op == "state") {
    if (command.value("minimized", false)) adapter.can_restore_on_activation = true;
    adapter.state_revision = command.value("stateRevision", uint64_t{0});
    auto actual = Json::object();
    for (const auto* name : {"fullscreen", "maximized", "minimized"})
      if (command.contains(name)) actual[name] = command.at(name);
    // Each setter synchronously calls update_window_properties. These are OS
    // acknowledgements, not three independent requests back to the compositor.
    adapter.window_mode.Acknowledge(actual, [&] {
      if (command.contains("fullscreen")) adapter.window().set_fullscreen(command.at("fullscreen").get<bool>());
      if (command.contains("maximized")) adapter.window().set_maximized(command.at("maximized").get<bool>());
      if (command.contains("minimized")) adapter.window().set_minimized(command.at("minimized").get<bool>());
    });
  } else if (op == "metrics") {
    adapter.positioned = true;
    adapter.origin = slint::PhysicalPosition({command.value("x", 0), command.value("y", 0)});
    adapter.decor = command.value("decor", 0);
    adapter.SetDensity(command.value("density", 1.f));
    adapter.dirty = true;
  } else if (op == "pointerExit") {
    adapter.window().dispatch_pointer_exit_event();
  } else if (op == "pointer") {
    const int kind = command.value("kind", 2), button = command.value("button", 0);
    const slint::LogicalPosition point{{command.value("x", 0.f), command.value("y", 0.f)}};
    {
      std::lock_guard lock(bridge_mutex);
      const auto density = adapter.density;
      environment["pointerX"] = adapter.origin.x + point.x * density;
      environment["pointerY"] = adapter.origin.y + adapter.decor + point.y * density;
      adapter.pointer.Update(kind, button, point.x, point.y);
      environment["buttons"] = adapter.pointer.Buttons();
    }
    const auto b = PointerButton(button);
    if (kind == 0) adapter.window().dispatch_pointer_press_event(point, b);
    else if (kind == 1) adapter.window().dispatch_pointer_release_event(point, b);
    else if (kind == 2) adapter.window().dispatch_pointer_move_event(point);
    else adapter.ReleaseInput();
  } else if (op == "scroll") adapter.window().dispatch_pointer_scroll_event(
      slint::LogicalPosition({command.value("x", 0.f), command.value("y", 0.f)}), command.value("dx", 0.f), command.value("dy", 0.f));
  else if (op == "key") Key(adapter, command);
  else if (op == "imeKey" && adapter.ime_adapter) {
    KeyboardState symbols;
    const auto text = symbols.Translate(command.value("code", 0), true);
    const int modifiers = (command.value("shift", false) ? 1 : 0) | (command.value("control", false) ? 2 : 0);
    slint::cbindgen_private::crossdesk_slint_edit(adapter.ime_adapter,
        reinterpret_cast<const uint8_t*>(text.data()), text.size(), modifiers);
  } else if (op == "imeSelection" && adapter.ime_adapter) {
    slint::cbindgen_private::crossdesk_slint_selection(adapter.ime_adapter,
        command.value("anchor", 0), command.value("cursor", 0));
  }
  else if (op == "composition" && adapter.ime_adapter) {
    const auto text = command.value("text", "");
    slint::cbindgen_private::crossdesk_slint_composition(adapter.ime_adapter, command.value("commit", true), reinterpret_cast<const uint8_t*>(text.data()), text.size());
  } else if (op == "text") {
    const slint::SharedString text(command.value("text", ""));
    adapter.window().dispatch_key_press_event(text); adapter.window().dispatch_key_release_event(text);
  }
}
class DesktopPlatform final : public slint::platform::Platform {
  int next_id = 0;
 public:
  std::unique_ptr<slint::platform::WindowAdapter> create_window_adapter() override { return std::make_unique<DesktopWindow>(++next_id); }
  void run_in_event_loop(Task task) override {
    std::lock_guard lock(bridge_mutex); tasks.push_back(std::move(task)); wake.notify_all();
  }
  void quit_event_loop() override { stopping = true; wake.notify_all(); }
  void set_clipboard_text(const slint::SharedString& text, Clipboard kind) override {
    if (kind == Clipboard::DefaultClipboard) SDL_SetClipboardText(text.data());
  }
  std::optional<slint::SharedString> clipboard_text(Clipboard kind) override {
    if (kind != Clipboard::DefaultClipboard) return {};
    const auto result = Request({{"op", "clipboardPaste"}});
    if (!result.value("ok", false)) return {};
    return slint::SharedString(result.value("text", ""));
  }
  void run_event_loop() override {
    while (!stopping) {
      std::deque<Json> commands;
      std::deque<Task> pending;
      {
        std::lock_guard lock(bridge_mutex); commands.swap(incoming); pending.swap(tasks);
      }
      for (auto& task : pending) std::move(task).run();
      for (auto& command : commands) {
        try { Dispatch(command); }
        catch (const std::exception& error) { Emit({{"op", "error"}, {"message", error.what()}}); }
      }
      slint::platform::update_timers_and_animations();
      for (const auto& [id, adapter] : windows) if (adapter->visible) adapter->surface->Render();
      std::unique_lock lock(bridge_mutex);
      wake.wait_for(lock, std::chrono::milliseconds(8), [] { return stopping || !incoming.empty() || !tasks.empty(); });
    }
  }
};
std::string String(napi_env env, napi_value value) {
  size_t length = 0;
  if (napi_get_value_string_utf8(env, value, nullptr, 0, &length) != napi_ok || length > 1024 * 1024) return {};
  std::string result(length + 1, '\0');
  napi_get_value_string_utf8(env, value, result.data(), result.size(), &length);
  result.resize(length); return result;
}
napi_value Undefined(napi_env env) { napi_value out; napi_get_undefined(env, &out); return out; }
napi_value Start(napi_env env, napi_callback_info info) {
  if (gui_thread.joinable()) return Undefined(env);
  napi_value arg; size_t count = 1; napi_get_cb_info(env, info, &count, &arg, nullptr, nullptr);
  try {
    environment = Json::parse(String(env, arg));
    if (environment.value("filesDir", "").empty()) throw std::runtime_error("Missing application storage directory");
  } catch (const std::exception& error) { napi_throw_error(env, nullptr, error.what()); return nullptr; }
  stopping = false;
  gui_thread = std::thread([] {
    try {
      const auto language = Environment().value("language", "en");
      setenv("LANGUAGE", language.c_str(), 1);
      setenv("SLINT_DEFAULT_FONT", "/system/fonts/HarmonyOS_Sans.ttf", 1);
      setenv("SLINT_FONT_PATH", "/system/fonts/HarmonyOS_Sans_SC.ttf", 1);
      const auto certificates = Environment().value("certificates", "");
      if (certificates.empty()) throw std::runtime_error("Missing system CA certificate directory");
      setenv("SSL_CERT_DIR", certificates.c_str(), 1);
      slint::cbindgen_private::crossdesk_slint_set_os_callback(OsRequest);
      slint::platform::set_platform(std::make_unique<DesktopPlatform>());
      // GuiRuntime constructs worker controllers before Run(). They may log
      // immediately, so select an app-writable directory before construction.
      crossdesk::InitLogger(crossdesk::platform::GetLogPath("CrossDesk").string());
      crossdesk::GuiApplication app;
      app.Run(Environment().value("loginLaunch", false));
      Emit({{"op", "exit"}});
    } catch (const std::exception& error) {
      OH_LOG_Print(LOG_APP, LOG_ERROR, 0, "SlintDesktop", "Application failed: %{public}s", error.what());
      Emit({{"op", "error"}, {"message", error.what()}});
    }
  });
  return Undefined(env);
}
napi_value Send(napi_env env, napi_callback_info info) {
  napi_value arg; size_t count = 1; napi_get_cb_info(env, info, &count, &arg, nullptr, nullptr);
  try {
    auto command = Json::parse(String(env, arg));
    if (command.value("op", "") == "autostartState") {
      SetHarmonyAutostartStatus(command.value("pending", false) ? HarmonyAutostartStatus::checking :
          !command.value("ok", false) ? HarmonyAutostartStatus::failed :
          !command.value("supported", false) ? HarmonyAutostartStatus::unsupported :
          command.value("active", false) ? HarmonyAutostartStatus::enabled : HarmonyAutostartStatus::disabled);
      return Undefined(env);
    }
    if (command.value("op", "") == "active")
      SetKeyboardFocus(command.value("id", 0), command.value("active", false));
    else if (command.value("op", "") == "keyboardPermission")
      SetKeyboardHookPermission(command.value("ok", false));
    else if (command.value("op", "") == "hostPermissions") {
      SetHostPermissions(command.value("controlDevice", false), command.value("screenRecording", false));
      return Undefined(env);
    }
    else if (command.value("op", "") == "clipboardChanged") {
      SDL_Event event{SDL_EVENT_CLIPBOARD_UPDATE}; SDL_PushEvent(&event);
      return Undefined(env);
    }
    std::lock_guard lock(bridge_mutex);
    // A synchronous native picker can be pending on the GUI thread when the
    // system tray requests exit. Unblock it so the shared exit callback runs.
    if (command.value("op", "") == "tray" && command.value("text", "") == "exit")
      services_cancelled = true;
    if (command.value("op", "") == "displays") environment["displays"] = command.at("displays");
    else if (command.value("op", "") == "reply") replies[command.at("request").get<uint64_t>()] = command;
    else incoming.push_back(std::move(command));
    wake.notify_all();
  } catch (...) { napi_throw_error(env, nullptr, "Invalid desktop event"); return nullptr; }
  return Undefined(env);
}
napi_value Poll(napi_env env, napi_callback_info) {
  Json result = Json::array();
  { std::lock_guard lock(bridge_mutex); for (auto& command : outgoing) result.push_back(std::move(command)); outgoing.clear(); }
  const auto text = result.dump(); napi_value out; napi_create_string_utf8(env, text.c_str(), text.size(), &out); return out;
}
napi_value Init(napi_env env, napi_value exports) {
  napi_property_descriptor properties[] = {
    {"start", nullptr, Start, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"send", nullptr, Send, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"poll", nullptr, Poll, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 3, properties);
  napi_add_env_cleanup_hook(env, [](void*) { stopping = true; wake.notify_all(); if (gui_thread.joinable()) gui_thread.join(); }, nullptr);
  return exports;
}
napi_module module = {1, 0, nullptr, Init, "slint_desktop", nullptr, {0}};
}
bool WindowActive(const slint::Window& window) {
  const auto& handle = window.window_handle().handle();
  for (const auto& [id, adapter] : windows) {
    const auto& candidate = adapter->window().window_handle().handle();
    if (handle._0 == candidate._0 && handle._1 == candidate._1)
      return adapter->active && adapter->visible && !window.is_minimized();
  }
  return false;
}
}
namespace crossdesk {
bool IsHarmonyWindowActive(const slint::Window& window) {
  return harmony::desktop::WindowActive(window);
}
}
extern "C" __attribute__((constructor)) void RegisterSlintDesktop() { napi_module_register(&crossdesk::harmony::desktop::module); }
