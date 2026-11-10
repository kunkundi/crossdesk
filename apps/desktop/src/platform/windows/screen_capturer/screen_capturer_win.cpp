#include "screen_capturer_win.h"

#include <Windows.h>
#include <roapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <display_stream_id.h>
#include "captured_nv12_frame.h"
#include "captured_cursor_state.h"
#include "capture_backend_policy.h"
#include "display_presence.h"
#include "display_label.h"
#include "virtual_display_binding.h"
#include "dxgi_cursor_state.h"
#include "interactive_desktop.h"
#include "interactive_state.h"
#include "named_pipe_deadline.h"
#include "rd_log.h"
#include "screen_capturer_dxgi.h"
#include "screen_capturer_gdi.h"
#include "secure_desktop_cursor_state.h"
#include "secure_desktop_frame_schedule.h"
#include "secure_desktop_status_poller.h"
#include "service_host.h"
#include "session_helper_shared.h"
#include "../virtual_display/usbmmidd_virtual_display.h"
#include "wgc_plugin_capturer.h"
#include "../winrt_apartment.h"
#include "../privacy/privacy_cursor_guard.h"

namespace crossdesk {

namespace {

using Json = nlohmann::json;

thread_local bool current_frame_from_secure_desktop = false;

constexpr DWORD kSecureDesktopStatusIntervalMs = 250;
constexpr DWORD kSecureDesktopStatusPipeTimeoutMs = 500;
constexpr DWORD kSecureDesktopHelperPipeTimeoutMs = 120;
constexpr DWORD kSecureDesktopTransientErrorGraceMs = 1500;
constexpr DWORD kSecureDesktopTransientErrorLogIntervalMs = 5000;
constexpr DWORD kPostSecureDesktopRestartRetryMs = 500;
constexpr DWORD kPostSecureDesktopRestartTimeoutMs = 10000;
constexpr ULONGLONG kCaptureStallTimeoutMs = 3000;
constexpr int kSecureDesktopCaptureMinFps = 30;
constexpr int kSecureDesktopCaptureMaxIntervalMs =
    1000 / kSecureDesktopCaptureMinFps;

std::unique_ptr<ScreenCapturer> CreateCaptureBackend(ScreenCaptureMethod method) {
  switch (method) {
    case ScreenCaptureMethod::Dxgi: return std::make_unique<ScreenCapturerDxgi>();
    case ScreenCaptureMethod::Wgc: return WgcPluginCapturer::Create();
    case ScreenCaptureMethod::Gdi: return std::make_unique<ScreenCapturerGdi>();
    default: return nullptr;
  }
}

ScreenCaptureMethod CaptureBackendMethod(const ScreenCapturer* capturer) {
  if (dynamic_cast<const ScreenCapturerDxgi*>(capturer)) return ScreenCaptureMethod::Dxgi;
  if (dynamic_cast<const WgcPluginCapturer*>(capturer)) return ScreenCaptureMethod::Wgc;
  if (dynamic_cast<const ScreenCapturerGdi*>(capturer)) return ScreenCaptureMethod::Gdi;
  return ScreenCaptureMethod::Auto;
}

const char* CaptureBackendName(const ScreenCapturer* capturer) {
  if (dynamic_cast<const ScreenCapturerDxgi*>(capturer)) return "DXGI";
  if (dynamic_cast<const WgcPluginCapturer*>(capturer)) return "WGC";
  if (dynamic_cast<const ScreenCapturerGdi*>(capturer)) return "GDI";
  return "unavailable";
}

bool CapturesUsbmmiddOutput(ScreenCapturer* capturer) {
  if (!capturer) return false;
  const auto displays = capturer->GetDisplayInfoList();
  const int selected = capturer->GetCurrentMonitorIndex();
  if (selected < 0 || selected >= static_cast<int>(displays.size())) return false;
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  return displays[selected].handle &&
         GetMonitorInfoW(static_cast<HMONITOR>(displays[selected].handle), &info) &&
         IsUsbmmiddDisplayDevice(info.szDevice);
}

bool BackendCursorCapture(ScreenCapturer* capturer, bool requested) {
  // GDI cannot exclude usbmmidd's software cursor. Compose and advertise it
  // as part of the frame so native controllers suppress their extra cursor.
  // DXGI already detects cursor embedding from the selected output's metadata.
  return requested || (dynamic_cast<ScreenCapturerGdi*>(capturer) &&
                        !SharedDxgiCursorState().PrivacyCursorHidden() &&
                        CapturesUsbmmiddOutput(capturer));
}

// Debug aid: CROSSDESK_FORCE_HEADLESS=1 makes a host that has monitors behave
// as if it had none until a usbmmidd monitor is plugged, so the virtual
// display path can be exercised without unplugging cables. Once the virtual
// monitor is attached the real probe takes over again.
bool ForceHeadlessEnabled() {
  static const bool forced = [] {
    wchar_t value[8] = {0};
    const DWORD length =
        GetEnvironmentVariableW(L"CROSSDESK_FORCE_HEADLESS", value, 8);
    return length > 0 && length < 8 && value[0] == L'1';
  }();
  return forced;
}

bool ForcedHeadlessForTesting() {
  return ForceHeadlessEnabled() && CountUsbmmiddMonitors() == 0;
}

// Detect phantom/headless topology for virtual-display provisioning.
// This does not change the DXGI/WGC/GDI backend order.
bool DetectHeadlessDesktop(bool capture_stalled = false,
                           bool log_probe = false) {
  if (ForcedHeadlessForTesting()) {
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) {
      LOG_WARN("Windows capturer: CROSSDESK_FORCE_HEADLESS=1 set; treating the "
               "desktop as headless until a usbmmidd monitor is plugged");
    }
    return true;
  }
  const auto probe = ProbeDisplayPresence();
  const auto presence = ClassifyDisplayPresence(probe, capture_stalled);
  // Sessions probe repeatedly; report transitions rather than every probe.
  static std::atomic<int> last_logged{-1};
  if (last_logged.exchange(static_cast<int>(presence)) !=
      static_cast<int>(presence) || log_probe) {
    LOG_INFO("Windows capturer: display probe presence={} query_ok={} "
             "active_paths={} available_targets={} identified_targets={} "
             "unidentified_targets={} target_query_failures={} "
             "embedded_or_virtual_targets={} edid_targets={} monitor_paths={} "
             "desktop_devices={} monitors={} capture_stalled={}",
             presence == DisplayPresence::headless ? "headless" :
             presence == DisplayPresence::present ? "present" : "unknown",
             probe.query_succeeded, probe.active_paths, probe.available_targets,
             probe.identified_targets, probe.unidentified_targets,
             probe.target_query_failures, probe.embedded_or_virtual_targets,
             probe.edid_targets, probe.monitor_path_targets, probe.desktop_devices,
             probe.attached_monitors, capture_stalled);
  }
  return presence == DisplayPresence::headless;
}

std::string BuildSecureCaptureCommand(int left, int top, int width, int height,
                                      bool show_cursor,
                                      const std::string& stage,
                                      const std::string& desktop) {
  std::ostringstream stream;
  stream << kCrossDeskSecureInputCaptureCommandPrefix << left << ":" << top
         << ":" << width << ":" << height << ":" << (show_cursor ? 1 : 0);
  if (!stage.empty()) {
    stream << ":" << stage;
    if (!desktop.empty()) {
      stream << ":" << desktop;
    }
  }
  return stream.str();
}

std::string BuildSecureCaptureStartCommand(int left, int top, int width,
                                           int height, bool show_cursor,
                                           int fps,
                                           const std::string& stage,
                                           const std::string& desktop) {
  std::ostringstream stream;
  stream << kCrossDeskSecureInputCaptureStartCommandPrefix << left << ":" << top
         << ":" << width << ":" << height << ":" << (show_cursor ? 1 : 0)
         << ":" << fps;
  if (!stage.empty()) {
    stream << ":" << stage;
    if (!desktop.empty()) {
      stream << ":" << desktop;
    }
  }
  return stream.str();
}

std::string ExtractPipeTextResponse(const std::vector<uint8_t>& response) {
  if (response.empty() || response.front() != '{') {
    return "<non-text-response>";
  }
  return std::string(response.begin(), response.end());
}

bool IsTransientSecureDesktopFrameError(const std::string& error_message) {
  return error_message.rfind("pipe_unavailable:", 0) == 0 ||
         error_message.find("\"error\":\"bitblt_failed\"") != std::string::npos;
}

bool IsTransientWindowsServiceStatusError(const std::string& error) {
  return error == "pipe_unavailable" || error == "pipe_connect_failed" ||
         error == "pipe_read_failed";
}

bool ParseSecureDesktopFrameResponse(const std::vector<uint8_t>& response,
                                     std::vector<uint8_t>* nv12_frame_out,
                                     int* width_out, int* height_out,
                                     std::string* error_out) {
  if (nv12_frame_out == nullptr || width_out == nullptr ||
      height_out == nullptr) {
    return false;
  }

  if (response.size() < sizeof(CrossDeskSecureDesktopFrameHeader)) {
    if (error_out != nullptr) {
      *error_out = ExtractPipeTextResponse(response);
    }
    return false;
  }

  CrossDeskSecureDesktopFrameHeader header{};
  std::memcpy(&header, response.data(), sizeof(header));
  if (header.magic != kCrossDeskSecureDesktopFrameMagic ||
      header.version != kCrossDeskSecureDesktopFrameVersion) {
    if (error_out != nullptr) {
      *error_out = ExtractPipeTextResponse(response);
    }
    return false;
  }

  const size_t expected_size = sizeof(header) + header.payload_size;
  if (expected_size != response.size()) {
    if (error_out != nullptr) {
      *error_out = "<invalid-frame-size>";
    }
    return false;
  }

  *width_out = static_cast<int>(header.width);
  *height_out = static_cast<int>(header.height);
  nv12_frame_out->assign(response.begin() + sizeof(header), response.end());
  SharedSecureDesktopCursorState().Update(header.cursor);
  return true;
}

SecureDesktopServiceStatus ReadSecureDesktopServiceStatus() {
  SecureDesktopServiceStatus status;
  std::vector<uint8_t> response;
  DWORD error_code = 0;
  if (!QueryNamedPipeWithDeadline(kCrossDeskServicePipeName, "status",
                                  kSecureDesktopStatusPipeTimeoutMs, &response,
                                  &status.error, &error_code)) {
    status.error_code = error_code;
    return status;
  }
  Json json = Json::parse(response.begin(), response.end(), nullptr, false);
  if (json.is_discarded() || !json.is_object()) {
    status.error = "invalid_service_status_json";
    return status;
  }

  status.service_available = json.value("ok", false);
  if (!status.service_available) {
    status.error = json.value("error", std::string("service_unavailable"));
    status.error_code = json.value("code", 0u);
    status.desktop_state_known = status.error == "service_session_mismatch";
    return status;
  }

  if (!IsCurrentProcessSession(json.value("active_session_id", 0xFFFFFFFFu))) {
    status.service_available = false;
    status.desktop_state_known = true;
    status.error = "service_session_mismatch";
    return status;
  }

  if (ShouldNormalizeUnlockToUserDesktop(
          json.value("interactive_lock_screen_visible", false),
          json.value("interactive_stage", std::string()),
          json.value("session_locked", false),
          json.value("interactive_logon_ui_visible", false),
          json.value("interactive_secure_desktop_active",
                     json.value("secure_desktop_active", false)),
          json.value("credential_ui_visible", false),
          json.value("password_box_visible", false),
          json.value("unlock_ui_visible", false),
          json.value("last_session_event", std::string()))) {
    status.active_session_id = json.value("active_session_id", 0xFFFFFFFFu);
    status.interactive_stage = "user-desktop";
    status.interactive_desktop.clear();
    status.capture_active = false;
    return status;
  }

  status.active_session_id = json.value("active_session_id", 0xFFFFFFFFu);
  status.consent_ui_visible = json.value("consent_ui_visible", false);
  status.helper_running = json.value("secure_input_helper_running", false);
  status.helper_process_id = json.value("secure_input_helper_pid", 0u);
  status.interactive_stage = json.value("interactive_stage", std::string());
  status.interactive_desktop =
      json.value("interactive_input_desktop", std::string());
  const bool secure_desktop_active =
      json.value("interactive_secure_desktop_active",
                 json.value("secure_desktop_active", false));
  status.capture_active =
      status.active_session_id != 0xFFFFFFFF &&
      (secure_desktop_active ||
       IsSecureDesktopInteractionRequired(status.interactive_stage));
  return status;
}

SecureDesktopServiceStatus QuerySecureDesktopServiceStatus() {
  auto status = ReadSecureDesktopServiceStatus();
  if (IsCurrentSessionUserDesktopActive()) status.OnUserDesktopObserved();
  return status;
}

bool QuerySecureDesktopHelperCommand(DWORD session_id,
                                     const std::string& command,
                                     std::vector<uint8_t>* response_out,
                                     std::string* error_out) {
  if (response_out == nullptr) {
    return false;
  }

  const std::wstring pipe_name =
      GetCrossDeskSecureInputHelperPipeName(session_id);
  std::string error;
  DWORD code = 0;
  // Starting may have to join/release the old GPU producer first. Its reply
  // cannot use the short deadline intended for lightweight control messages.
  const DWORD timeout_ms =
      (command.rfind(kCrossDeskSecureInputCaptureCommandPrefix, 0) == 0 ||
       command.rfind(kCrossDeskSecureInputCaptureStartCommandPrefix, 0) == 0)
          ? 1000
          : kSecureDesktopHelperPipeTimeoutMs;
  const bool ok = QueryNamedPipeWithDeadline(pipe_name, command, timeout_ms,
                                             response_out, &error, &code);
  if (!ok && error_out != nullptr) {
    *error_out = error + ":" + std::to_string(code);
  }
  return ok;
}

bool QuerySecureDesktopHelperFrame(
    DWORD session_id, int left, int top, int width, int height,
    bool show_cursor, const std::string& stage, const std::string& desktop,
    std::vector<uint8_t>* nv12_frame_out, int* captured_width_out,
    int* captured_height_out, std::string* error_out) {
  if (nv12_frame_out == nullptr || captured_width_out == nullptr ||
      captured_height_out == nullptr) {
    return false;
  }

  const std::string command =
      BuildSecureCaptureCommand(left, top, width, height, show_cursor, stage,
                                desktop);
  std::vector<uint8_t> response;
  if (!QuerySecureDesktopHelperCommand(session_id, command, &response,
                                       error_out)) {
    return false;
  }

  return ParseSecureDesktopFrameResponse(response, nv12_frame_out,
                                         captured_width_out,
                                         captured_height_out, error_out);
}

}  // namespace

ScreenCapturerWin::ScreenCapturerWin() {}
ScreenCapturerWin::~ScreenCapturerWin() { Destroy(); }

void ScreenCapturerWin::ReleaseNativeCursor() {
  if (!native_cursor_guard_) return;
  native_cursor_transition_.store(true, std::memory_order_release);
  std::string error;
  if (!native_cursor_guard_->Stop(error))
    LOG_WARN("Desktop cursor restoration pending: {}", error);
  native_cursor_guard_.reset();
  native_cursor_owned_.store(false, std::memory_order_release);
  native_cursor_transition_.store(false, std::memory_order_release);
}

void ScreenCapturerWin::UpdateNativeCursor(ScreenCapturer* backend) {
  const bool required = unattended_host_ && native_cursor_enabled_.load() && !show_cursor_.load() &&
      IsCurrentSessionUserDesktopActive() &&
      (virtual_display_.active() || CapturesUsbmmiddOutput(backend));
  if (!required) {
    ReleaseNativeCursor();
    return;
  }
  std::string error;
  if (native_cursor_guard_) {
    if (native_cursor_guard_->delegated() &&
        !SharedDxgiCursorState().ExternalCursorHidden())
      native_cursor_transition_.store(true, std::memory_order_release);
    if (native_cursor_guard_->Healthy(error)) {
      native_cursor_owned_.store(!native_cursor_guard_->delegated(), std::memory_order_release);
      native_cursor_transition_.store(false, std::memory_order_release);
      return;
    }
    ReleaseNativeCursor();
  }
  const auto now = GetTickCount64();
  if (now < next_native_cursor_retry_) return;
  next_native_cursor_retry_ = now + 1000;
  native_cursor_transition_.store(true, std::memory_order_release);
  native_cursor_guard_ = std::make_unique<PrivacyCursorGuard>();
  if (native_cursor_guard_->Start(error, L"winsta0\\Default")) {
    native_cursor_owned_.store(!native_cursor_guard_->delegated(), std::memory_order_release);
    LOG_INFO("Desktop cursor ready before capture: owner={}",
             native_cursor_guard_->delegated() ? "GUI" : "persistent host");
  } else {
    LOG_WARN("Desktop cursor exclusion unavailable: {}; retaining captured cursor", error);
    native_cursor_guard_.reset();
    native_cursor_owned_.store(false, std::memory_order_release);
  }
  native_cursor_transition_.store(false, std::memory_order_release);
}

bool ScreenCapturerWin::PrepareUnattendedDisplay() {
  if (!ForceHeadlessEnabled() && unattended_host_ && virtual_display_.active() &&
      QueryAttachedPhysicalDisplayMode().width > 0) {
    // The monitor may have returned between controller connections.
    virtual_display_.Release();
    return false;
  }
  if (!unattended_host_ ||
      (!virtual_display_.active() && !DetectHeadlessDesktop())) return false;
  virtual_display_.PrepareAcquire();
  std::string error;
  if (!virtual_display_.Acquire(ResolveVirtualDisplayMode(), &error)) {
    LOG_WARN("Cannot prepare unattended capture display: {}", error);
    return false;
  }
  const auto name = GetDisplayLabel(FindUsbmmiddDisplayDeviceName());
  std::lock_guard lock(alias_mutex_);
  replacement_display_name_ = name;
  replacement_display_slot_ = name.empty() ? -1 : 0;
  return !name.empty();
}

void ScreenCapturerWin::NotifyPrivacyCapture(bool running) {
  if (privacy_)
    privacy_->CaptureChanged(running);
}

int ScreenCapturerWin::Init(const int fps, cb_desktop_data cb) {
  ResolveVirtualDisplayMode();  // Remember the attached mode before a later unplug/reboot.
  NotifyPrivacyCapture(false);
  fps_ = fps;
  cb_orig_ = cb;
  native_output_logged_.store(false, std::memory_order_relaxed);
  native_output_error_logged_.store(false, std::memory_order_relaxed);
  last_capture_progress_tick_.store(0, std::memory_order_relaxed);
  try {
    native_frame_pool_ = CapturedNv12FramePool::Create();
  } catch (...) {
    LOG_ERROR("Windows capturer: failed to create native NV12 frame pool");
    return -1;
  }
  cb_ = [this](unsigned char* data, int size, int w, int h,
              const char* reported_stream_id,
              const MiniRtcNativeVideoFrame* native_frame) {
    if (size == ScreenCapturer::kDisplayTopologyChanged) {
      SharedCapturedCursorState().Reset();
      RebuildAliasesFromImpl(true);
      display_probe_requested_.store(true, std::memory_order_relaxed);
      return;
    }
    if (size == ScreenCapturer::kBackendReset) {
      SharedCapturedCursorState().Reset();
      backend_reset_pending_.store(true, std::memory_order_relaxed);
      // Backend recovery is not a user request to disable local privacy.
      return;
    }
    if (native_cursor_transition_.load(std::memory_order_acquire)) return;
    if ((data || native_frame) && size > 0 && w > 0 && h > 0) {
      backend_frame_received_.store(true, std::memory_order_relaxed);
      backend_reset_pending_.store(false, std::memory_order_relaxed);
      last_capture_progress_tick_.store(GetTickCount64(),
                                     std::memory_order_relaxed);
    }
    if (secure_desktop_capture_active_.load(std::memory_order_relaxed)) {
      SharedDxgiCursorState().Reset();
      SharedCapturedCursorState().Reset();
      return;
    }

    const char* raw_stream_id = reported_stream_id ? reported_stream_id : "";
    std::string mapped_stream_id;
    void* frame_monitor = nullptr;
    {
      std::lock_guard<std::mutex> lock(alias_mutex_);
      auto it = stream_id_alias_.find(raw_stream_id);
      if (it != stream_id_alias_.end()) {
        mapped_stream_id = it->second;
      } else {
        // During IDD takeover, never relabel a late frame from the old output
        // as the virtual display's stream.
        if (!replacement_display_name_.empty()) return;
        // Unknown backend labels are presentation data, not wire IDs.
        // Resolve them through the selected logical display instead.
        mapped_stream_id.clear();
      }
      mapped_stream_id = ResolveDisplayStreamId(
          mapped_stream_id.c_str(), canonical_displays_.size(),
          monitor_index_.load(std::memory_order_relaxed));
      if (current_frame_has_cursor) {
        for (size_t index = 0; index < canonical_displays_.size(); ++index) {
          if (mapped_stream_id == MakeDisplayStreamId(index)) {
            frame_monitor = canonical_displays_[index].handle;
            break;
          }
        }
      }
    }
    if (privacy_ && privacy_->Engaged() && !IsWindowsPrivacyDesktopAvailable()) {
      privacy_->Fail("Secure or unavailable desktop; privacy screen will turn off");
    }
    if (mapped_stream_id.empty()) {
      if (!invalid_stream_id_logged_.exchange(true,
                                              std::memory_order_relaxed)) {
        LOG_WARN("Windows capturer dropping frame without a registered stream "
                 "id: reported='{}', size={}x{}, bytes={}",
                 raw_stream_id, w, h, size);
      }
      return;
    }
    invalid_stream_id_logged_.store(false, std::memory_order_relaxed);
    SharedCapturedCursorState().Update(current_frame_has_cursor, frame_monitor);
    if (post_secure_desktop_waiting_for_frame_.exchange(
            false, std::memory_order_relaxed)) {
      // Keep the independent secure-desktop cursor until a valid normal
      // frame commits the new source, after its cursor guard is ready.
      SharedSecureDesktopCursorState().SetActive(false);
      const ULONGLONG start_tick =
          post_secure_desktop_started_tick_.exchange(
              0, std::memory_order_relaxed);
      const ULONGLONG elapsed_ms =
          start_tick == 0 ? 0 : GetTickCount64() - start_tick;
      LOG_INFO(
          "Windows capturer first normal frame after secure desktop: "
          "reported_stream='{}', mapped_stream='{}', size={}x{}, bytes={}, "
          "elapsed_ms={}",
          raw_stream_id, mapped_stream_id, w, h, size, elapsed_ms);
    }
    EmitCapturedFrame(data, size, w, h, mapped_stream_id.c_str(),
                      native_frame);
  };

  JoinVirtualDisplayThread();
  const bool prepared_display = PrepareUnattendedDisplay();
  impl_.reset();
  const bool headless = DetectHeadlessDesktop();
  int ret = -1;
  for (const auto backend : WindowsCaptureBackendOrder()) {
    auto candidate = CreateCaptureBackend(backend);
    ret = candidate ? candidate->Init(fps_, cb_) : -1;
    if (ret == 0) {
      impl_ = std::move(candidate);
      break;
    }
    LOG_WARN("Windows capturer: {} init failed (ret={})",
             CaptureBackendName(candidate.get()), ret);
  }
  if (impl_) {
    LOG_INFO("Windows capturer: using {}{}", CaptureBackendName(impl_.get()),
             headless ? " (headless desktop)" : "");
    BuildCanonicalFromImpl();
    monitor_index_.store(0, std::memory_order_relaxed);
    if (prepared_display) {
      const auto name = replacement_display_name_;
      const auto displays = impl_->GetDisplayInfoList();
      if (std::none_of(displays.begin(), displays.end(), [&name](const DisplayInfo& display) {
            return display.name == name && display.width > 0 && display.height > 0;
          })) {
        LOG_ERROR("Prepared startup display '{}' is not available to capture", name);
        return -1;
      }
      {
        std::lock_guard lock(alias_mutex_);
        canonical_displays_.erase(canonical_displays_.begin() + 1, canonical_displays_.end());
      }
      RestoreMonitor(0);
      const auto selected = GetDisplayInfoList().front();
      LOG_INFO("Startup display '{}' bound to Display1: {}x{}",
               name, selected.width, selected.height);
    }
    return 0;
  }

  LOG_ERROR("Windows capturer: DXGI/WGC/GDI initialization failed, ret={}", ret);
  return -1;
}

int ScreenCapturerWin::Destroy() {
  Stop();
  if (unattended_host_) BeginVirtualDisplayRelease();
  // Finish display release before the provisioner and its owner go away.
  JoinVirtualDisplayThread();
  paused_.store(false, std::memory_order_relaxed);
  if (impl_) {
    impl_->Destroy();
    impl_.reset();
  }
  {
    std::lock_guard<std::mutex> lock(alias_mutex_);
    stream_id_alias_.clear();
    handle_to_canonical_index_.clear();
  }
  native_frame_pool_.reset();
  return 0;
}

bool ScreenCapturerWin::CurrentFrameIsFromSecureDesktop() {
  return current_frame_from_secure_desktop;
}

void ScreenCapturerWin::EmitCapturedFrame(
    unsigned char* data, int size, int width, int height,
    const char* stream_id, const MiniRtcNativeVideoFrame* native_frame,
    bool from_secure_desktop) {
  // Helper frames do not use DXGI's pointer plane. Do not carry normal-desktop
  // cursor suppression into the lock screen or UAC desktop.
  if (from_secure_desktop) {
    SharedDxgiCursorState().Reset();
    SharedCapturedCursorState().Reset();
  }
  // Secure-desktop capture takes priority; remove privacy without dropping
  // the helper frame or delaying the remote session.
  if (from_secure_desktop && privacy_ && privacy_->Engaged()) {
    privacy_->Fail("Secure desktop helper frame arrived; privacy screen will turn off");
  }
  if (!cb_orig_) {
    return;
  }

  // Normal and helper callbacks can originate on different threads during
  // desktop transitions. Keep diagnostics tied to this callback's source.
  struct RestoreFrameSource {
    bool previous;
    ~RestoreFrameSource() { current_frame_from_secure_desktop = previous; }
  } restore_source{
      std::exchange(current_frame_from_secure_desktop, from_secure_desktop)};

  // Privacy is a local, capture-excluded window. Always deliver the actual
  // desktop pixels, including while that window is starting or stopping.
  if (native_frame) {
    if (!native_output_logged_.exchange(true, std::memory_order_relaxed)) {
      LOG_INFO("Windows capturer native frame output enabled (type={})",
               static_cast<uint32_t>(native_frame->type));
    }
    cb_orig_(data, size, width, height, stream_id, native_frame);
    return;
  }

  CapturedNv12Frame* owned_frame = nullptr;
  if (native_frame_pool_ && data && size > 0 && width > 0 && height > 0) {
    owned_frame = native_frame_pool_->CopyFrom(
        data, static_cast<size_t>(size), static_cast<uint32_t>(width),
        static_cast<uint32_t>(height));
  }
  if (!owned_frame) {
    if (!native_output_error_logged_.exchange(true,
                                               std::memory_order_relaxed)) {
      LOG_WARN(
          "Windows capturer could not retain a native NV12 frame; falling "
          "back to copied CPU input (size={}x{}, bytes={})",
          width, height, size);
    }
    cb_orig_(data, size, width, height, stream_id, nullptr);
    return;
  }

  if (!native_output_logged_.exchange(true, std::memory_order_relaxed)) {
    LOG_INFO("Windows capturer native NV12 output enabled");
  }
  cb_orig_(nullptr, static_cast<int>(owned_frame->Size()), width, height,
           stream_id, owned_frame->Descriptor());
  owned_frame->Release();
}

void ScreenCapturerWin::RestoreMonitor(int monitor_index) {
  RebuildAliasesFromImpl();
  if (monitor_index >= 0) SwitchTo(monitor_index);
}

bool ScreenCapturerWin::TryStartBackend(
    std::unique_ptr<ScreenCapturer> candidate, int monitor_index) {
  if (!candidate) {
    LOG_WARN("Windows capturer: WGC plugin unavailable");
    return false;
  }
  const char* name = CaptureBackendName(candidate.get());
  const bool show_cursor = show_cursor_.load(std::memory_order_relaxed);
  // A candidate may deliver immediately from Start(). Publish frames only
  // after its display selection and wire aliases have been committed.
  auto committed = std::make_shared<std::atomic<bool>>(false);
  int ret = candidate->Init(fps_, [this, committed](
      unsigned char* data, int size, int width, int height,
      const char* stream, const MiniRtcNativeVideoFrame* native) {
    if (committed->load(std::memory_order_acquire))
      cb_(data, size, width, height, stream, native);
  });
  std::string required_display;
  {
    std::lock_guard<std::mutex> lock(alias_mutex_);
    required_display = replacement_display_name_;
  }
  if (ret == 0 && !required_display.empty()) {
    const auto displays = candidate->GetDisplayInfoList();
    const auto found = std::find_if(displays.begin(), displays.end(),
        [&](const DisplayInfo& display) {
          return display.name == required_display &&
                 display.width > 0 && display.height > 0;
        });
    ret = found == displays.end() ? -1 :
        candidate->SwitchTo(static_cast<int>(found - displays.begin()));
  }
  if (ret == 0) {
    UpdateNativeCursor(candidate.get());
    backend_frame_received_.store(false, std::memory_order_relaxed);
    backend_reset_pending_.store(false, std::memory_order_relaxed);
    last_capture_progress_tick_.store(GetTickCount64(), std::memory_order_relaxed);
    ret = candidate->Start(BackendCursorCapture(candidate.get(), show_cursor));
  }
  if (ret != 0) {
    LOG_WARN("Windows capturer: {} initialization/start failed (ret={})", name,
             ret);
    candidate->Destroy();
    return false;
  }
  // Commit only after the replacement is running. Failed candidates release
  // their resources without replacing the current backend.
  {
    std::lock_guard<std::mutex> lock(impl_mutex_);
    impl_ = std::move(candidate);
  }
  applied_show_cursor_ = BackendCursorCapture(impl_.get(), show_cursor);
  RestoreMonitor(monitor_index);
  committed->store(true, std::memory_order_release);
  LOG_INFO("Windows capturer: started {}", name);
  if (!required_display.empty())
    LOG_INFO("Windows capturer: replacement display '{}' captured via {}",
             required_display, name);
  return true;
}

bool ScreenCapturerWin::StartPreferredBackend(int monitor_index, ScreenCaptureMethod after) {
  bool include = after == ScreenCaptureMethod::Auto;
  for (const auto backend : WindowsCaptureBackendOrder()) {
    if (!include) { include = backend == after; continue; }
    if (TryStartBackend(CreateCaptureBackend(backend), monitor_index)) return true;
  }
  return false;
}

void ScreenCapturerWin::ResetPostSecureDesktopState(bool secure) {
  secure_desktop_capture_active_.store(secure, std::memory_order_relaxed);
  post_secure_desktop_waiting_for_frame_.store(false,
                                               std::memory_order_relaxed);
  post_secure_desktop_started_tick_.store(0, std::memory_order_relaxed);
  SharedSecureDesktopCursorState().SetActive(secure);
}

int ScreenCapturerWin::Start(bool show_cursor) {
  if (!impl_) {
    ReleaseNativeCursor();
    return -1;
  }
  if (running_.load(std::memory_order_relaxed)) {
    return 0;
  }
  ResolveVirtualDisplayMode();
  native_cursor_enabled_.store(native_cursor_default_.load());
  NotifyPrivacyCapture(false);

  show_cursor_.store(show_cursor, std::memory_order_relaxed);
  applied_show_cursor_ = show_cursor;
  ResetPostSecureDesktopState(!IsCurrentSessionUserDesktopActive());
  SharedCapturedCursorState().Reset();
  paused_.store(false, std::memory_order_relaxed);
  invalid_stream_id_logged_.store(false, std::memory_order_relaxed);
  last_capture_progress_tick_.store(0, std::memory_order_relaxed);

  // Refresh physical monitor identities before every session. HMONITOR and
  // DXGI output handles may change after an HDMI hotplug while CrossDesk stays
  // open; logical stream IDs must remain stable.
  const int requested_monitor =
      monitor_index_.load(std::memory_order_relaxed);
  // A monitor may have been attached or removed since the last session. Pick
  // the backend for the desktop as it is now, not as it was at Init.
  JoinVirtualDisplayThread();
  // Reconcile the live target, not the display name cached at process startup.
  if (!PrepareUnattendedDisplay()) {
    std::lock_guard<std::mutex> lock(alias_mutex_);
    replacement_display_name_.clear();
    replacement_display_slot_ = -1;
  }
  DetectHeadlessDesktop(false, true);
  virtual_display_ready_.store(false, std::memory_order_relaxed);
  display_probe_requested_.store(true, std::memory_order_relaxed);
  last_display_probe_tick_ = 0;
  last_capture_retry_tick_ = 0;
  last_virtual_adoption_tick_ = 0;
  physical_display_since_ = 0;
  headless_recovery_.Reset();
  impl_->Destroy();
  if (!StartPreferredBackend(requested_monitor)) {
    LOG_ERROR("Windows capturer: DXGI/WGC/GDI could not start");
    ReleaseNativeCursor();
    return -1;
  }
  applied_show_cursor_ = BackendCursorCapture(
      impl_.get(), show_cursor_.load(std::memory_order_relaxed));
  running_.store(true, std::memory_order_relaxed);
  NotifyPrivacyCapture(true);
  if (!secure_capture_thread_.joinable()) {
    secure_capture_thread_ =
        std::thread([this]() { SecureDesktopCaptureLoop(); });
  }
  return 0;
}

int ScreenCapturerWin::Stop() {
  NotifyPrivacyCapture(false);
  running_.store(false, std::memory_order_relaxed);
  last_capture_progress_tick_.store(0, std::memory_order_relaxed);
  int ret = 0;
  // Drain management operations before stopping/replacing their backend.
  virtual_display_.RequestCancel();
  StopSecureCaptureThread();
  // A recovery already in progress can publish "running" while Stop waits.
  // Publish the final stopped state after that management thread has exited.
  NotifyPrivacyCapture(false);
  JoinVirtualDisplayThread();
  ResetPostSecureDesktopState();
  if (impl_) {
    ret = impl_->Stop();
  }
  ReleaseNativeCursor();
  SharedCapturedCursorState().Reset();
  StopSecureDesktopSharedCapture(secure_shared_session_id_);
  // After the backend released its duplication of the virtual output. The
  // unplug itself can take seconds; do not hold the UI thread for it.
  // Keep the machine host's display alive between controller connections.
  // Destroy() releases it when the host hands off or exits.
  if (!unattended_host_) BeginVirtualDisplayRelease();
  return ret;
}

void ScreenCapturerWin::JoinVirtualDisplayThread() {
  if (virtual_display_thread_.joinable()) virtual_display_thread_.join();
}

void ScreenCapturerWin::BeginVirtualDisplayProvisioning() {
  if (virtual_display_busy_.load(std::memory_order_acquire)) return;
  JoinVirtualDisplayThread();
  virtual_display_.PrepareAcquire();
  if (!running_.load(std::memory_order_relaxed)) return;
  virtual_display_busy_.store(true, std::memory_order_release);
  virtual_display_thread_ = std::thread([this] {
    // A usbmmidd virtual monitor turns the phantom desktop into a real
    // display target, so the GPU backends and a chosen resolution apply.
    std::string error;
    if (virtual_display_.Acquire(ResolveVirtualDisplayMode(), &error)) {
      virtual_display_ready_.store(true, std::memory_order_release);
    } else if (error != "cancelled") {
      LOG_WARN("Windows capturer: no usbmmidd virtual display ({}); "
               "will retry while headless",
               error);
    }
    virtual_display_busy_.store(false, std::memory_order_release);
  });
}

void ScreenCapturerWin::BeginVirtualDisplayRelease() {
  JoinVirtualDisplayThread();
  virtual_display_ready_.store(false, std::memory_order_relaxed);
  if (!virtual_display_.NeedsRelease()) return;
  virtual_display_thread_ = std::thread([this] { virtual_display_.Release(); });
}

void ScreenCapturerWin::MaybeAdoptVirtualDisplay() {
  if (!virtual_display_ready_.load(std::memory_order_acquire))
    return;
  if (!running_.load(std::memory_order_relaxed)) return;
  const auto now = GetTickCount64();
  if (last_virtual_adoption_tick_ != 0 &&
      now - last_virtual_adoption_tick_ < 3000) return;
  last_virtual_adoption_tick_ = now;
  const auto virtual_name = GetDisplayLabel(FindUsbmmiddDisplayDeviceName());
  if (virtual_name.empty()) return;
  const int current_monitor = monitor_index_.load(std::memory_order_relaxed);
  LOG_INFO("Windows capturer: adopting usbmmidd display '{}' in stream slot {}",
           virtual_name, current_monitor);
  NotifyPrivacyCapture(false);
  // Drain callbacks while impl_ still names their backend. A topology callback
  // can rebuild aliases and must not observe the temporary null during a swap.
  if (impl_) impl_->Stop();
  // Public entry points dereference impl_ under impl_mutex_; take the old
  // backend out under the lock, then tear it down without holding it.
  std::unique_ptr<ScreenCapturer> previous;
  {
    std::lock_guard<std::mutex> lock(impl_mutex_);
    previous = std::move(impl_);
  }
  if (previous) previous->Destroy();
  {
    std::lock_guard<std::mutex> lock(alias_mutex_);
    replacement_display_name_ = virtual_name;
    replacement_display_slot_ = current_monitor;
  }
  if (StartPreferredBackend(current_monitor)) {
    if (!IsCurrentSessionUserDesktopActive()) impl_->Stop();
    virtual_display_ready_.store(false, std::memory_order_release);
    // The owner refreshes its display list and input mapping every tick from
    // GetDisplayInfoList(), so the new geometry is picked up without a hint.
  } else {
    LOG_ERROR("Windows capturer: no backend could start on usbmmidd; will retry");
    {
      std::lock_guard<std::mutex> lock(alias_mutex_);
      replacement_display_name_.clear();
      replacement_display_slot_ = -1;
    }
    {
      std::lock_guard<std::mutex> lock(impl_mutex_);
      impl_ = std::move(previous);
    }
    // Retain the stopped instance for Stop() and the next ordered retry.
  }
  NotifyPrivacyCapture(true);
}

int ScreenCapturerWin::Pause(int monitor_index) {
  paused_.store(true, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(impl_mutex_);
  if (!impl_) return -1;
  return impl_->Pause(monitor_index);
}

int ScreenCapturerWin::Resume(int monitor_index) {
  paused_.store(false, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(impl_mutex_);
  if (!impl_) return -1;
  return impl_->Resume(monitor_index);
}

int ScreenCapturerWin::SwitchTo(int monitor_index) {
  std::lock_guard<std::mutex> impl_lock(impl_mutex_);
  if (!impl_) return -1;
  int backend_index = -1;
  {
    std::lock_guard<std::mutex> lock(alias_mutex_);
    if (monitor_index < 0 ||
        monitor_index >= static_cast<int>(canonical_displays_.size()) ||
        canonical_displays_[monitor_index].width <= 0 ||
        canonical_displays_[monitor_index].height <= 0) return -1;
    const auto found = std::find(backend_to_canonical_.begin(),
                                  backend_to_canonical_.end(), monitor_index);
    if (found != backend_to_canonical_.end())
      backend_index = static_cast<int>(found - backend_to_canonical_.begin());
  }
  if (backend_index < 0) return -1;
  const int ret = impl_->SwitchTo(backend_index);
  if (ret == 0) {
    monitor_index_.store(monitor_index, std::memory_order_relaxed);
  }
  return ret;
}

int ScreenCapturerWin::ResetToInitialMonitor() {
  if (unattended_host_ && virtual_display_.active()) return SwitchTo(0);
  std::lock_guard<std::mutex> lock(impl_mutex_);
  if (!impl_) return -1;
  const int ret = impl_->ResetToInitialMonitor();
  RebuildAliasesFromImpl(true);
  return ret;
}

std::vector<DisplayInfo> ScreenCapturerWin::GetDisplayInfoList() {
  if (!impl_) return {};
  std::lock_guard<std::mutex> lock(alias_mutex_);
  return canonical_displays_;
}

int ScreenCapturerWin::GetCurrentMonitorIndex() const {
  return monitor_index_.load(std::memory_order_relaxed);
}

void ScreenCapturerWin::BuildCanonicalFromImpl() {
  std::lock_guard<std::mutex> lock(alias_mutex_);
  handle_to_canonical_index_.clear();
  stream_id_alias_.clear();
  canonical_displays_ = impl_->GetDisplayInfoList();
  backend_to_canonical_.clear();
  std::unordered_map<std::string, size_t> name_counts;
  for (const auto& display : canonical_displays_) {
    if (!display.name.empty()) {
      ++name_counts[display.name];
    }
  }
  for (size_t i = 0; i < canonical_displays_.size(); ++i) {
    backend_to_canonical_.push_back(static_cast<int>(i));
    auto& di = canonical_displays_[i];
    const std::string stream_id = MakeDisplayStreamId(i);
    if (di.name.empty()) {
      di.name = stream_id;
    }
    handle_to_canonical_index_[di.handle] = i;
    stream_id_alias_[stream_id] = stream_id;
    if (name_counts[di.name] == 1) {
      // Compatibility with an older WGC plugin that reports display names.
      stream_id_alias_[di.name] = stream_id;
    }
  }
}

void ScreenCapturerWin::RebuildAliasesFromImpl(bool preserve_backend_slots) {
  std::lock_guard<std::mutex> lock(alias_mutex_);
  stream_id_alias_.clear();
  auto current = impl_->GetDisplayInfoList();
  if (current.empty() || canonical_displays_.empty()) return;
  if (!replacement_display_name_.empty() && replacement_display_slot_ >= 0 &&
      replacement_display_slot_ < static_cast<int>(canonical_displays_.size())) {
    // Bind IDD (or the returning physical screen) to the same wire slot.
    // The first enumerated output may still be a phantom NVIDIA desktop.
    handle_to_canonical_index_.clear();
    const int i = BindVirtualDisplayToStream(current, replacement_display_name_,
        replacement_display_slot_, &canonical_displays_, &backend_to_canonical_);
    if (i >= 0) {
      const auto stream = MakeDisplayStreamId(replacement_display_slot_);
      stream_id_alias_[MakeDisplayStreamId(i)] = stream;
      stream_id_alias_[current[i].name] = stream;
      if (current[i].handle)
        handle_to_canonical_index_[current[i].handle] = replacement_display_slot_;
      monitor_index_.store(replacement_display_slot_, std::memory_order_relaxed);
    }
    return;
  }
  const bool stable_slots = preserve_backend_slots &&
                           dynamic_cast<ScreenCapturerDxgi*>(impl_.get());
  if (!stable_slots) backend_to_canonical_.assign(current.size(), -1);
  const auto previous_handles = handle_to_canonical_index_;
  handle_to_canonical_index_.clear();
  std::unordered_map<std::string, size_t> name_counts;
  for (const auto& display : current) {
    if (!display.name.empty()) {
      ++name_counts[display.name];
    }
  }
  auto similar = [&](const DisplayInfo& a, const DisplayInfo& b) {
    int dl = std::abs(a.left - b.left);
    int dt = std::abs(a.top - b.top);
    int dw = std::abs(a.width - b.width);
    int dh = std::abs(a.height - b.height);
    return dl <= 10 && dt <= 10 && dw <= 20 && dh <= 20;
  };
  std::vector<bool> used(canonical_displays_.size(), false);
  for (size_t current_index = 0; current_index < current.size();
       ++current_index) {
    const auto& di = current[current_index];
    int canonical_index = -1;
    if (stable_slots) {
      // New outputs have no registered wire stream in this session.
      if (current_index >= backend_to_canonical_.size() ||
          backend_to_canonical_[current_index] < 0) continue;
      canonical_index = backend_to_canonical_[current_index];
    }
    auto old_handle = previous_handles.find(di.handle);
    if (canonical_index < 0 && old_handle != previous_handles.end() &&
        old_handle->second < canonical_displays_.size() &&
        !used[old_handle->second]) {
      canonical_index = static_cast<int>(old_handle->second);
    }
    if (canonical_index < 0) {
      for (size_t i = 0; i < canonical_displays_.size(); ++i) {
        if (!used[i] && (similar(di, canonical_displays_[i]) ||
                         (di.is_primary && canonical_displays_[i].is_primary))) {
          canonical_index = static_cast<int>(i);
          break;
        }
      }
    }
    if (canonical_index < 0 && current_index < canonical_displays_.size() &&
        !used[current_index]) {
      canonical_index = static_cast<int>(current_index);
    }
    if (canonical_index < 0) {
      LOG_WARN("Windows capturer ignoring unregistered display after topology "
               "change: display='{}', index={}",
               di.name, current_index);
      continue;
    }

    used[canonical_index] = true;
    backend_to_canonical_[current_index] = canonical_index;
    auto& canonical = canonical_displays_[canonical_index];
    const std::string backend_stream_id =
        MakeDisplayStreamId(current_index);
    const std::string stable_stream_id =
        MakeDisplayStreamId(static_cast<size_t>(canonical_index));
    stream_id_alias_[backend_stream_id] = stable_stream_id;
    if (!di.name.empty() && name_counts[di.name] == 1) {
      stream_id_alias_[di.name] = stable_stream_id;
    }
    if (di.handle)
      handle_to_canonical_index_[di.handle] =
          static_cast<size_t>(canonical_index);
    canonical = di;
    if (canonical.name.empty()) {
      canonical.name = stable_stream_id;
    }
  }
  const int active = impl_->GetCurrentMonitorIndex();
  if (active >= 0 && active < static_cast<int>(backend_to_canonical_.size()) &&
      backend_to_canonical_[active] >= 0)
    monitor_index_.store(backend_to_canonical_[active]);
}

void ScreenCapturerWin::StopSecureCaptureThread() {
  if (secure_capture_thread_.joinable()) {
    secure_capture_thread_.join();
  }
}

bool ScreenCapturerWin::RestartCaptureBackendAfterSecureDesktop() {
  NotifyPrivacyCapture(false);
  if (!impl_ || !running_.load(std::memory_order_relaxed)) return false;
  const int monitor = monitor_index_.load(std::memory_order_relaxed);
  LOG_INFO("Windows capturer: restarting DXGI/WGC/GDI after secure desktop");
  impl_->Stop();
  if (StartPreferredBackend(monitor)) {
    NotifyPrivacyCapture(true);
    return true;
  }
  LOG_WARN("Windows capturer: all backend restart attempts failed");
  ReleaseNativeCursor();
  return false;
}

void ScreenCapturerWin::CheckDisplayPresence(ULONGLONG now) {
  // Topology events accelerate the next probe, but driver rebuild callbacks
  // must not flood QueryDisplayConfig. Polling also covers WGC/GDI and unplug
  // events that NVIDIA does not expose as a changed DXGI output.
  const auto elapsed = now - last_display_probe_tick_;
  if (last_display_probe_tick_ != 0 &&
      (elapsed < 250 || (elapsed < 1000 &&
       !display_probe_requested_.load(std::memory_order_relaxed)))) return;
  last_display_probe_tick_ = now;
  const bool topology_changed =
      display_probe_requested_.exchange(false, std::memory_order_relaxed);
  const bool desktop_available = IsCurrentSessionUserDesktopActive();
  if (!desktop_available && !unattended_host_) {
    headless_recovery_.ShouldProvision(now, false, false, false);
    return;
  }
  const auto last_progress =
      last_capture_progress_tick_.load(std::memory_order_relaxed);
  const bool stalled = last_progress != 0 && now >= last_progress &&
                       now - last_progress >= kCaptureStallTimeoutMs;
  const bool headless = DetectHeadlessDesktop(stalled,
                                             topology_changed);
  const bool owns_display = virtual_display_.active();
  const bool provisioning =
      virtual_display_busy_.load(std::memory_order_acquire);
  if (!provisioning) {
    std::wstring physical_device;
    const bool physical_present = !ForceHeadlessEnabled() &&
        QueryAttachedPhysicalDisplayMode(&physical_device).width > 0;
    bool physical_binding_changed = false;
    if (!owns_display && physical_present) {
      std::lock_guard lock(alias_mutex_);
      physical_binding_changed = !replacement_display_name_.empty() &&
          replacement_display_name_ != GetDisplayLabel(physical_device);
    }
    if (physical_present && (owns_display || physical_binding_changed)) {
      // Debounce a hotplug before removing the fallback that is still carrying
      // the stream. This runs on the management thread, never a frame callback.
      if (physical_display_since_ == 0) physical_display_since_ = now;
      if (now - physical_display_since_ >= 1000) {
        RestorePhysicalDisplay();
        physical_display_since_ = 0;
      }
      return;
    }
    physical_display_since_ = 0;
    // Track user resolution changes during capture, not only process startup.
    // Resolve ignores phantom, RDP and virtual-display clone modes.
    ResolveVirtualDisplayMode();
  }
  bool target_ready = false;
  if (owns_display) {
    const auto current = GetDisplayLabel(FindUsbmmiddDisplayDeviceName());
    std::lock_guard lock(alias_mutex_);
    target_ready = !current.empty() && current == replacement_display_name_;
  }
  if (!headless_recovery_.ShouldProvision(
          now, headless || (owns_display && !target_ready),
          provisioning, target_ready)) return;
  if (!running_.load(std::memory_order_relaxed)) return;
  headless_recovery_.Attempted(now);
  // The current backend has lost its usable target. Continue the same order
  // while the virtual-display worker prepares a replacement target.
  if (headless && desktop_available && impl_ &&
      dynamic_cast<ScreenCapturerGdi*>(impl_.get()) == nullptr) {
    const int current_monitor = monitor_index_.load(std::memory_order_relaxed);
    NotifyPrivacyCapture(false);
    impl_->Stop();
    const bool started = StartPreferredBackend(current_monitor, CaptureBackendMethod(impl_.get()));
    NotifyPrivacyCapture(started);
  }
  LOG_WARN("Windows capturer: confirmed headless desktop during capture "
           "(stalled={}); provisioning usbmmidd", stalled);
  BeginVirtualDisplayProvisioning();
}

void ScreenCapturerWin::RestorePhysicalDisplay() {
  JoinVirtualDisplayThread();
  if (!running_.load(std::memory_order_relaxed)) return;
  LOG_INFO("Windows capturer: physical monitor returned; restoring physical capture");
  NotifyPrivacyCapture(false);
  if (impl_) impl_->Stop();
  StopSecureDesktopSharedCapture(secure_shared_session_id_);
  virtual_display_ready_.store(false, std::memory_order_release);
  virtual_display_.Release();
  // Windows may renumber outputs after the unplug. Requery and bind the real
  // screen to the same registered wire slot, including multi-monitor hotplugs.
  std::wstring device;
  QueryAttachedPhysicalDisplayMode(&device);
  const int slot = monitor_index_.load(std::memory_order_relaxed);
  {
    std::lock_guard lock(alias_mutex_);
    replacement_display_name_ = GetDisplayLabel(device);
    replacement_display_slot_ = replacement_display_name_.empty() ? -1 : slot;
  }
  headless_recovery_.Reset();
  if (!running_.load(std::memory_order_relaxed)) return;
  const bool started = StartPreferredBackend(slot);
  if (started && !IsCurrentSessionUserDesktopActive()) impl_->Stop();
  NotifyPrivacyCapture(started);
  if (!started) ReleaseNativeCursor();
}

void ScreenCapturerWin::CheckCaptureProgress(ULONGLONG now) {
  if (!running_.load() || !impl_) return;
  if (dynamic_cast<WgcPluginCapturer*>(impl_.get()) && backend_frame_received_.load() &&
      !backend_reset_pending_.load()) return;  // Static WGC images have no new events.
  const auto last_progress = last_capture_progress_tick_.load(std::memory_order_relaxed);
  if (!last_progress || now < last_progress || now - last_progress < kCaptureStallTimeoutMs) return;
  if (last_capture_retry_tick_ && now - last_capture_retry_tick_ < kCaptureStallTimeoutMs) return;
  if (auto* dxgi = dynamic_cast<ScreenCapturerDxgi*>(impl_.get());
      dxgi && (!last_capture_retry_tick_ || last_progress > last_capture_retry_tick_)) {
    LOG_WARN("Windows capturer: DXGI has no frame; refreshing the selected output");
    dxgi->RequestTopologyRefresh();
  } else {
    const auto failed = CaptureBackendMethod(impl_.get());
    const int monitor = monitor_index_.load(std::memory_order_relaxed);
    LOG_WARN("Windows capturer: {} has no frame; advancing DXGI/WGC/GDI fallback", CaptureBackendName(impl_.get()));
    NotifyPrivacyCapture(false);
    impl_->Stop();
    // Exhausting the sequence starts a fresh ordered recovery cycle.
    const bool started = StartPreferredBackend(monitor, failed) || StartPreferredBackend(monitor);
    NotifyPrivacyCapture(started);
    if (!started) ReleaseNativeCursor();
  }
  last_capture_retry_tick_ = now;
}

void ScreenCapturerWin::ApplyCursorCaptureSetting() {
  const bool requested = show_cursor_.load(std::memory_order_relaxed);
  if (!impl_) return;
  const bool effective = BackendCursorCapture(impl_.get(), requested);
  if (effective == applied_show_cursor_) return;
  int ret = 0;
  if (auto* gdi = dynamic_cast<ScreenCapturerGdi*>(impl_.get())) {
    gdi->SetCursorCapture(effective);
  } else if (auto* dxgi = dynamic_cast<ScreenCapturerDxgi*>(impl_.get())) {
    dxgi->SetCursorCapture(effective);
  } else if (auto* wgc = dynamic_cast<WgcPluginCapturer*>(impl_.get())) {
    ret = wgc->SetCursorCapture(effective);
  }
  if (ret == 0) {
    applied_show_cursor_ = effective;
    LOG_INFO("Windows capturer cursor capture updated: backend={}, requested={}, effective={}",
             CaptureBackendName(impl_.get()), requested, effective);
  } else {
    LOG_WARN("Windows capturer cursor update failed, ret={}; will retry", ret);
  }
}

bool ScreenCapturerWin::GetCurrentCaptureRegion(int* left, int* top, int* width,
                                                int* height,
                                                std::string* display_name) {
  if (left == nullptr || top == nullptr || width == nullptr ||
      height == nullptr || display_name == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(alias_mutex_);
  if (canonical_displays_.empty()) {
    return false;
  }

  int current_monitor = monitor_index_.load(std::memory_order_relaxed);
  if (current_monitor < 0 ||
      current_monitor >= static_cast<int>(canonical_displays_.size())) {
    current_monitor = 0;
  }

  const auto& display = canonical_displays_[current_monitor];
  const int capture_width = display.width & ~1;
  const int capture_height = display.height & ~1;
  if (capture_width <= 0 || capture_height <= 0) {
    return false;
  }

  *left = display.left;
  *top = display.top;
  *width = capture_width;
  *height = capture_height;
  *display_name = MakeDisplayStreamId(static_cast<size_t>(current_monitor));
  return true;
}

void ScreenCapturerWin::CloseSecureDesktopSharedFrame() {
  if (secure_frame_view_ != nullptr) {
    UnmapViewOfFile(secure_frame_view_);
    secure_frame_view_ = nullptr;
  }
  if (secure_frame_ready_event_ != nullptr) {
    CloseHandle(secure_frame_ready_event_);
    secure_frame_ready_event_ = nullptr;
  }
  if (secure_frame_mapping_ != nullptr) {
    CloseHandle(secure_frame_mapping_);
    secure_frame_mapping_ = nullptr;
  }
  secure_frame_view_size_ = 0;
}

void ScreenCapturerWin::StopSecureDesktopSharedCapture(DWORD session_id) {
  DWORD target_session_id = session_id;
  if (target_session_id == 0xFFFFFFFF) {
    target_session_id = secure_shared_session_id_;
  }

  if ((secure_shared_capture_started_ || secure_shared_start_pending_) &&
      target_session_id != 0xFFFFFFFF) {
    std::vector<uint8_t> response;
    std::string error_message;
    QuerySecureDesktopHelperCommand(
        target_session_id, kCrossDeskSecureInputCaptureStopCommand, &response,
        &error_message);
  }

  CloseSecureDesktopSharedFrame();
  secure_shared_capture_started_ = false;
  secure_shared_start_pending_ = false;
  secure_shared_session_id_ = 0xFFFFFFFF;
  secure_shared_configuration_ = {};
  secure_shared_last_keepalive_ = 0;
}

bool ScreenCapturerWin::OpenSecureDesktopSharedFrame(DWORD session_id,
                                                     size_t min_size,
                                                     std::string* error_out) {
  if (secure_frame_view_ != nullptr &&
      secure_shared_session_id_ == session_id &&
      secure_frame_view_size_ >= min_size) {
    return true;
  }

  CloseSecureDesktopSharedFrame();

  const std::wstring mapping_name =
      GetCrossDeskSecureDesktopFrameMappingName(session_id);
  HANDLE frame_mapping =
      OpenFileMappingW(FILE_MAP_READ, FALSE, mapping_name.c_str());
  if (frame_mapping == nullptr) {
    if (error_out != nullptr) {
      *error_out = "open_frame_mapping_failed:" +
                   std::to_string(GetLastError());
    }
    return false;
  }

  auto* frame_view =
      static_cast<uint8_t*>(MapViewOfFile(frame_mapping, FILE_MAP_READ, 0, 0, 0));
  if (frame_view == nullptr) {
    const DWORD error = GetLastError();
    CloseHandle(frame_mapping);
    if (error_out != nullptr) {
      *error_out = "map_frame_view_failed:" + std::to_string(error);
    }
    return false;
  }

  const std::wstring event_name =
      GetCrossDeskSecureDesktopFrameReadyEventName(session_id);
  HANDLE frame_ready_event =
      OpenEventW(SYNCHRONIZE, FALSE, event_name.c_str());
  if (frame_ready_event == nullptr) {
    const DWORD error = GetLastError();
    UnmapViewOfFile(frame_view);
    CloseHandle(frame_mapping);
    if (error_out != nullptr) {
      *error_out = "open_frame_event_failed:" + std::to_string(error);
    }
    return false;
  }

  secure_frame_mapping_ = frame_mapping;
  secure_frame_ready_event_ = frame_ready_event;
  secure_frame_view_ = frame_view;
  secure_frame_view_size_ = min_size;
  secure_shared_session_id_ = session_id;
  return true;
}

bool ScreenCapturerWin::ReadSecureDesktopSharedFrame(
    DWORD wait_ms, std::vector<uint8_t>* nv12_frame_out, int* width_out,
    int* height_out, std::string* error_out) {
  if (nv12_frame_out == nullptr || width_out == nullptr ||
      height_out == nullptr || secure_frame_view_ == nullptr ||
      secure_frame_ready_event_ == nullptr) {
    return false;
  }

  const DWORD wait_result = WaitForSingleObject(secure_frame_ready_event_,
                                                wait_ms);
  if (wait_result == WAIT_TIMEOUT) {
    if (error_out != nullptr) {
      *error_out = "frame_wait_timeout";
    }
    return false;
  }
  if (wait_result != WAIT_OBJECT_0) {
    if (error_out != nullptr) {
      *error_out = "frame_wait_failed:" + std::to_string(GetLastError());
    }
    return false;
  }

  auto* header =
      reinterpret_cast<CrossDeskSecureDesktopSharedFrameHeader*>(
          secure_frame_view_);
  if (header->magic != kCrossDeskSecureDesktopFrameMagic ||
      header->version != kCrossDeskSecureDesktopFrameVersion) {
    if (error_out != nullptr) {
      *error_out = "invalid_shared_frame_header";
    }
    return false;
  }
  if (header->writing != 0) {
    if (error_out != nullptr) {
      *error_out = "shared_frame_write_in_progress";
    }
    return false;
  }

  const uint32_t sequence = header->sequence;
  const uint32_t payload_size = header->payload_size;
  const uint32_t buffer_size = header->buffer_size;
  if (payload_size == 0 || payload_size > buffer_size ||
      sizeof(*header) + static_cast<size_t>(payload_size) >
          secure_frame_view_size_) {
    if (error_out != nullptr) {
      *error_out = "invalid_shared_frame_size";
    }
    return false;
  }

  nv12_frame_out->resize(payload_size);
  const SecureDesktopCursorSnapshot cursor = header->cursor;
  const int captured_width = static_cast<int>(header->width);
  const int captured_height = static_cast<int>(header->height);
  std::memcpy(nv12_frame_out->data(), secure_frame_view_ + sizeof(*header),
              payload_size);
  MemoryBarrier();
  if (header->writing != 0 || header->sequence != sequence) {
    if (error_out != nullptr) {
      *error_out = "shared_frame_changed_during_read";
    }
    return false;
  }

  *width_out = captured_width;
  *height_out = captured_height;
  SharedSecureDesktopCursorState().Update(cursor);
  return true;
}

bool ScreenCapturerWin::StartSecureDesktopSharedCapture(
    DWORD session_id, int left, int top, int width, int height,
    const std::string& stage, const std::string& desktop, bool show_cursor,
    int fps,
    std::string* error_out) {
  const size_t payload_size = static_cast<size_t>(width) * height * 3 / 2;
  const size_t mapping_size =
      sizeof(CrossDeskSecureDesktopSharedFrameHeader) + payload_size;
  if (payload_size == 0) {
    if (error_out != nullptr) {
      *error_out = "invalid_capture_size";
    }
    return false;
  }

  const SharedCaptureConfiguration configuration{left, top, width, height,
                                                 show_cursor, fps};
  if (secure_shared_capture_started_ &&
      secure_shared_session_id_ == session_id &&
      secure_shared_configuration_ == configuration &&
      OpenSecureDesktopSharedFrame(session_id, mapping_size, error_out)) {
    const auto now = GetTickCount64();
    if (now - secure_shared_last_keepalive_ >= 1000) {
      // Reuse the idempotent start operation to renew the producer's lease.
      // If a stop packet is lost, the helper still restores the local cursor.
      secure_shared_last_keepalive_ = now;
      std::vector<uint8_t> response;
      std::string error;
      const auto command = BuildSecureCaptureStartCommand(
          left, top, width, height, show_cursor, fps, stage, desktop);
      const bool sent = QuerySecureDesktopHelperCommand(session_id, command, &response, &error);
      const auto reply = Json::parse(response.begin(), response.end(), nullptr, false);
      if (!sent || !reply.is_object() || !reply.value("ok", false))
        LOG_WARN("Secure capture keepalive failed; retaining available frames: {}", error);
    }
    return true;
  }

  if (secure_shared_session_id_ != session_id) {
    StopSecureDesktopSharedCapture(secure_shared_session_id_);
  } else {
    // capture-start performs the replacement atomically in the helper. Avoid
    // a separate stop round trip and keep retries idempotent after a timeout.
    CloseSecureDesktopSharedFrame();
  }
  secure_shared_capture_started_ = false;
  secure_shared_start_pending_ = true;
  secure_shared_session_id_ = session_id;

  const std::string command = BuildSecureCaptureStartCommand(
      left, top, width, height, show_cursor, fps, stage, desktop);
  std::vector<uint8_t> response;
  if (!QuerySecureDesktopHelperCommand(session_id, command, &response,
                                       error_out)) {
    secure_shared_start_pending_ =
        error_out && IsPendingSecureCaptureStart(*error_out);
    return false;
  }

  secure_shared_start_pending_ = false;
  Json json = Json::parse(response.begin(), response.end(), nullptr, false);
  if (json.is_discarded() || !json.value("ok", false)) {
    if (error_out != nullptr) {
      *error_out = ExtractPipeTextResponse(response);
    }
    return false;
  }

  secure_shared_capture_started_ = true;
  secure_shared_session_id_ = session_id;
  secure_shared_configuration_ = configuration;
  secure_shared_last_keepalive_ = GetTickCount64();

  if (!OpenSecureDesktopSharedFrame(session_id, mapping_size, error_out)) {
    StopSecureDesktopSharedCapture(session_id);
    return false;
  }

  return true;
}

void ScreenCapturerWin::SecureDesktopCaptureLoop() {
  // Virtual-display adoption constructs WGC on this std::thread, which does
  // not inherit the UI thread's WinRT apartment.
  ScopedWinRtApartment apartment;
  const int frame_interval_ms =
      fps_ > 0 ? (std::min)(kSecureDesktopCaptureMaxIntervalMs, 1000 / fps_)
               : kSecureDesktopCaptureMaxIntervalMs;
  ULONGLONG last_error_tick = 0;
  ULONGLONG capture_stage_started_tick = 0;
  bool post_secure_restart_pending = false;
  bool desktop_was_unavailable = false;
  std::string last_desktop_probe_reason;
  bool privacy_recovery_pending = false;
  bool normal_backend_suspended = false;
  ULONGLONG post_secure_restart_deadline_tick = 0;
  ULONGLONG last_post_secure_restart_tick = 0;
  SecureDesktopServiceStatus status;
  SecureDesktopStatusPoller status_poller(
      QuerySecureDesktopServiceStatus,
      std::chrono::milliseconds(kSecureDesktopStatusIntervalMs));
  SecureDesktopFrameSchedule frame_schedule;
  frame_schedule.Reset(GetTickCount64());
  ULONGLONG stats_started = GetTickCount64();
  unsigned shared_frames = 0, fallback_frames = 0, shared_waits = 0;
  unsigned shared_restarts = 0;
  int64_t max_status_ms = 0;
  auto report_stats = [&](bool force) {
    const ULONGLONG elapsed = GetTickCount64() - stats_started;
    if (elapsed == 0 || (!force && elapsed < 5000)) return;
    if (shared_frames || fallback_frames || shared_waits || shared_restarts) {
      LOG_DEBUG(
          "Secure capture delivery: shared_fps={:.1f}, fallback_fps={:.1f}, "
          "pending_waits={}, shared_restarts={}, status_max_ms={}",
          shared_frames * 1000.0 / elapsed, fallback_frames * 1000.0 / elapsed,
          shared_waits, shared_restarts, max_status_ms);
    }
    stats_started = GetTickCount64();
    shared_frames = fallback_frames = shared_waits = shared_restarts = 0;
    max_status_ms = 0;
  };
  std::vector<uint8_t> secure_frame;
  ULONGLONG last_shared_error_tick = 0;
  auto report_shared_error = [&](const char* operation,
                                 const std::string& error) {
    const ULONGLONG now = GetTickCount64();
    if (last_shared_error_tick == 0 || now - last_shared_error_tick >= 5000) {
      LOG_WARN(
          "Windows capturer secure shared capture {} failed; using pipe "
          "fallback, stage='{}', session_id={}, error={}",
          operation, status.interactive_stage, status.active_session_id, error);
      last_shared_error_tick = now;
    }
  };

  while (running_.load(std::memory_order_relaxed)) {
    // Capture follows this process's session, including an active RDP session.
    // Privacy additionally requires the console desktop and must not gate video.
    std::string desktop_probe_reason;
    const bool desktop_available = IsCurrentSessionUserDesktopActive(&desktop_probe_reason);
    if (desktop_probe_reason != last_desktop_probe_reason) {
      LOG_INFO("Windows capturer: user desktop probe: {}",
               desktop_available ? "available" : desktop_probe_reason);
      last_desktop_probe_reason = desktop_probe_reason;
    }
    const bool privacy_available =
        desktop_available && IsWindowsPrivacyDesktopAvailable();
    if (!desktop_available) desktop_was_unavailable = true;
    // Remember automatic privacy even if the connection started while locked
    // and its first enable attempt has already failed.
    if (!privacy_available) {
      privacy_recovery_pending = true;
      if (privacy_) privacy_->SuspendForDesktop();
    }
    if (paused_.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    const ULONGLONG now = GetTickCount64();
    const auto frame_started = std::chrono::steady_clock::now();
    report_stats(false);
    auto sample = status_poller.Take();
    if (sample || (desktop_available && status.capture_active)) {
      auto latest = sample ? std::move(sample->status) : status;
      // Recheck when applying a queued reply, not only when querying. A local
      // unlock must never be undone by a delayed secure-desktop snapshot.
      if (desktop_available) latest.OnUserDesktopObserved();
      const auto previous = std::exchange(status, std::move(latest));
      if (sample) max_status_ms = (std::max)(max_status_ms, sample->query_ms);
      if (status.service_available != previous.service_available ||
          status.error != previous.error) {
        if (status.service_available) {
          LOG_INFO(
              "Windows capturer secure desktop service available, "
              "polling session_id={}",
              status.active_session_id);
        } else if (IsTransientWindowsServiceStatusError(status.error)) {
          LOG_INFO(
              "Windows capturer secure desktop service temporarily "
              "unavailable: error={}, code={}, capture_active={}",
              status.error, status.error_code, status.capture_active);
        } else {
          LOG_WARN(
              "Windows capturer secure desktop service unavailable: "
              "error={}, code={}",
              status.error, status.error_code);
        }
      }

      secure_desktop_capture_active_.store(status.capture_active,
                                           std::memory_order_relaxed);
      if (status.capture_active)
        SharedSecureDesktopCursorState().SetActive(true);
      if (status.capture_active != previous.capture_active ||
          status.interactive_stage != previous.interactive_stage ||
          status.active_session_id != previous.active_session_id ||
          status.helper_process_id != previous.helper_process_id) {
        report_stats(true);
        // A restarted helper can leave our mapping/event handles alive, but
        // their old producer is gone. Explicitly establish a fresh stream.
        if (status.capture_active != previous.capture_active ||
            status.active_session_id != previous.active_session_id ||
            status.helper_process_id != previous.helper_process_id) {
          StopSecureDesktopSharedCapture(secure_shared_session_id_);
          frame_schedule.Reset(now);
        }
        const bool secure_capture_started =
            !previous.capture_active && status.capture_active;
        const bool secure_capture_ended =
            previous.capture_active && !status.capture_active;
        capture_stage_started_tick = now;
        LOG_INFO(
            "Windows capturer secure desktop state: active={}, stage='{}', "
            "session_id={}",
            status.capture_active, status.interactive_stage,
            status.active_session_id);
        if (secure_capture_started) {
          post_secure_restart_pending = false;
          post_secure_desktop_waiting_for_frame_.store(
              false, std::memory_order_relaxed);
          post_secure_desktop_started_tick_.store(0, std::memory_order_relaxed);
        } else if (secure_capture_ended) {
          post_secure_restart_pending = true;
          post_secure_restart_deadline_tick =
              now + kPostSecureDesktopRestartTimeoutMs;
          last_post_secure_restart_tick = 0;
          post_secure_desktop_waiting_for_frame_.store(
              true, std::memory_order_relaxed);
          post_secure_desktop_started_tick_.store(now,
                                                  std::memory_order_relaxed);
        }
      }
    }

    // A locked session can still report Default, and helper status can be
    // unavailable. Observe local unlocks independently so recovery publishes
    // a fresh capture-running notification in either case.
    if (desktop_available && desktop_was_unavailable) {
      desktop_was_unavailable = false;
      post_secure_restart_pending = true;
      post_secure_restart_deadline_tick =
          now + kPostSecureDesktopRestartTimeoutMs;
      last_post_secure_restart_tick = 0;
    }

    if ((!desktop_available || status.capture_active) &&
        !normal_backend_suspended) {
      // LockApp may return to Default while the session remains locked. Merely
      // dropping callbacks leaves a second DXGI/GDI/WGC pipeline capturing and
      // converting the same desktop alongside the helper. Stop it once, also
      // when local lock detection precedes the service/helper startup.
      std::lock_guard<std::mutex> lock(impl_mutex_);
      if (impl_) {
        impl_->Stop();
        normal_backend_suspended = true;
        DWORD process_session_id = 0xFFFFFFFF;
        ProcessIdToSessionId(GetCurrentProcessId(), &process_session_id);
        LOG_INFO("Windows capturer: normal backend suspended, "
                 "user_desktop_available={}, secure_capture_active={}, "
                 "process_session_id={}, console_session_id={}",
                 desktop_available, status.capture_active, process_session_id,
                 WTSGetActiveConsoleSessionId());
      }
    }

    if (unattended_host_ && !desktop_available) {
      ReleaseNativeCursor();
      CheckDisplayPresence(now);
      MaybeAdoptVirtualDisplay();
    }
    if (!status.capture_active || status.active_session_id == 0xFFFFFFFF) {
      StopSecureDesktopSharedCapture(secure_shared_session_id_);
      // Normal capture was intentionally unavailable on the secure desktop.
      // Do not schedule a second topology/stall rebuild during its recovery.
      if (!post_secure_restart_pending && desktop_available &&
          !normal_backend_suspended) {
        UpdateNativeCursor(impl_.get());
        CheckDisplayPresence(now);
        MaybeAdoptVirtualDisplay();
        CheckCaptureProgress(now);
      }
      if (post_secure_restart_pending && desktop_available) {
        if (now >= post_secure_restart_deadline_tick) {
          LOG_WARN(
              "Windows capturer: capture backend restart after secure desktop "
              "still unavailable; retrying");
          // The normal backend is stopped. Retain recovery ownership instead
          // of falling back to a stall probe on a thread that is not running.
          post_secure_restart_deadline_tick =
              now + kPostSecureDesktopRestartTimeoutMs;
        } else if (last_post_secure_restart_tick == 0 ||
                   now - last_post_secure_restart_tick >=
                       kPostSecureDesktopRestartRetryMs) {
          last_post_secure_restart_tick = now;
          post_secure_restart_pending =
              !RestartCaptureBackendAfterSecureDesktop();
          if (!post_secure_restart_pending) {
            normal_backend_suspended = false;
            // Input callbacks may have suspended privacy between local probes.
            privacy_recovery_pending = true;
          }
        }
      }
      if (privacy_recovery_pending && privacy_available &&
          !post_secure_restart_pending && !normal_backend_suspended) {
        privacy_recovery_pending = false;
        if (privacy_) privacy_->ResumeAfterDesktopRecovery();
      }
      // Apply once after any backend/desktop recovery in this iteration.
      if (desktop_available && !post_secure_restart_pending && !normal_backend_suspended) {
        ApplyCursorCaptureSetting();
      }
      std::this_thread::sleep_for(
          std::chrono::milliseconds(status.service_available ? 50 : 200));
      continue;
    }

    if (!status.helper_running) {
      StopSecureDesktopSharedCapture(secure_shared_session_id_);
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      continue;
    }

    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    std::string display_name;
    if (!GetCurrentCaptureRegion(&left, &top, &width, &height, &display_name)) {
      StopSecureDesktopSharedCapture(secure_shared_session_id_);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    int captured_width = 0;
    int captured_height = 0;
    std::string error_message;
    bool frame_delivered = false;
    const bool show_cursor = show_cursor_.load(std::memory_order_relaxed);
    const int shared_fps = fps_ > 0
                               ? (std::max)(kSecureDesktopCaptureMinFps, fps_)
                               : kSecureDesktopCaptureMinFps;

    if (secure_shared_capture_started_ &&
        frame_schedule.SharedCaptureStalled(now)) {
      LOG_WARN("Secure shared capture stalled; restarting producer");
      StopSecureDesktopSharedCapture(secure_shared_session_id_);
      frame_schedule.Reset(GetTickCount64());
      ++shared_restarts;
    }

    bool shared_ready = false;
    const bool was_started = secure_shared_capture_started_;
    if (was_started || frame_schedule.CanStart(now)) {
      shared_ready = StartSecureDesktopSharedCapture(
          status.active_session_id, left, top, width, height,
          status.interactive_stage, status.interactive_desktop, show_cursor,
          shared_fps, &error_message);
      if (!shared_ready) {
        if (!secure_shared_start_pending_) report_shared_error("start", error_message);
        frame_schedule.OnStartFailure(GetTickCount64());
      } else if (!was_started) {
        frame_schedule.Reset(GetTickCount64());
      }
    }

    if (shared_ready &&
        ReadSecureDesktopSharedFrame(
            static_cast<DWORD>((std::max)(50, frame_interval_ms + 20)),
            &secure_frame, &captured_width, &captured_height, &error_message)) {
      frame_delivered = true;
      ++shared_frames;
      frame_schedule.OnSharedFrame(GetTickCount64());
    }

    const bool frame_pending = secure_shared_start_pending_ ||
        (shared_ready && !frame_delivered && IsPendingSecureDesktopFrame(error_message));
    if (frame_pending) ++shared_waits;
    if (shared_ready && !frame_delivered && !frame_pending) {
      report_shared_error("read", error_message);
      StopSecureDesktopSharedCapture(secure_shared_session_id_);
      frame_schedule.OnStartFailure(GetTickCount64());
    }
    if (!frame_delivered && !frame_pending &&
        QuerySecureDesktopHelperFrame(
            status.active_session_id, left, top, width, height, show_cursor,
            status.interactive_stage, status.interactive_desktop, &secure_frame,
            &captured_width, &captured_height, &error_message)) {
      frame_delivered = true;
      ++fallback_frames;
    }

    if (frame_delivered && !secure_frame.empty()) {
      EmitCapturedFrame(secure_frame.data(),
                        static_cast<int>(secure_frame.size()), captured_width,
                        captured_height, display_name.c_str(), nullptr, true);
    }

    if (!frame_delivered && !frame_pending) {
      const bool transient_error =
          IsTransientSecureDesktopFrameError(error_message);
      const bool in_grace_period = capture_stage_started_tick != 0 &&
                                   now - capture_stage_started_tick <
                                       kSecureDesktopTransientErrorGraceMs;
      const DWORD log_interval =
          transient_error ? kSecureDesktopTransientErrorLogIntervalMs : 1000;
      if (transient_error && in_grace_period) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(frame_interval_ms));
        continue;
      }
      if (now - last_error_tick >= log_interval) {
        if (transient_error) {
          LOG_INFO(
              "Windows capturer secure desktop transient frame query failed, "
              "stage='{}', session_id={}, error={}",
              status.interactive_stage, status.active_session_id,
              error_message);
        } else {
          LOG_WARN(
              "Windows capturer secure desktop frame query failed, stage='{}', "
              "session_id={}, error={}",
              status.interactive_stage, status.active_session_id,
              error_message);
        }
        last_error_tick = now;
      }
    }

    // The shared producer owns frame pacing. Waiting again here can miss the
    // next publication (and overwrite a pending frame) due to timer rounding.
    if (shared_ready && (frame_delivered || frame_pending)) continue;

    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - frame_started)
            .count();
    const int sleep_ms = SecureDesktopFrameSchedule::RemainingFrameDelay(
        frame_interval_ms, static_cast<uint64_t>(elapsed_ms));
    if (sleep_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
  }

  report_stats(true);
  StopSecureDesktopSharedCapture(secure_shared_session_id_);
  secure_desktop_capture_active_.store(false, std::memory_order_relaxed);
  SharedSecureDesktopCursorState().SetActive(false);
}

}  // namespace crossdesk
