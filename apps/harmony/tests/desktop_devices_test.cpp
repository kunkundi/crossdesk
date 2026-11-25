#include "bridge.h"
#include "capture.h"
#include "input.h"
#include "keyboard_hook.h"
#include "host_access.h"
#include "keyboard_hook_mock.h"
#include "platform/harmony/input/system_shortcuts.h"
#include "screen_capturer_factory.h"
#include "device_controller_factory.h"
#include "runtime/cursor_state_provider.h"
#include "runtime/cursor_presentation.h"
#include <multimodalinput/oh_input_manager.h>
#include <hilog/log.h>
#include <cassert>
#include <memory>
#include <tuple>
#include <limits>

// Exercise the production desktop adapter and SDK declarations without
// recording the workstation or requesting real input-injection permissions.
using crossdesk::harmony::desktop::Json;
Json environment;
crossdesk::harmony::Capture::Frame frame_callback;
crossdesk::harmony::Capture::State state_callback;
std::tuple<int, int, int, int> capture_parameters;
int starts = 0, stops = 0, fail_display = -1, input_display = -1, releases = 0;
bool release_ok = true;
bool persistent_capture = false;
bool capture_show_cursor = true;
crossdesk::harmony::CaptureCursorMode captured_cursor_mode = crossdesk::harmony::CaptureCursorMode::separate;
struct Input_CursorInfo {};
bool pointer_visible = true;
Input_PointerStyle pointer_style = DEFAULT;
Input_Result cursor_result = INPUT_SUCCESS, location_result = INPUT_SUCCESS, style_result = INPUT_SUCCESS;
int32_t pointer_display = 0;
double pointer_x = 960, pointer_y = 270;
struct Input_MouseEvent { int32_t action, display, x, y; };
Input_MouseEventCallback mouse_monitor = nullptr;
Input_Result monitor_add_result = INPUT_PERMISSION_DENIED, monitor_remove_result = INPUT_SUCCESS;
int monitor_adds = 0, monitor_removes = 0;
const Input_MouseEvent* initial_monitor_event = nullptr;
int injection_requests = 0;
Input_InjectionStatus authorization = UNAUTHORIZED;
namespace crossdesk::harmony::desktop {
Json Environment() { return environment; }
void Emit(Json) {}
}
namespace crossdesk::harmony {
void CaptureSurface::Stop() {}
bool Capture::Start(int width, int height, uint64_t display, Frame frame, State state, Audio, int fps, bool persistent_permission, bool show_cursor) {
  ++starts;
  persistent_capture = persistent_permission;
  capture_show_cursor = show_cursor;
  if (int(display) == fail_display) return false;
  capture_parameters = {width, height, int(display), fps};
  frame_callback = std::move(frame);
  state_callback = state;
  cursor_mode_ = show_cursor ? CaptureCursorMode::embedded : captured_cursor_mode;
  state(OH_SCREEN_CAPTURE_STATE_STARTED);
  return true;
}
void Capture::Stop() { ++stops; frame_callback = {}; }
bool Input::Configure(int, int, int display) { if (!Release()) return false; input_display = display; return true; }
bool Input::Release() { ++releases; return release_ok; }
unsigned Input::ReleaseMouseButtons(unsigned mask) { return release_ok ? mask & 7u : 0; }
bool Input::Apply(const RemoteAction&) { return true; }
bool Input::LockWorkstation() { return true; }
}
extern "C" {
int OH_LOG_Print(LogType, LogLevel, unsigned int, const char*, const char*, ...) { return 0; }
Input_Result OH_Input_QueryAuthorizedStatus(Input_InjectionStatus* result) { *result = authorization; return INPUT_SUCCESS; }
Input_Result OH_Input_RequestInjection(Input_InjectAuthorizeCallback callback) {
  ++injection_requests;
  authorization = AUTHORIZED; callback(authorization); return INPUT_SUCCESS;
}
void OH_Input_CancelInjection() { authorization = UNAUTHORIZED; }
Input_CursorInfo* OH_Input_CursorInfo_Create() { return new Input_CursorInfo; }
void OH_Input_CursorInfo_Destroy(Input_CursorInfo** info) { delete *info; *info = nullptr; }
Input_Result OH_Input_GetCursorInfo(Input_CursorInfo*, OH_PixelmapNative** pixels) {
  assert(pixels == nullptr); return cursor_result;
}
Input_Result OH_Input_CursorInfo_IsVisible(Input_CursorInfo*, bool* visible) {
  *visible = pointer_visible; return INPUT_SUCCESS;
}
Input_Result OH_Input_CursorInfo_GetStyle(Input_CursorInfo*, Input_PointerStyle* style) {
  assert(pointer_visible); *style = pointer_style; return style_result;
}
Input_Result OH_Input_GetPointerLocation(int32_t* display, double* x, double* y) {
  *display = pointer_display; *x = pointer_x; *y = pointer_y; return location_result;
}
Input_Result OH_Input_AddMouseEventMonitor(Input_MouseEventCallback callback) {
  ++monitor_adds;
  assert(!mouse_monitor);
  if (monitor_add_result == INPUT_SUCCESS) {
    mouse_monitor = callback;
    if (initial_monitor_event) callback(initial_monitor_event);
  }
  return monitor_add_result;
}
Input_Result OH_Input_RemoveMouseEventMonitor(Input_MouseEventCallback callback) {
  ++monitor_removes;
  assert(mouse_monitor == callback || (!mouse_monitor && monitor_remove_result == INPUT_PARAMETER_ERROR));
  if (monitor_remove_result == INPUT_SUCCESS || monitor_remove_result == INPUT_PARAMETER_ERROR) mouse_monitor = nullptr;
  return monitor_remove_result;
}
int32_t OH_Input_GetMouseEventAction(const Input_MouseEvent* event) { return event->action; }
int32_t OH_Input_GetMouseEventDisplayId(const Input_MouseEvent* event) { return event->display; }
int32_t OH_Input_GetMouseEventDisplayX(const Input_MouseEvent* event) { return event->x; }
int32_t OH_Input_GetMouseEventDisplayY(const Input_MouseEvent* event) { return event->y; }
}

int main() {
  using namespace crossdesk;
  const auto primary = Json::parse(R"({"id":0,"primary":true,"width":1920,"height":1080})");
  const auto secondary = Json::parse(R"({"id":42,"x":-2560,"width":2560,"height":1440})");
  environment["displays"] = Json::array({secondary, primary});
  ScreenCapturerFactory factory;
  std::unique_ptr<ScreenCapturer> screen(factory.Create());
  std::string stream;
  screen->Init(30, [&](unsigned char*, int, int, int, const char* id, const MiniRtcNativeVideoFrame*) { stream = id; });
  assert(screen->GetDisplayInfoList().size() == 2);
  assert(screen->Start(false) == 0);
  assert((capture_parameters == std::tuple{1920, 1080, 0, 30}));
  MiniRtcVideoFrame frame{}; frame_callback(frame); assert(stream == "Display1");
  CursorStateProvider cursor;
  CursorState cursor_state{};
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  assert(!capture_show_cursor && cursor_state.render_mode == CursorRenderMode::separate);
  assert(cursor_state.visible && cursor_state.shape == RemoteCursorShape::default_cursor);
  assert(cursor_state.position_update && cursor_state.position_valid && cursor_state.display_id == 0);
  assert(cursor_state.x == 0.5f && cursor_state.y == 0.25f);
  const std::pair<Input_PointerStyle, RemoteCursorShape> shapes[] = {
    {TEXT_CURSOR, RemoteCursorShape::text}, {HORIZONTAL_TEXT_CURSOR, RemoteCursorShape::text},
    {HAND_POINTING, RemoteCursorShape::pointer}, {HAND_OPEN, RemoteCursorShape::grab},
    {HAND_GRABBING, RemoteCursorShape::grabbing}, {CURSOR_FORBID, RemoteCursorShape::not_allowed},
    {NORTH_WEST_SOUTH_EAST, RemoteCursorShape::nwse_resize}, {NORTH_EAST_SOUTH_WEST, RemoteCursorShape::nesw_resize},
    {WEST_EAST, RemoteCursorShape::ew_resize}, {NORTH_SOUTH, RemoteCursorShape::ns_resize},
    {RESIZE_LEFT_RIGHT, RemoteCursorShape::col_resize}, {RESIZE_UP_DOWN, RemoteCursorShape::row_resize},
    {NORTH, RemoteCursorShape::n_resize}, {SOUTH, RemoteCursorShape::s_resize},
    {EAST, RemoteCursorShape::e_resize}, {WEST, RemoteCursorShape::w_resize},
    {NORTH_EAST, RemoteCursorShape::ne_resize}, {NORTH_WEST, RemoteCursorShape::nw_resize},
    {SOUTH_EAST, RemoteCursorShape::se_resize}, {SOUTH_WEST, RemoteCursorShape::sw_resize},
    {LOADING, RemoteCursorShape::wait}, {RUNNING, RemoteCursorShape::progress},
    {HELP, RemoteCursorShape::help}, {MOVE, RemoteCursorShape::move},
    {CROSS, RemoteCursorShape::crosshair}, {CURSOR_COPY, RemoteCursorShape::copy},
    {DEVELOPER_DEFINED_ICON, RemoteCursorShape::default_cursor},
    {static_cast<Input_PointerStyle>(999), RemoteCursorShape::default_cursor}
  };
  for (const auto& [native, remote] : shapes) {
    pointer_style = native;
    assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
    assert(cursor_state.shape == remote);
    RemoteAction outgoing{}, incoming{};
    outgoing.type = ControlType::cursor_state; outgoing.cs = cursor_state;
    assert(incoming.from_json(outgoing.to_json()));
    assert(incoming.type == ControlType::cursor_state && incoming.cs.position_valid);
    assert(incoming.cs.display_id == 0 && incoming.cs.x == 0.5f && incoming.cs.y == 0.25f);
    // Mac and Windows both use this presentation policy for the Harmony host.
    CursorPresentation presentation;
    presentation.Observe(incoming.cs, 100);
    assert(presentation.Resolve(101, HostPlatform::HarmonyOS) == remote);
  }
  pointer_visible = false;
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  assert(!cursor_state.visible && cursor_state.shape == RemoteCursorShape::none);
  assert(cursor_state.render_mode == CursorRenderMode::hidden && cursor_state.hidden_reason == CursorHiddenReason::system_hidden);
  location_result = INPUT_DEVICE_NO_POINTER;
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  assert(!cursor_state.position_valid && cursor_state.hidden_reason == CursorHiddenReason::no_pointing_device);
  pointer_visible = true; pointer_style = TEXT_CURSOR;
  location_result = INPUT_APP_NOT_FOCUSED;
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  assert(cursor_state.visible && cursor_state.shape == RemoteCursorShape::text && !cursor_state.position_valid);
  cursor_result = INPUT_SERVICE_EXCEPTION;
  assert(!cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  cursor_result = INPUT_SUCCESS; style_result = INPUT_SERVICE_EXCEPTION;
  assert(!cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  style_result = INPUT_SUCCESS; location_result = INPUT_SUCCESS;
  pointer_display = 42; pointer_x = 1280; pointer_y = 720;
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  assert(cursor_state.position_valid && cursor_state.display_id == 1 && cursor_state.x == 0.5f && cursor_state.y == 0.5f);
  auto overlapping = screen->GetDisplayInfoList();
  overlapping[1].left = overlapping[1].top = 0;
  overlapping[1].right = 2560; overlapping[1].bottom = 1440;
  assert(cursor.Sample(overlapping, 0, &cursor_state) && cursor_state.display_id == 1);
  for (double invalid : {-1., 2560., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    pointer_x = invalid;
    assert(cursor.Sample(overlapping, 0, &cursor_state) && !cursor_state.position_valid);
  }
  pointer_x = 1280; pointer_display = 99;
  assert(cursor.Sample(overlapping, 0, &cursor_state) && !cursor_state.position_valid);
  pointer_display = 42;
  overlapping[1].width = 0;
  assert(cursor.Sample(overlapping, 0, &cursor_state) && !cursor_state.position_valid);
  assert(screen->SwitchTo(1) == 0 && screen->GetCurrentMonitorIndex() == 1);
  assert(!capture_show_cursor); // Separate cursor policy survives a physical display switch.
  assert((capture_parameters == std::tuple{2560, 1440, 42, 30}));
  frame_callback(frame); assert(stream == "Display2");

  DeviceControllerFactory input_factory;
  std::unique_ptr<DeviceController> device(input_factory.Create(DeviceControllerFactory::Mouse));
  auto* mouse = static_cast<MouseController*>(device.get());
  mouse->Init(screen->GetDisplayInfoList());
  std::unique_ptr<DeviceController> keyboard_device(input_factory.Create(DeviceControllerFactory::Keyboard));
  auto* keyboard = static_cast<KeyboardCapturer*>(keyboard_device.get());
  const int before_unhook = releases;
  assert(keyboard->Unhook() == 0 && releases == before_unhook);
  harmony::desktop::SetKeyboardFocus(2, true);
  harmony::desktop::SetKeyboardHookPermission(false);
  hook_add_result = INPUT_PERMISSION_DENIED;
  assert(keyboard->Hook([](int, bool, uint32_t, bool, void*) {}, nullptr) == INPUT_PERMISSION_DENIED);
  assert(!keyboard->ShouldRetryHook()); // Do not interrupt a held fallback chord every second.
  harmony::desktop::SetKeyboardHookPermission(true);
  assert(keyboard->ShouldRetryHook());
  hook_add_result = INPUT_SUCCESS;
  RemoteAction action{}; action.type = ControlType::mouse;
  release_ok = false;
  assert(mouse->SendMouseCommand(action, 1) != 0 && input_display == 0);
  release_ok = true;
  const int before_input_switch = releases;
  assert(mouse->SendMouseCommand(action, 1) == 0 && input_display == 42);
  assert(releases == before_input_switch + 1);
  assert(mouse->SendMouseCommand(action, 1) == 0 && releases == before_input_switch + 1);
  assert(mouse->SendMouseCommand(action, -1) != 0 && mouse->SendMouseCommand(action, 2) != 0);
  state_callback(OH_SCREEN_CAPTURE_STATE_MIC_MUTED_BY_USER);
  assert(mouse->SendMouseCommand(action, 1) == 0);
  state_callback(OH_SCREEN_CAPTURE_STATE_ENTER_PRIVATE_SCENE);
  assert(mouse->SendMouseCommand(action, 1) != 0);
  assert(!cursor.Sample(screen->GetDisplayInfoList(), 1, &cursor_state));
  state_callback(OH_SCREEN_CAPTURE_STATE_EXIT_PRIVATE_SCENE);
  assert(mouse->SendMouseCommand(action, 1) == 0);
  authorization = UNAUTHORIZED;
  assert(mouse->SendMouseCommand(action, 1) != 0);
  authorization = AUTHORIZED;
  assert(SendHarmonySystemShortcut(ServiceCommandFlag::lock_workstation));
  assert(!SendHarmonySystemShortcut(ServiceCommandFlag::send_sas));

  fail_display = 0;
  assert(screen->SwitchTo(0) != 0 && screen->GetCurrentMonitorIndex() == 1);
  frame_callback(frame); assert(stream == "Display2"); // Failed switch rolls back.
  fail_display = -1;
  environment["displays"] = Json::array({secondary});
  const int before_removal = starts;
  auto displays = screen->GetDisplayInfoList();
  assert(displays.size() == 2 && displays[0].width == 0 && displays[1].width == 2560);
  assert(screen->GetCurrentMonitorIndex() == 1 && starts == before_removal);
  mouse->UpdateDisplayInfoList(displays);
  assert(mouse->SendMouseCommand(action, 0) != 0);
  assert(screen->SwitchTo(0) != 0);

  environment["displays"] = Json::array({primary});
  screen->GetDisplayInfoList();
  assert(screen->GetCurrentMonitorIndex() == 0);
  frame_callback(frame); assert(stream == "Display1");
  environment["displays"].push_back(Json::parse(R"({"id":91,"width":1280,"height":720})"));
  assert(screen->GetDisplayInfoList().size() == 2); // Unnegotiated streams must not be advertised as usable.
  environment["displays"] = Json::array();
  screen->GetDisplayInfoList();
  assert(screen->GetCurrentMonitorIndex() == -1 && !frame_callback);
  const int before_repeat = stops;
  screen->GetDisplayInfoList(); assert(stops == before_repeat);
  environment["displays"] = Json::array({secondary});
  screen->GetDisplayInfoList();
  assert(screen->GetCurrentMonitorIndex() == 1 && frame_callback);
  frame_callback(frame); assert(stream == "Display2");
  assert(screen->Pause(1) == 0 && !frame_callback);
  assert(screen->Resume(1) == 0 && frame_callback);
  assert(screen->Stop() == 0);
  assert(mouse->SendMouseCommand(action, 1) != 0); // Authorization alone is insufficient after capture ends.
  assert(!SendHarmonySystemShortcut(ServiceCommandFlag::lock_workstation));

  // A persistent grant is independent of dialog authorization and survives
  // disconnect/reconnect. Capture must still be active before accepting input.
  harmony::desktop::SetHostPermissions(true, true);
  const int before_persistent = injection_requests;
  assert(authorization == UNAUTHORIZED);
  assert(screen->Resume(1) == 0 && persistent_capture);
  assert(injection_requests == before_persistent && authorization == UNAUTHORIZED);
  mouse->UpdateDisplayInfoList(screen->GetDisplayInfoList());
  assert(mouse->SendMouseCommand(action, 1) == 0);
  assert(keyboard->SendKeyboardCommand(65, true) == 0);
  assert(SendHarmonySystemShortcut(ServiceCommandFlag::lock_workstation));
  state_callback(OH_SCREEN_CAPTURE_STATE_ENTER_PRIVATE_SCENE);
  assert(mouse->SendMouseCommand(action, 1) != 0);
  assert(keyboard->SendKeyboardCommand(65, false) != 0);
  state_callback(OH_SCREEN_CAPTURE_STATE_EXIT_PRIVATE_SCENE);
  assert(injection_requests == before_persistent);
  assert(screen->Stop() == 0 && mouse->SendMouseCommand(action, 1) != 0);
  assert(screen->Resume(1) == 0 && persistent_capture && injection_requests == before_persistent);
  const int before_revoke = releases;
  harmony::desktop::SetHostPermissions(false, false);
  assert(releases == before_revoke + 1);
  assert(mouse->SendMouseCommand(action, 1) != 0);
  assert(keyboard->SendKeyboardCommand(65, true) != 0);
  assert(!SendHarmonySystemShortcut(ServiceCommandFlag::lock_workstation));
  assert(injection_requests == before_persistent); // Revocation never triggers a prompt loop.
  assert(screen->Stop() == 0 && screen->Resume(1) == 0);
  assert(!persistent_capture && injection_requests == before_persistent + 1);
  assert(mouse->SendMouseCommand(action, 1) == 0); // Ordinary consent remains usable.
  screen->Stop();
  assert(!cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  assert(screen->Start(true) == 0 && capture_show_cursor);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  assert(cursor_state.render_mode == CursorRenderMode::embedded && !cursor_state.visible);
  assert(cursor_state.shape == RemoteCursorShape::none && !cursor_state.position_valid);
  CursorPresentation embedded;
  embedded.Observe(cursor_state, 100);
  assert(embedded.Resolve(101, HostPlatform::HarmonyOS) == RemoteCursorShape::none);
  assert(screen->Start(false) == 0 && !capture_show_cursor);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  assert(cursor_state.render_mode == CursorRenderMode::separate);
  screen->Stop();
  captured_cursor_mode = harmony::CaptureCursorMode::embedded; // Recorder refused to remove its cursor.
  assert(screen->Start(false) == 0);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  assert(cursor_state.render_mode == CursorRenderMode::embedded);
  screen->Stop();
  captured_cursor_mode = harmony::CaptureCursorMode::unknown;
  assert(screen->Start(false) == 0);
  assert(!cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  screen->Stop();

  // INPUT_MONITORING supplies screen-relative positions while the direct
  // query is rejected in the background. No INPUT_DEVICE_CONFIGURATOR grant.
  environment["displays"] = Json::array({primary, secondary});
  captured_cursor_mode = harmony::CaptureCursorMode::separate;
  location_result = INPUT_APP_NOT_FOCUSED;
  monitor_add_result = INPUT_SUCCESS;
  assert(screen->Start(false) == 0 && mouse_monitor);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 1, &cursor_state) && !cursor_state.position_valid);
  Input_MouseEvent motion{MOUSE_ACTION_MOVE, 42, 640, 720};
  mouse_monitor(&motion);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  assert(cursor_state.position_valid && cursor_state.display_id == 1);
  assert(cursor_state.x == 0.25f && cursor_state.y == 0.5f && cursor_state.shape == RemoteCursorShape::text);
  RemoteAction monitored_out{}, monitored_in{};
  monitored_out.type = ControlType::cursor_state; monitored_out.cs = cursor_state;
  assert(monitored_in.from_json(monitored_out.to_json()));
  assert(monitored_in.cs.position_valid && monitored_in.cs.display_id == 1 && monitored_in.cs.x == 0.25f);
  const int before_notification = monitor_adds;
  state_callback(OH_SCREEN_CAPTURE_STATE_MIC_MUTED_BY_USER);
  assert(monitor_adds == before_notification);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state) && cursor_state.position_valid);

  const auto queued_monitor = mouse_monitor;
  state_callback(OH_SCREEN_CAPTURE_STATE_ENTER_PRIVATE_SCENE);
  assert(!mouse_monitor && !cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  queued_monitor(&motion); // Late events cannot repopulate a stopped session.
  state_callback(OH_SCREEN_CAPTURE_STATE_EXIT_PRIVATE_SCENE);
  assert(mouse_monitor && cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  assert(!cursor_state.position_valid);
  mouse_monitor(&motion);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state) && cursor_state.position_valid);
  motion.action = MOUSE_ACTION_CANCEL;
  mouse_monitor(&motion);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state) && !cursor_state.position_valid);
  motion.action = MOUSE_ACTION_MOVE; motion.x = -1;
  mouse_monitor(&motion);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state) && !cursor_state.position_valid);
  motion.x = 640;
  mouse_monitor(&motion);
  assert(screen->SwitchTo(0) == 0 && mouse_monitor);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state) && !cursor_state.position_valid);
  motion.display = 0; motion.x = 480; motion.y = 540;
  mouse_monitor(&motion);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 1, &cursor_state));
  assert(cursor_state.position_valid && cursor_state.display_id == 0 && cursor_state.x == 0.25f && cursor_state.y == 0.5f);

  location_result = INPUT_DEVICE_NO_POINTER; pointer_visible = false;
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  assert(!cursor_state.position_valid && cursor_state.hidden_reason == CursorHiddenReason::no_pointing_device);
  location_result = INPUT_APP_NOT_FOCUSED; pointer_visible = true;
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state) && !cursor_state.position_valid);

  monitor_remove_result = INPUT_SERVICE_EXCEPTION;
  screen->Stop();
  assert(mouse_monitor);
  mouse_monitor(&motion); // Failed removal stays inert.
  const int before_failed_restart = monitor_adds;
  assert(screen->Start(false) == 0);
  assert(monitor_adds == before_failed_restart); // Never install a duplicate callback.
  mouse_monitor(&motion);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state) && !cursor_state.position_valid);
  monitor_remove_result = INPUT_SUCCESS;
  screen->Stop();
  assert(!mouse_monitor);
  assert(screen->Start(false) == 0 && mouse_monitor);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state) && !cursor_state.position_valid);
  mouse_monitor(&motion);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state) && cursor_state.position_valid);
  // Capture can remove the monitor itself; "not registered" is clean removal.
  mouse_monitor = nullptr;
  monitor_remove_result = INPUT_PARAMETER_ERROR;
  state_callback(OH_SCREEN_CAPTURE_STATE_PAUSED_BY_USER);
  monitor_remove_result = INPUT_SUCCESS;
  initial_monitor_event = &motion;
  state_callback(OH_SCREEN_CAPTURE_STATE_RESUMED_BY_USER);
  initial_monitor_event = nullptr;
  assert(mouse_monitor);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state) && cursor_state.position_valid);
  screen->Stop();
  assert(!mouse_monitor);
  monitor_add_result = INPUT_PERMISSION_DENIED;
  assert(screen->Start(false) == 0 && !mouse_monitor);
  const int before_denied_notification = monitor_adds;
  state_callback(OH_SCREEN_CAPTURE_STATE_MIC_MUTED_BY_USER);
  assert(monitor_adds == before_denied_notification);
  assert(cursor.Sample(screen->GetDisplayInfoList(), 0, &cursor_state));
  assert(cursor_state.visible && cursor_state.shape == RemoteCursorShape::text && !cursor_state.position_valid);
  screen->Stop();
  const int before_embedded = monitor_adds;
  assert(screen->Start(true) == 0 && !mouse_monitor && monitor_adds == before_embedded);
  screen->Stop();
}
