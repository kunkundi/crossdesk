/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#include <ShlObj.h>
#include <Windows.h>
#include <dwmapi.h>
#include <winternl.h>
#include <wtsapi32.h>

#include <algorithm>
#include <vector>

#include "../windows_thread_dpi.h"
#include "privacy_backend.h"
#include "privacy_band_guard.h"
#include "privacy_capture_exclusion.h"
#include "privacy_cursor_guard.h"
#include "privacy_input_guard.h"
#include "privacy_topology.h"
#include "privacy_window_visibility.h"
#include "rd_log.h"

namespace crossdesk {
namespace {

constexpr wchar_t kClassName[] = L"CrossDesk.PrivacyWindow";
constexpr UINT kEmergency = WM_APP + 71;
constexpr int kHotkeyId = 0x4353;

std::string Error(const char* operation, DWORD code = GetLastError()) {
  const std::string result = std::string(operation) +
                             " failed (Windows error " + std::to_string(code) +
                             ")";
  LOG_ERROR("Privacy: {}", result);
  return result;
}

bool DesktopName(HDESK desktop, wchar_t (&name)[256]) {
  DWORD needed = 0;
  return desktop && GetUserObjectInformationW(desktop, UOI_NAME, name,
                                              sizeof(name), &needed);
}

struct MonitorEnumeration {
  std::vector<RECT> rects;
  bool ok = true;
};

BOOL CALLBACK EnumMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
  auto& result = *reinterpret_cast<MonitorEnumeration*>(data);
  MONITORINFO info{sizeof(info)};
  if (!GetMonitorInfoW(monitor, &info)) {
    Error("GetMonitorInfoW");
    result.ok = false;
    return FALSE;
  }
  result.rects.push_back(info.rcMonitor);
  return TRUE;
}

class WindowsPrivacyBackend final : public PrivacyBackend {
 public:
  WindowsPrivacyBackend() {
    wake_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!wake_event_) wake_error_ = Error("CreateEventW privacy commands");
  }

  ~WindowsPrivacyBackend() override {
    Recover();
    if (sink_) DestroyWindow(sink_);
    if (registered_) UnregisterClassW(kClassName, GetModuleHandleW(nullptr));
    if (wake_event_) CloseHandle(wake_event_);
  }

  PrivacyCapabilities Query() override {
    if (!wake_event_) return {false, false, wake_error_};
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const auto rtl = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (!rtl || rtl(&version) != 0 || version.dwMajorVersion < 10 ||
        version.dwBuildNumber < 19041) {
      return {false, false, "Requires Windows 10 2004 (build 19041) or later"};
    }
    BOOL composed = FALSE;
    if (FAILED(DwmIsCompositionEnabled(&composed)) || !composed) {
      return {false, false, "Desktop composition is unavailable"};
    }
    if (GetSystemMetrics(SM_REMOTESESSION)) {
      return {false, false, "Privacy screen requires a local console session"};
    }
    if (!IsWindowsPrivacyDesktopAvailable()) {
      return {false, false,
              "Privacy screen requires the unlocked Default desktop"};
    }
    QUERY_USER_NOTIFICATION_STATE shell_state{};
    if (FAILED(SHQueryUserNotificationState(&shell_state)) ||
        shell_state == QUNS_RUNNING_D3D_FULL_SCREEN) {
      return {false, false,
              "Exclusive fullscreen or unknown display presentation cannot "
              "guarantee privacy coverage"};
    }
    std::string high_band_error;
    if (!PrivacyBandGuard::Available(high_band_error))
      return {false, false, high_band_error};
    return {true, true,
            "High-band privacy screen available"};
  }

  bool Enable(bool block_input, const PrivacyScreenText& text,
              std::string& error) override {
    const auto caps = Query();
    if (!caps.overlay) {
      error = caps.reason;
      return false;
    }
    if (!Initialize(error)) return false;
    emergency_ = false;
    health_error_.clear();
    if (!RegisterHotKey(sink_, kHotkeyId,
                        MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,
                        VK_F12)) {
      error = Error("RegisterHotKey Ctrl+Alt+Shift+F12");
      return false;
    }
    hotkey_ = true;
    if (!cursor_guard_.Start(error)) return false;
    block_input_ = block_input;
    if (block_input && !input_guard_.Start(sink_, kEmergency, error))
      return false;
    if (!band_guard_.Start(text, error)) return false;
    if (!ReconcileMonitors(error)) return false;
    layout_changed_ = false;
    event_owner_ = this;
    for (DWORD event : {EVENT_OBJECT_SHOW, EVENT_OBJECT_REORDER,
                        EVENT_OBJECT_LOCATIONCHANGE}) {
      auto hook = SetWinEventHook(event, event, nullptr, WindowEvent, 0, 0,
                                  WINEVENT_OUTOFCONTEXT);
      if (!hook) {
        error = Error("Watch privacy window coverage changes");
        return false;
      }
      window_events_.push_back(hook);
    }
    return true;
  }

  void Recover() override {
    for (auto& hook : window_events_) {
      if (UnhookWinEvent(hook))
        hook = nullptr;
      else
        Error("Unhook privacy window events");
    }
    window_events_.erase(
        std::remove(window_events_.begin(), window_events_.end(), nullptr),
        window_events_.end());
    if (window_events_.empty()) event_owner_ = nullptr;
    std::string band_error;
    band_guard_.Stop(band_error);
    if (band_guard_.stopped()) windows_.clear();
    input_guard_.Stop();
    block_input_ = false;
    if (hotkey_) {
      if (UnregisterHotKey(sink_, kHotkeyId) ||
          GetLastError() == ERROR_HOTKEY_NOT_REGISTERED)
        hotkey_ = false;
      else
        Error("UnregisterHotKey");
    }
    // The sink lives across sessions. Do not let an already-posted emergency
    // command from a closed session cancel a subsequent connection.
    if (sink_) {
      MSG discarded{};
      while (PeekMessageW(&discarded, sink_, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
      }
      while (
          PeekMessageW(&discarded, sink_, kEmergency, kEmergency, PM_REMOVE)) {
      }
    }
    last_check_ = 0;
    last_health_ = {};
    last_obstruction_ = nullptr;
    last_obstruction_log_ = 0;
    layout_changed_ = false;
    topology_pending_ = false;
    topology_pending_since_ = 0;
    coverage_dirty_ = false;
    emergency_ = false;
    health_error_.clear();
    std::string cursor_error;
    cursor_guard_.Stop(cursor_error);
  }

  bool IsRecovered() const override {
    return band_guard_.stopped() && windows_.empty() &&
           window_events_.empty() && input_guard_.stopped() && !hotkey_ &&
           cursor_guard_.stopped();
  }

  void Wake() override {
    if (wake_event_) SetEvent(wake_event_);
  }

  void WaitForEvents(unsigned timeout_ms) override {
    const DWORD result = MsgWaitForMultipleObjectsEx(
        wake_event_ ? 1 : 0, wake_event_ ? &wake_event_ : nullptr, timeout_ms,
        QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (result == WAIT_FAILED)
      health_error_ = Error("MsgWaitForMultipleObjectsEx privacy thread");
  }

  PrivacyHealth Poll() override {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT)
        health_error_ =
            "Privacy message thread is exiting";
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (emergency_) {
      emergency_ = false;
      return {{}, true};
    }
    if (block_input_) {
      std::string error;
      if (!input_guard_.Healthy(error)) return {error, false};
    }
    if (band_guard_.stopped()) return {health_error_, false};
    // Input messages wake the thread immediately; expensive health queries
    // must retain their own cadence instead of running once per input event.
    // Window events only dirty the coverage check, which is cheap enough to
    // run between the throttled full health passes.
    if (!layout_changed_ && GetTickCount64() - last_check_ < 100) {
      if (coverage_dirty_ && last_health_.failure.empty() &&
          !topology_pending_) {
        coverage_dirty_ = false;
        last_health_ = CheckCoverage();
      }
      return last_health_;
    }
    coverage_dirty_ = false;
    last_check_ = GetTickCount64();
    last_health_ = CheckHealth();
    return last_health_;
  }

 private:
  // The broker reconciles hotplug asynchronously; while it does, coverage
  // cannot be verified. Preserve intent for a bounded time only: a broker that
  // never settles must not keep privacy "on" without any verification.
  static constexpr uint64_t kTopologyPendingLimitMs = 3000;

  PrivacyHealth TopologyPending() {
    const uint64_t now = GetTickCount64();
    if (topology_pending_since_ == 0) topology_pending_since_ = now;
    if (now - topology_pending_since_ >= kTopologyPendingLimitMs) {
      return {"Privacy cover topology did not settle after a display change",
              false};
    }
    return {{}, false, false};
  }

  PrivacyHealth CheckHealth() {
    if (!health_error_.empty()) return {health_error_, false};
    if (!band_guard_.Healthy(health_error_)) return {health_error_, false};
    if (!IsWindowsPrivacyDesktopAvailable()) {
      std::string cursor_error;
      cursor_guard_.Stop(cursor_error);
      return {
          "Secure desktop, lock screen or unavailable input desktop; "
          "privacy screen will turn off",
          false};
    }
    std::string cursor_error;
    if (!cursor_guard_.Healthy(cursor_error)) return {cursor_error, false};
    QUERY_USER_NOTIFICATION_STATE shell_state{};
    if (FAILED(SHQueryUserNotificationState(&shell_state)) ||
        shell_state == QUNS_RUNNING_D3D_FULL_SCREEN) {
      return {
          "Exclusive fullscreen or unknown display presentation",
          false};
    }
    if (!ReconcileMonitors(health_error_)) return {health_error_, false};
    if (topology_pending_) return TopologyPending();
    BOOL composed = FALSE;
    if (FAILED(DwmIsCompositionEnabled(&composed)) || !composed) {
      return {
          "Desktop composition stopped; privacy coverage cannot be guaranteed",
          false};
    }
    auto coverage = CheckCoverage();
    if (!coverage.failure.empty()) {
      // The broker may replace an HWND after the snapshot above. Refresh once
      // before treating a stale geometry/handle as loss of privacy coverage.
      if (!ReconcileMonitors(health_error_)) return {health_error_, false};
      if (topology_pending_) return TopologyPending();
      coverage = CheckCoverage();
    }
    return coverage;
  }

  PrivacyHealth CheckCoverage() {
    const auto get_band = reinterpret_cast<GetPrivacyWindowBand>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetWindowBand"));
    if (!get_band) return {"High-band window API became unavailable", false};
    for (const auto& w : windows_) {
      DWORD band = 0;
      DWORD affinity = 0;
      BYTE alpha = 0;
      DWORD layered_flags = 0;
      RECT rect{};
      DWORD cloaked = 0;
      if (!get_band(w.cover, &band) || band != kPrivacyWindowBand ||
          !IsWindowVisible(w.cover) || !GetWindowRect(w.cover, &rect) ||
          !SameRect(rect, w.rect) ||
          !(GetWindowLongPtrW(w.cover, GWL_EXSTYLE) & WS_EX_TOPMOST) ||
          !GetLayeredWindowAttributes(w.cover, nullptr, &alpha,
                                      &layered_flags) ||
          alpha != 255 || layered_flags != LWA_ALPHA ||
          !GetWindowDisplayAffinity(w.cover, &affinity) ||
          affinity != kPrivacyWindowDisplayAffinity ||
          FAILED(DwmGetWindowAttribute(w.cover, DWMWA_CLOAKED, &cloaked,
                                       sizeof(cloaked))) ||
          cloaked) {
        return {
            "Privacy window visibility, geometry or capture affinity was lost",
            false};
      }
    }
    for (const auto& w : windows_) {
      // Detect windows in special bands above the cover. A high-band window
      // still cannot cover another desktop or guarantee exclusive fullscreen.
      for (HWND above = GetWindow(w.cover, GW_HWNDPREV); above;
           above = GetWindow(above, GW_HWNDPREV)) {
        if (!IsWindowVisible(above) || IsIconic(above)) continue;
        DWORD above_band = 0;
        if (!get_band(above, &above_band))
          return {"Could not inspect a window above the high-band cover",
                  false};
        // HWND list order alone does not express compositor band order.
        // Only skip the two known ordinary desktop bands, never unknown bands.
        if (above_band == 0 || above_band == 1) continue;
        if (std::any_of(windows_.begin(), windows_.end(),
                        [above](const auto& own) {
                          return above == own.cover;
                        }))
          continue;
        RECT bounds{}, intersection{};
        DWORD hidden = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(above, DWMWA_CLOAKED, &hidden,
                                            sizeof(hidden))) &&
            hidden)
          continue;
        if (GetWindowRect(above, &bounds) &&
            IntersectRect(&intersection, &bounds, &w.rect)) {
          if (IsNonObstructingPrivacyWindow(above, bounds)) continue;
          char window_class[128]{};
          DWORD process_id = 0;
          GetClassNameA(above, window_class, sizeof(window_class));
          GetWindowThreadProcessId(above, &process_id);
          const auto now = GetTickCount64();
          if (above != last_obstruction_ ||
              now - last_obstruction_log_ >= 1000) {
            LOG_WARN(
                "Potential privacy cover overlap: class={}, process={}, "
                "band={}, bounds=({}, {})-({}, {}), exstyle=0x{:x}",
                window_class, process_id, above_band, bounds.left, bounds.top,
                bounds.right, bounds.bottom,
                GetWindowLongPtrW(above, GWL_EXSTYLE));
            last_obstruction_ = above;
            last_obstruction_log_ = now;
          }
          // HWND order cannot prove compositor order, but this window may
          // display content. Report failure so optional privacy is released.
          return {
              "A visible window may overlap privacy coverage",
              false};
        }
      }
    }
    return {health_error_, false};
  }

  struct Window {
    RECT rect{};
    HWND cover = nullptr;
  };

  bool Initialize(std::string& error) {
    if (sink_) return true;
    // This dedicated thread owns no existing application windows. Never alter
    // process-wide or Slint/SDL DPI awareness.
    if (!dpi_scope_ || !dpi_scope_->active()) {
      dpi_scope_.reset();
      dpi_scope_ = std::make_unique<ScopedWindowsPhysicalCoordinates>();
    }
    if (!dpi_scope_->active()) {
      error = Error("SetThreadDpiAwarenessContext");
      return false;
    }
    WNDCLASSEXW cls{sizeof(cls)};
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = kClassName;
    cls.hCursor = nullptr;
    cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (!registered_ && !RegisterClassExW(&cls)) {
      error = Error("RegisterClassExW");
      return false;
    }
    registered_ = true;
    sink_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName,
                            L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                            cls.hInstance, this);
    if (!sink_) {
      error = Error("CreateWindowExW message sink");
      return false;
    }
    return true;
  }

  bool ReconcileMonitors(std::string& error) {
    const auto wait_for_broker = [&] {
      topology_pending_ = true;
      // Preserve intent while a healthy broker reconciles hotplug. Broker
      // failure/heartbeat checks still detect real faults independently.
      error.clear();
      return true;
    };
    MonitorEnumeration monitors;
    if (!EnumDisplayMonitors(nullptr, nullptr, EnumMonitor,
                             reinterpret_cast<LPARAM>(&monitors)) || !monitors.ok) {
      error = Error("EnumDisplayMonitors");
      return false;
    }
    std::vector<PrivacyBandWindow> covers;
    if (!band_guard_.Windows(covers, error)) {
      if (GetLastError() == ERROR_RETRY) return wait_for_broker();
      return false;
    }
    if (!MatchPrivacyTopology(monitors.rects, covers)) return wait_for_broker();

    std::vector<Window> updated;
    for (const auto& cover : covers) {
      HWND hwnd = reinterpret_cast<HWND>(cover.hwnd);
      if (std::none_of(windows_.begin(), windows_.end(), [&](const auto& old) {
            return old.cover == hwnd && SameRect(old.rect, cover.rect);
          })) {
        LOG_INFO("Privacy: reconciled local cover without changing session state: "
                 "bounds=({}, {})-({}, {}), affinity=0x11",
                 cover.rect.left, cover.rect.top, cover.rect.right, cover.rect.bottom);
      }
      updated.push_back({cover.rect, hwnd});
    }
    windows_ = std::move(updated);
    layout_changed_ = false;
    topology_pending_ = false;
    topology_pending_since_ = 0;
    return true;
  }

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp) {
    if (msg == WM_NCCREATE) {
      const auto create = reinterpret_cast<CREATESTRUCTW*>(lp);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto self = reinterpret_cast<WindowsPrivacyBackend*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_HOTKEY || msg == kEmergency) {
      if (self && hwnd == self->sink_) self->emergency_ = true;
      return 0;
    }
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg == WM_SETCURSOR) {
      SetCursor(nullptr);
      return TRUE;
    }
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_CLOSE) return 0;
    if ((msg == WM_DISPLAYCHANGE || msg == WM_DPICHANGED) && self) {
      self->layout_changed_ = true;
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }

  static void CALLBACK WindowEvent(HWINEVENTHOOK, DWORD, HWND hwnd, LONG object,
                                   LONG child, DWORD, DWORD) {
    auto self = event_owner_;
    if (!self || !hwnd || object != OBJID_WINDOW || child != CHILDID_SELF ||
        hwnd == self->sink_ || GetAncestor(hwnd, GA_ROOT) != hwnd)
      return;
    for (const auto& w : self->windows_)
      if (hwnd == w.cover) return;
    // OUTOFCONTEXT delivers on this window thread. Only mark work here;
    // monitor/session queries and Z-order changes remain in the control loop.
    const auto get_band = reinterpret_cast<GetPrivacyWindowBand>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetWindowBand"));
    DWORD band = 0;
    if (get_band && get_band(hwnd, &band) && (band == 0 || band == 1)) return;
    self->coverage_dirty_ = true;
  }

  inline static thread_local WindowsPrivacyBackend* event_owner_ = nullptr;
  std::vector<HWINEVENTHOOK> window_events_;
  std::vector<Window> windows_;
  HWND sink_ = nullptr;
  HANDLE wake_event_ = nullptr;
  std::string wake_error_;
  PrivacyInputGuard input_guard_;
  bool block_input_ = false;
  PrivacyCursorGuard cursor_guard_;
  PrivacyBandGuard band_guard_;
  std::unique_ptr<ScopedWindowsPhysicalCoordinates> dpi_scope_;
  bool registered_ = false;
  bool hotkey_ = false;
  bool emergency_ = false;
  bool layout_changed_ = false;
  bool topology_pending_ = false;
  uint64_t topology_pending_since_ = 0;
  bool coverage_dirty_ = false;
  uint64_t last_check_ = 0;
  PrivacyHealth last_health_;
  HWND last_obstruction_ = nullptr;
  uint64_t last_obstruction_log_ = 0;
  std::string health_error_;
};

}  // namespace

bool IsWindowsPrivacyDesktopAvailable() {
  // LockApp can display the lock screen while OpenInputDesktop still reports
  // Default. Desktop-name checks alone are NOT a session-unlocked check.
  DWORD session = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) ||
      session != WTSGetActiveConsoleSessionId())
    return false;
  LPWSTR buffer = nullptr;
  DWORD bytes = 0;
  if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session,
                                   WTSSessionInfoEx, &buffer, &bytes))
    return false;
  const auto info = reinterpret_cast<WTSINFOEXW*>(buffer);
  const bool unlocked =
      bytes >= sizeof(WTSINFOEXW) && info->Level == 1 &&
      info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_UNLOCK;
  WTSFreeMemory(buffer);
  if (!unlocked) return false;
  HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  if (!input) return false;
  wchar_t input_name[256]{}, thread_name[256]{};
  const bool ok =
      DesktopName(input, input_name) &&
      DesktopName(GetThreadDesktop(GetCurrentThreadId()), thread_name) &&
      _wcsicmp(input_name, L"Default") == 0 &&
      _wcsicmp(input_name, thread_name) == 0;
  CloseDesktop(input);
  return ok;
}

std::unique_ptr<PrivacyBackend> CreateWindowsPrivacyBackend() {
  return std::make_unique<WindowsPrivacyBackend>();
}

}  // namespace crossdesk
