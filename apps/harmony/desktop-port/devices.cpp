#include "bridge.h"
#include "displays.h"
#include "capture.h"
#include "input.h"
#include "keyboard_hook.h"
#include "host_access.h"
#include "screen_capturer_factory.h"
#include "speaker_capturer_factory.h"
#include "device_controller_factory.h"
#include "platform/video_renderer.h"
#include "runtime/cursor_state_provider.h"
#include "runtime/cursor_position.h"
#include "cursor_shape.h"
#include "platform/harmony/input/system_shortcuts.h"
#include <display_stream_id.h>
#include <multimodalinput/oh_input_manager.h>
#include <hilog/log.h>
#include <atomic>
#include <cmath>
#include <mutex>

namespace crossdesk {
namespace {
std::mutex input_mutex;
harmony::Input input;
std::atomic<bool> capture_active{false};
std::atomic<harmony::CaptureCursorMode> cursor_mode{harmony::CaptureCursorMode::unknown};
std::atomic<bool> control_device_granted{false};
std::atomic<bool> screen_recording_granted{false};
std::mutex cursor_monitor_lifecycle, cursor_monitor_mutex;
bool cursor_monitor_requested = false, cursor_monitor_installed = false;
bool cursor_monitor_accepting = false;
struct MonitoredCursorPosition {
  bool valid = false;
  int32_t display = -1;
  double x = 0, y = 0;
};
MonitoredCursorPosition monitored_cursor;

void ReceiveCursorPosition(const Input_MouseEvent* event) {
  if (!event) return;
  std::lock_guard lock(cursor_monitor_mutex);
  if (!cursor_monitor_accepting || !capture_active) return;
  if (OH_Input_GetMouseEventAction(event) == MOUSE_ACTION_CANCEL) {
    monitored_cursor = {};
    return;
  }
  const auto display = OH_Input_GetMouseEventDisplayId(event);
  const auto x = OH_Input_GetMouseEventDisplayX(event);
  const auto y = OH_Input_GetMouseEventDisplayY(event);
  monitored_cursor = {display >= 0 && x >= 0 && y >= 0, display, double(x), double(y)};
}

void SetCursorMonitoring(bool active) {
  std::lock_guard operation(cursor_monitor_lifecycle);
  if (cursor_monitor_requested == active) return;
  cursor_monitor_requested = active;
  {
    std::lock_guard lock(cursor_monitor_mutex);
    cursor_monitor_accepting = false;
    monitored_cursor = {};
  }
  // The callback only uses process-owned state. A failed removal remains
  // inert, and must be removed before the next session can register again.
  if (cursor_monitor_installed) {
    const auto result = OH_Input_RemoveMouseEventMonitor(ReceiveCursorPosition);
    // With this non-null callback, PARAMETER_ERROR means no listener remains
    // (for example, the recording service already removed it on shutdown).
    if (result != INPUT_SUCCESS && result != INPUT_PARAMETER_ERROR) {
      OH_LOG_Print(LOG_APP, LOG_WARN, 0, "SlintDesktop", "Cursor monitor removal failed: %{public}d", int(result));
      return;
    }
    cursor_monitor_installed = false;
  }
  if (!active) return;
  // Input Kit allows this listener only during recording. INPUT_MONITORING
  // provides background positions without INPUT_DEVICE_CONFIGURATOR.
  {
    std::lock_guard lock(cursor_monitor_mutex);
    cursor_monitor_accepting = true;
  }
  const auto result = OH_Input_AddMouseEventMonitor(ReceiveCursorPosition);
  if (result == INPUT_SUCCESS) {
    cursor_monitor_installed = true;
  } else {
    {
      std::lock_guard lock(cursor_monitor_mutex);
      cursor_monitor_accepting = false;
      monitored_cursor = {};
    }
    OH_LOG_Print(LOG_APP, LOG_WARN, 0, "SlintDesktop", "Cursor monitor unavailable: %{public}d", int(result));
  }
}

bool InjectionAuthorized() {
  if (!capture_active) return false;
  // API 26's dialog status does not reflect CONTROL_DEVICE authorization.
  if (control_device_granted) return true;
  Input_InjectionStatus status = UNAUTHORIZED;
  return OH_Input_QueryAuthorizedStatus(&status) == INPUT_SUCCESS && status == AUTHORIZED;
}
std::mutex speaker_mutex;
SpeakerCapturer::speaker_data_cb speaker_callback;
bool speaker_running = false;
std::vector<DisplayInfo> Displays() {
  std::vector<DisplayInfo> result;
  for (const auto& d : harmony::desktop::ReadDisplays(harmony::desktop::Environment()))
    result.emplace_back(reinterpret_cast<void*>(uintptr_t(d.id)), d.name, d.primary,
                        d.x, d.y, d.x + d.width, d.y + d.height);
  return result;
}
class HarmonyScreen final : public ScreenCapturer {
  cb_desktop_data callback_;
  harmony::Capture capture_;
  mutable std::mutex mutex_;
  std::vector<DisplayInfo> displays_;
  DisplayInfo current_{"", 0, 0, 0, 0};
  int selected_ = 0, fps_ = 60;
  bool running_ = false, requested_ = false;
  bool show_cursor_ = false;

  void StopCapture() {
    capture_active = false;
    cursor_mode = harmony::CaptureCursorMode::unknown;
    SetCursorMonitoring(false);
    capture_.Stop();
    { std::lock_guard lock(input_mutex); input.Release(); }
    OH_Input_CancelInjection();
  }
  void Refresh() {
    const auto latest = Displays();
    // MiniRTC negotiates DisplayN at initialization. Preserve its slot/physical
    // identity across hot-unplug; new displays become available on reinit.
    for (auto& known : displays_) {
      const auto found = std::find_if(latest.begin(), latest.end(), [&](const auto& d) { return d.handle == known.handle; });
      if (found != latest.end()) known = *found;
      else { known.width = known.height = 0; known.right = known.left; known.bottom = known.top; }
    }
    int target = selected_;
    if (target < 0 || target >= int(displays_.size()) || displays_[target].width <= 0 || displays_[target].height <= 0) {
      const auto first = std::find_if(displays_.begin(), displays_.end(), [](const auto& d) { return d.width > 0 && d.height > 0; });
      target = first == displays_.end() ? -1 : int(first - displays_.begin());
    }
    const bool changed = target != selected_ || (target >= 0 &&
        (displays_[target].width != current_.width || displays_[target].height != current_.height));
    selected_ = target;
    if (requested_ && changed) {
      StopCapture();
      running_ = StartCapture() == 0;
    }
  }
  int StartCapture() {
    if (selected_ < 0 || selected_ >= int(displays_.size()) ||
        displays_[selected_].width <= 0 || displays_[selected_].height <= 0) return -1;
    current_ = displays_[selected_];
    const auto stream = MakeDisplayStreamId(selected_);
    const bool started = capture_.Start(current_.width, current_.height, uintptr_t(current_.handle),
        [this, stream](const MiniRtcVideoFrame& frame) {
          if (callback_) callback_(reinterpret_cast<unsigned char*>(const_cast<char*>(frame.data)),
              frame.size, frame.width, frame.height, stream.c_str(), nullptr);
        }, [this](int state) {
          const bool was_active = capture_active.exchange(harmony::CaptureActiveForState(state, capture_active));
          cursor_mode = capture_active ? capture_.CursorMode() : harmony::CaptureCursorMode::unknown;
          SetCursorMonitoring(capture_active && cursor_mode == harmony::CaptureCursorMode::separate);
          if (capture_active && !was_active && !InjectionAuthorized()) {
            OH_Input_RequestInjection([](Input_InjectionStatus status) {
              OH_LOG_Print(LOG_APP, LOG_INFO, 0, "SlintDesktop", "Host input authorization: %{public}d", int(status));
              // Retry releases retained while authorization was unavailable
              // before accepting a newly authorized controller's presses.
              { std::lock_guard lock(input_mutex); input.Release(); }
            });
          } else if (!capture_active) { std::lock_guard lock(input_mutex); input.Release(); }
          OH_LOG_Print(LOG_APP, LOG_INFO, 0, "SlintDesktop", "Host capture state: %{public}d", state);
        },
        [](const char* data, size_t size) {
          std::lock_guard lock(speaker_mutex);
          if (speaker_running && speaker_callback)
            speaker_callback(reinterpret_cast<unsigned char*>(const_cast<char*>(data)), size, "audio");
        }, fps_, screen_recording_granted, show_cursor_);
    cursor_mode = started ? capture_.CursorMode() : harmony::CaptureCursorMode::unknown;
    return started ? 0 : -1;
  }
  int Select(int index) {
    if (index < 0 || index >= int(displays_.size()) || displays_[index].width <= 0 || displays_[index].height <= 0) return -1;
    if (index == selected_) return 0;
    const int previous = selected_;
    if (running_) StopCapture();
    selected_ = index;
    if (!requested_ || StartCapture() == 0) { running_ = requested_; return 0; }
    // Do not acknowledge a failed switch or send input to the failed target.
    selected_ = previous;
    running_ = StartCapture() == 0;
    return -1;
  }
 public:
  ~HarmonyScreen() override { Stop(); }
  int Init(int fps, cb_desktop_data cb) override {
    std::lock_guard lock(mutex_);
    fps_ = std::clamp(fps, 1, 60); callback_ = std::move(cb); displays_ = Displays(); return 0;
  }
  int Destroy() override { return Stop(); }
  int Start(bool show_cursor) override {
    std::lock_guard lock(mutex_);
    if (running_ && show_cursor_ == show_cursor) return 0;
    if (running_) StopCapture();
    requested_ = running_ = false;
    show_cursor_ = show_cursor;
    Refresh();
    running_ = StartCapture() == 0;
    requested_ = running_;
    return running_ ? 0 : -1;
  }
  int Stop() override {
    std::lock_guard lock(mutex_);
    requested_ = running_ = false; StopCapture(); return 0;
  }
  int Pause(int) override { return Stop(); }
  int Resume(int index) override {
    std::lock_guard lock(mutex_);
    Refresh();
    if (Select(index) != 0) return -1;
    if (!running_) running_ = StartCapture() == 0;
    requested_ = running_;
    return running_ ? 0 : -1;
  }
  std::vector<DisplayInfo> GetDisplayInfoList() override {
    std::lock_guard lock(mutex_);
    Refresh();
    return displays_;
  }
  int SwitchTo(int index) override { std::lock_guard lock(mutex_); return Select(index); }
  int ResetToInitialMonitor() override { return SwitchTo(0); }
  int GetCurrentMonitorIndex() const override { std::lock_guard lock(mutex_); return selected_; }
};
class HarmonyMouse final : public MouseController {
  std::vector<DisplayInfo> displays_;
  void* display_ = nullptr;
  int width_ = 0, height_ = 0;
  bool Configure(int index) {
    const auto& d = displays_[index];
    if (d.handle == display_ && d.width == width_ && d.height == height_) return true;
    if (!input.Configure(d.width, d.height, uintptr_t(d.handle))) return false;
    display_ = d.handle; width_ = d.width; height_ = d.height;
    return true;
  }
 public:
  unsigned ReleasePressedButtons(unsigned mask = 7) override {
    std::lock_guard lock(input_mutex);
    return input.ReleaseMouseButtons(mask);
  }
  int Init(std::vector<DisplayInfo> displays) override {
    if (displays.empty()) return -1;
    std::lock_guard lock(input_mutex);
    displays_ = std::move(displays); return Configure(0) ? 0 : -1;
  }
  void UpdateDisplayInfoList(const std::vector<DisplayInfo>& displays) override {
    std::lock_guard lock(input_mutex);
    if (!std::any_of(displays.begin(), displays.end(), [this](const auto& d) {
          return d.handle == display_ && d.width > 0 && d.height > 0;
        })) input.Release();
    displays_ = displays;
  }
  int Destroy() override {
    std::lock_guard lock(input_mutex); input.Release(); displays_.clear(); width_ = height_ = 0; return 0;
  }
  int SendMouseCommand(RemoteAction action, int index) override {
    std::lock_guard lock(input_mutex);
    if (action.m.relative) return 0;
    if (index < 0 || index >= int(displays_.size()) || displays_[index].width <= 0 || displays_[index].height <= 0 || !InjectionAuthorized()) return -1;
    if (!Configure(index)) return -1;
    return input.Apply(action) ? 0 : -1;
  }
};
class HarmonyKeyboard final : public KeyboardCapturer {
  harmony::desktop::KeyboardHook hook_;
  int last_hook_result_ = -1;
 public:
  int Hook(OnKeyAction callback, void* context) override {
    const int result = hook_.Start(callback, context);
    if (result != last_hook_result_) {
      last_hook_result_ = result;
      OH_LOG_Print(LOG_APP, result == INPUT_SUCCESS ? LOG_INFO : LOG_WARN, 0, "SlintDesktop",
          "Global keyboard hook result: %{public}d", result);
    }
    if (result == INPUT_PERMISSION_DENIED)
      harmony::desktop::Emit({{"op", "keyboardPermission"}});
    return result;
  }
  // Local keyboard capture and remote input injection are independent. A
  // controller-window focus change must not release another peer's host keys.
  int Unhook() override { return hook_.Stop(); }
  bool IsHookActive() const override { return hook_.Active(); }
  bool ShouldRetryHook() const override {
    return last_hook_result_ != INPUT_DEVICE_NOT_SUPPORTED &&
        (last_hook_result_ != INPUT_PERMISSION_DENIED || harmony::desktop::KeyboardHookPermissionGranted());
  }
  int SendKeyboardCommand(int key, bool down, uint32_t scan, bool extended) override {
    RemoteAction action{}; action.type = keyboard;
    action.k.key_value = key; action.k.flag = down ? key_down : key_up;
    action.k.scan_code = scan; action.k.extended = extended;
    std::lock_guard lock(input_mutex); return InjectionAuthorized() && input.Apply(action) ? 0 : -1;
  }
};
// The common presenter owns its CPU image fallback. A GPU renderer can replace
// this without introducing a separate session, input, or window implementation.
class SoftwareVideo final : public VideoRenderer {
  std::string selected_;
 public:
  bool IsReady() const override { return false; }
  bool IsActive() const override { return false; }
  bool SetSelectedStream(std::string id) override { bool changed = id != selected_; selected_ = std::move(id); return changed; }
  void DiscardStream(std::string_view id) override { if (id == selected_) selected_.clear(); }
  SubmitResult SubmitNv12(std::string_view, const uint8_t*, size_t, int, int, VideoLatencyFrame) override { return SubmitResult::failed; }
  SubmitResult SubmitCachedNv12(std::string_view, const uint8_t*, size_t, int, int, VideoLatencyFrame) override { return SubmitResult::failed; }
  bool CopyLatestNv12(std::string_view, std::vector<unsigned char>*, int*, int*) const override { return false; }
};
class HarmonySpeaker final : public SpeakerCapturer {
 public:
  int Init(speaker_data_cb callback) override { std::lock_guard lock(speaker_mutex); speaker_callback = std::move(callback); return 0; }
  int Destroy() override { std::lock_guard lock(speaker_mutex); speaker_running = false; speaker_callback = {}; return 0; }
  int Start() override { std::lock_guard lock(speaker_mutex); speaker_running = true; return 0; }
  int Stop() override { std::lock_guard lock(speaker_mutex); speaker_running = false; return 0; }
  bool IsRunning() const override { std::lock_guard lock(speaker_mutex); return speaker_running; }
};
}
void harmony::desktop::SetHostPermissions(bool control_device, bool screen_recording) {
  std::lock_guard lock(input_mutex);
  if (control_device_granted.exchange(control_device) != control_device) input.Release();
  screen_recording_granted = screen_recording;
}
ScreenCapturer* ScreenCapturerFactory::Create() { return new HarmonyScreen(); }
bool SendHarmonySystemShortcut(ServiceCommandFlag command) {
  // Windows secure attention has no public Harmony equivalent. Do not claim
  // it succeeded, or turn a SAS request into an unrelated physical chord.
  if (command != ServiceCommandFlag::lock_workstation) return false;
  std::lock_guard lock(input_mutex);
  return InjectionAuthorized() && input.LockWorkstation();
}
SpeakerCapturer* SpeakerCapturerFactory::Create() { return new HarmonySpeaker(); }
DeviceController* DeviceControllerFactory::Create(Device device) {
  return device == Mouse ? static_cast<DeviceController*>(new HarmonyMouse()) : new HarmonyKeyboard();
}
std::unique_ptr<VideoRenderer> CreateVideoRenderer() { return std::make_unique<SoftwareVideo>(); }
struct CursorStateProvider::Impl {
  Input_CursorInfo* cursor = OH_Input_CursorInfo_Create();
  ~Impl() { if (cursor) OH_Input_CursorInfo_Destroy(&cursor); }
};
CursorStateProvider::CursorStateProvider() : impl_(std::make_unique<Impl>()) {}
CursorStateProvider::~CursorStateProvider() = default;
bool CursorStateProvider::Sample(const std::vector<DisplayInfo>& displays, int, CursorState* state) {
  if (!state) return false;
  *state = {};
  ResetCursorPosition(state);
  state->shape = RemoteCursorShape::none;
  if (!capture_active) return false;
  const auto mode = cursor_mode.load();
  if (mode == harmony::CaptureCursorMode::embedded) {
    state->render_mode = CursorRenderMode::embedded;
    return true;
  }
  if (mode != harmony::CaptureCursorMode::separate || !impl_->cursor ||
      OH_Input_GetCursorInfo(impl_->cursor, nullptr) != INPUT_SUCCESS ||
      OH_Input_CursorInfo_IsVisible(impl_->cursor, &state->visible) != INPUT_SUCCESS) return false;

  if (state->visible) {
    Input_PointerStyle style = DEFAULT;
    if (OH_Input_CursorInfo_GetStyle(impl_->cursor, &style) != INPUT_SUCCESS) return false;
    state->shape = harmony::desktop::ShapeFromPointerStyle(style);
  }
  state->render_mode = state->visible ? CursorRenderMode::separate : CursorRenderMode::hidden;
  state->hidden_reason = state->visible ? CursorHiddenReason::unspecified : CursorHiddenReason::system_hidden;

  int32_t display = -1;
  double x = 0, y = 0;
  auto location = OH_Input_GetPointerLocation(&display, &x, &y);
  if (location != INPUT_SUCCESS) {
    std::lock_guard lock(cursor_monitor_mutex);
    if (location == INPUT_DEVICE_NO_POINTER) {
      monitored_cursor = {};
    } else if (cursor_monitor_accepting && monitored_cursor.valid) {
      display = monitored_cursor.display;
      x = monitored_cursor.x;
      y = monitored_cursor.y;
      location = INPUT_SUCCESS;
    }
  }
  if (location == INPUT_SUCCESS && display >= 0 && std::isfinite(x) && std::isfinite(y)) {
    // SDK coordinates are relative to a physical display; the protocol uses
    // negotiated display slots. Do not confuse a physical ID with a slot or
    // select a different monitor when desktop rectangles overlap.
    for (size_t index = 0; index < displays.size(); ++index) {
      const auto& d = displays[index];
      if (uintptr_t(d.handle) != static_cast<uintptr_t>(display) || d.width <= 0 || d.height <= 0 ||
          x < 0 || y < 0 || x >= d.width || y >= d.height) continue;
      state->display_id = static_cast<int>(index);
      state->x = static_cast<float>(x / d.width);
      state->y = static_cast<float>(y / d.height);
      state->position_valid = true;
      break;
    }
  } else if (location == INPUT_DEVICE_NO_POINTER && !state->visible) {
    state->hidden_reason = CursorHiddenReason::no_pointing_device;
  }
  // Without a successful query or a monitored event, report no position.
  // This must not discard a successfully sampled shape/visibility.
  return true;
}
}
