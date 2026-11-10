#include "session_end_observer.h"

#include "rd_log.h"

namespace crossdesk {
namespace {
constexpr wchar_t kSessionWindowClass[] = L"CrossDesk.SessionEndObserver";
}

SessionEndObserver::~SessionEndObserver() {
  if (window_) DestroyWindow(window_);
}

bool SessionEndObserver::ReportError(const char* operation) {
  const DWORD error = GetLastError();
  const ULONGLONG now = GetTickCount64();
  if (last_error_tick_ == 0 || now - last_error_tick_ >= 5000) {
    LOG_WARN("Windows session end observer {} failed, error={}", operation, error);
    last_error_tick_ = now;
  }
  SetLastError(error);
  return false;
}

bool SessionEndObserver::Start() {
  if (window_) return true;
  WNDCLASSW klass{};
  klass.lpfnWndProc = WindowProc;
  klass.hInstance = GetModuleHandleW(nullptr);
  klass.lpszClassName = kSessionWindowClass;
  if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return ReportError("register window");
  // A hidden top-level window receives session-end broadcasts even when the
  // Slint windows are in the tray. HWND_MESSAGE does not receive them.
  window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kSessionWindowClass, L"CrossDesk",
      WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, klass.hInstance, this);
  if (!window_) return ReportError("create window");
  return true;
}

LRESULT CALLBACK SessionEndObserver::WindowProc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    auto* observer = static_cast<SessionEndObserver*>(create->lpCreateParams);
    observer->window_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(observer));
  }
  auto* observer = reinterpret_cast<SessionEndObserver*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (observer && message == WM_QUERYENDSESSION) {
    // Never stop work or veto shutdown here, including logoff/Restart Manager.
    LOG_INFO("Windows shutdown query accepted: flags=0x{:x}",
             static_cast<DWORD>(lparam));
    return TRUE;
  }
  if (observer && message == WM_ENDSESSION) {
    LOG_INFO("Windows session end: confirmed={}, flags=0x{:x}",
             wparam != FALSE, static_cast<DWORD>(lparam));
    // Let the GUI timer begin its normal cleanup only after confirmation.
    // Do not wait for network/device threads inside a Windows message handler.
    if (wparam != FALSE) observer->ending_ = true;
    return 0;
  }
  if (observer && message == WM_NCDESTROY) {
    observer->window_ = nullptr;
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace crossdesk
