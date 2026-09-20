/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-10
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#include <Windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <vector>

#include "../../windows_thread_dpi.h"
#include "../privacy_band_ipc.h"
#include "../privacy_capture_exclusion.h"
#include "../privacy_topology.h"
#include "privacy_cover_renderer.h"

namespace {
using namespace crossdesk;
constexpr wchar_t kWindowClass[] = L"CrossDesk.HighBandPrivacy";
constexpr DWORD kCoverStyles = WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                               WS_EX_TOOLWINDOW | WS_EX_LAYERED |
                               WS_EX_TRANSPARENT;
HMODULE module = nullptr;

class Windows {
 public:
  explicit Windows(PrivacyBandShared* shared) : shared_(shared) {}
  ~Windows() {
    for (const auto& window : windows_)
      DestroyWindow(reinterpret_cast<HWND>(window.hwnd));
    if (atom_) UnregisterClassW(kWindowClass, module);
  }
  bool Fail(const wchar_t* operation, DWORD code = GetLastError()) {
    wcsncpy_s(shared_->operation, operation, _TRUNCATE);
    InterlockedExchange(&shared_->error, code ? code : ERROR_GEN_FAILURE);
    return false;
  }
  bool Initialize() {
    const HMODULE user = GetModuleHandleW(L"user32.dll");
    create_ = reinterpret_cast<CreatePrivacyWindowInBand>(
        GetProcAddress(user, "CreateWindowInBand"));
    get_band_ = reinterpret_cast<GetPrivacyWindowBand>(
        GetProcAddress(user, "GetWindowBand"));
    if (!create_ || !get_band_)
      return Fail(L"Resolve high-band APIs", ERROR_PROC_NOT_FOUND);
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = module;
    cls.lpfnWndProc = WindowProc;
    cls.lpszClassName = kWindowClass;
    cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    atom_ = RegisterClassExW(&cls);
    return atom_ ? Reconcile() : Fail(L"Register high-band window class");
  }
  bool Reconcile() {
    InterlockedExchange(&shared_->topology_pending, 1);
    std::vector<RECT> monitors;
    if (!EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR, HDC, LPRECT rect, LPARAM data) -> BOOL {
              auto& result = *reinterpret_cast<std::vector<RECT>*>(data);
              if (result.size() >= kPrivacyMaxMonitors) return FALSE;
              result.push_back(*rect);
              return TRUE;
            },
            reinterpret_cast<LPARAM>(&monitors)))
      return Fail(L"Enumerate high-band displays");
    const bool changed = monitors.size() != windows_.size() ||
        std::any_of(monitors.begin(), monitors.end(), [&](const RECT& rect) {
          return std::none_of(windows_.begin(), windows_.end(),
                              [&](const auto& window) { return SameRect(rect, window.rect); });
        });
    // Mark the shared snapshot busy BEFORE replacing/destroying any HWND.
    // Readers must not validate a stale handle from a previous topology.
    struct PublishGuard {
      PrivacyBandShared* shared;
      ~PublishGuard() { InterlockedIncrement(&shared->sequence); }
    } publishing{shared_};
    InterlockedIncrement(&shared_->sequence);
    for (const auto& rect : monitors) {
      const auto found =
          std::find_if(windows_.begin(), windows_.end(),
                       [&](const auto& w) { return SameRect(w.rect, rect); });
      if (found != windows_.end()) {
        // Windows can reposition a top-level window during DPI/hotplug before
        // delivering WM_DISPLAYCHANGE. Restore the intended monitor bounds.
        HWND hwnd = reinterpret_cast<HWND>(found->hwnd);
        RECT actual{};
        if ((!GetWindowRect(hwnd, &actual) || !SameRect(actual, rect) ||
             !IsWindowVisible(hwnd)) &&
            !SetWindowPos(hwnd, nullptr, rect.left, rect.top,
                          rect.right - rect.left, rect.bottom - rect.top,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW))
          return Fail(L"Reposition high-band cover");
        continue;
      }
      HWND hwnd = create_(kCoverStyles, atom_, L"CrossDesk Privacy", WS_POPUP,
                          rect.left, rect.top, rect.right - rect.left,
                          rect.bottom - rect.top, nullptr, nullptr, module,
                          this, kPrivacyWindowBand);
      if (!hwnd) return Fail(L"CreateWindowInBand ZBID_ABOVELOCK_UX");
      windows_.push_back({reinterpret_cast<uint64_t>(hwnd), rect});
      if (!SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA))
        return Fail(L"Make high-band cover opaque");
      if (!ExcludePrivacyWindowFromCapture(hwnd))
        return Fail(L"Exclude high-band cover from capture");
      const BOOL yes = TRUE;
      const HRESULT peek = DwmSetWindowAttribute(hwnd, DWMWA_EXCLUDED_FROM_PEEK,
                                                 &yes, sizeof(yes));
      if (FAILED(peek)) return Fail(L"Exclude high-band cover from Peek", peek);
      if (!SetWindowPos(hwnd, nullptr, rect.left, rect.top,
                        rect.right - rect.left, rect.bottom - rect.top,
                        SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW) ||
          !RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW))
        return Fail(L"Present high-band cover");
    }
    if (changed) {
      const HRESULT result = DwmFlush();
      if (FAILED(result))
        return Fail(L"Flush high-band cover presentation", result);
      // New areas are covered before old windows are removed.
      for (auto it = windows_.begin(); it != windows_.end();) {
        if (std::none_of(
                monitors.begin(), monitors.end(),
                [&](const auto& rect) { return SameRect(rect, it->rect); })) {
          if (!DestroyWindow(reinterpret_cast<HWND>(it->hwnd)))
            return Fail(L"Destroy obsolete high-band cover");
          it = windows_.erase(it);
        } else
          ++it;
      }
    }
    if (!Validate()) {
      // Another topology event can race enumeration/presentation. Retry while
      // retaining every surviving cover; actual validation errors stay fatal.
      return InterlockedCompareExchange(&shared_->error, 0, 0) == ERROR_SUCCESS;
    }
    // A previous reconciliation may have changed windows_ but failed geometry
    // validation. Publish on every success so retries cannot leave stale HWNDs.
    shared_->count = static_cast<uint32_t>(windows_.size());
    std::copy(windows_.begin(), windows_.end(), shared_->windows);
    InterlockedExchange(&shared_->topology_pending, 0);
    return true;
  }
  bool Validate() {
    if (paint_failed_) return false;
    for (const auto& w : windows_) {
      HWND hwnd = reinterpret_cast<HWND>(w.hwnd);
      DWORD band = 0, affinity = 0, flags = 0, cloaked = 0;
      BYTE alpha = 0;
      RECT rect{};
      if (!GetWindowRect(hwnd, &rect) || !SameRect(rect, w.rect)) {
        changed = true;
        return false;
      }
      if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & kCoverStyles) !=
              kCoverStyles ||
          !get_band_(hwnd, &band) || band != kPrivacyWindowBand ||
          !GetWindowDisplayAffinity(hwnd, &affinity) ||
          affinity != kPrivacyWindowDisplayAffinity ||
          !GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags) ||
          alpha != 255 || flags != LWA_ALPHA || !IsWindowVisible(hwnd) ||
          FAILED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked,
                                       sizeof(cloaked))) ||
          cloaked)
        return Fail(L"High-band cover lost style/band/affinity/visibility",
                    ERROR_INVALID_STATE);
    }
    return true;
  }
  bool changed = false;

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wp,
                                     LPARAM lp) {
    if (message == WM_NCCREATE) {
      auto create = reinterpret_cast<CREATESTRUCTW*>(lp);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto self =
        reinterpret_cast<Windows*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_SETCURSOR) {
      SetCursor(nullptr);
      return TRUE;
    }
    if (message == WM_CLOSE) return 0;
    if ((message == WM_DISPLAYCHANGE || message == WM_DPICHANGED) && self) {
      self->changed = true;
      InvalidateRect(hwnd, nullptr, TRUE);
      return 0;
    }
    if (message == WM_PAINT) {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(hwnd, &paint);
      RECT rect{};
      GetClientRect(hwnd, &rect);
      if (self && !DrawPrivacyCover(dc, rect, PrivacyWindowDpi(hwnd), module,
                                    self->shared_->unlock_hint)) {
        self->paint_failed_ = true;
        self->Fail(L"Paint privacy logo and unlock instructions");
      }
      EndPaint(hwnd, &paint);
      return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
  }
  PrivacyBandShared* shared_;
  bool paint_failed_ = false;
  std::vector<PrivacyBandWindow> windows_;
  ATOM atom_ = 0;
  CreatePrivacyWindowInBand create_ = nullptr;
  GetPrivacyWindowBand get_band_ = nullptr;
};

DWORD Run() {
  wchar_t bootstrap[256]{};
  if (!GetEnvironmentVariableW(kPrivacyBandBootstrap, bootstrap, 256))
    return ERROR_INVALID_PARAMETER;
  SetEnvironmentVariableW(kPrivacyBandBootstrap, nullptr);
  std::array<HANDLE, 4> handles{};
  wchar_t* item = bootstrap;
  for (size_t i = 0; i < handles.size(); ++i) {
    wchar_t* end = nullptr;
    const auto value = wcstoull(item, &end, 10);
    if (!value || end == item ||
        (i + 1 == handles.size() ? *end != 0 : *end != L','))
      return ERROR_INVALID_PARAMETER;
    handles[i] = reinterpret_cast<HANDLE>(value);
    item = end + 1;
  }
  HANDLE parent = handles[0], stop = handles[1], ready = handles[2],
         mapping = handles[3];
  auto shared = static_cast<PrivacyBandShared*>(MapViewOfFile(
      mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(PrivacyBandShared)));
  if (!shared) return GetLastError();
  if (shared->magic != kPrivacyBandMagic ||
      shared->version != kPrivacyBandVersion ||
      shared->size != sizeof(PrivacyBandShared) ||
      shared->broker_pid != GetCurrentProcessId() ||
      shared->parent_pid != GetProcessId(parent) ||
      WaitForSingleObject(parent, 0) != WAIT_TIMEOUT) {
    UnmapViewOfFile(shared);
    return ERROR_INVALID_DATA;
  }
  DWORD exit_code = ERROR_SUCCESS;
  {
    ScopedWindowsPhysicalCoordinates dpi;
    Windows windows(shared);
    try {
      bool healthy = dpi.active() && windows.Initialize();
      const bool initialized = healthy;
      if (!dpi.active()) windows.Fail(L"Set high-band thread DPI context");
      InterlockedExchange64(&shared->heartbeat, GetTickCount64());
      if (healthy) {
        InterlockedExchange(&shared->error, ERROR_SUCCESS);
        InterlockedExchange(&shared->active, 1);
      }
      SetEvent(ready);
      auto last_check = GetTickCount64();
      HANDLE watched[] = {parent, stop};
      // Once enabled, retain surviving covers on a validation/layout fault.
      // The parent releases privacy after reading the latched error. Only an
      // explicit stop or parent death tears the remaining windows down.
      while (initialized) {
        const DWORD wait = MsgWaitForMultipleObjectsEx(
            2, watched, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_OBJECT_0 + 1) break;
        if (wait == WAIT_FAILED) {
          windows.Fail(L"Wait for high-band lifecycle");
          break;
        }
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
          if (message.message == WM_QUIT) healthy = false;
          TranslateMessage(&message);
          DispatchMessageW(&message);
        }
        const auto now = GetTickCount64();
        if (healthy && (windows.changed || now - last_check >= 250)) {
          windows.changed = false;
          last_check = now;
          healthy = windows.Reconcile();
        }
        InterlockedExchange64(&shared->heartbeat, now);
      }
    } catch (...) {
      windows.Fail(L"High-band window thread exception",
                   ERROR_UNHANDLED_EXCEPTION);
      SetEvent(ready);
    }
    exit_code = InterlockedCompareExchange(&shared->error, 0, 0);
    InterlockedExchange(&shared->active, 0);
  }
  UnmapViewOfFile(shared);
  for (HANDLE handle : handles) CloseHandle(handle);
  return exit_code;
}

void CALLBACK RunBeforeBrokerEntryPoint(ULONG_PTR) {
  // The LoadLibraryW startup APC has returned, so the loader lock is no longer
  // held. Own the primary thread until privacy stops, including startup errors.
  // Never return into RuntimeBroker's COM server startup: an elevated launch
  // can fail there with CO_E_WRONG_SERVER_IDENTITY and kill healthy covers.
  ExitProcess(Run());
}
}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    module = instance;
    // Loaded by the startup APC on our dedicated, suspended broker's primary
    // thread. Queue the window loop after that APC instead of starting a worker
    // alongside RuntimeBroker's own entry point. Do not run it under loader lock.
    if (!QueueUserAPC(RunBeforeBrokerEntryPoint, GetCurrentThread(), 0))
      return FALSE;
  }
  return TRUE;
}
