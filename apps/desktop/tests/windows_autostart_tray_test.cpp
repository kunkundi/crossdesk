#include <Windows.h>

#include <chrono>
#include <iostream>
#include <vector>

#include "login_launch_policy.h"
#include "win_tray_icon.h"

namespace {

using crossdesk::LoginLaunchTrayPolicy;
using crossdesk::WinTrayIconHost;
using Action = LoginLaunchTrayPolicy::Action;

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

bool TestLoginLaunchPolicy() {
  using namespace std::chrono_literals;
  const LoginLaunchTrayPolicy::TimePoint t0{};
  bool ok = true;

  {
    LoginLaunchTrayPolicy policy(30s, 1s);
    ok &= Expect(policy.Begin(false, true, false, t0) == Action::kShowMainWindow,
                 "interactive launch shows main window immediately");
    ok &= Expect(!policy.pending(),
                 "interactive launch does not arm tray retries");
  }

  {
    LoginLaunchTrayPolicy policy(30s, 1s);
    ok &= Expect(policy.Begin(true, false, false, t0) == Action::kShowMainWindow,
                 "login launch without tray support shows main window");
    ok &= Expect(!policy.pending(),
                 "unsupported tray platform does not arm retries");
  }

  {
    LoginLaunchTrayPolicy policy(30s, 1s);
    ok &= Expect(policy.Begin(true, true, true, t0) == Action::kStayHidden,
                 "login launch stays hidden when initial tray registration succeeds");
    ok &= Expect(!policy.pending(),
                 "completed initial tray registration needs no retry");
  }

  {
    // Reproduces Windows boot/login when Explorer's Shell_TrayWnd is not ready
    // yet: main window must remain hidden while retrying instead of popping up.
    LoginLaunchTrayPolicy policy(30s, 1s);
    ok &= Expect(policy.Begin(true, true, false, t0) == Action::kStayHidden,
                 "login launch stays hidden while Explorer tray is starting");
    ok &= Expect(policy.pending(), "login launch arms bounded tray retry");
    ok &= Expect(policy.Poll(t0 + 500ms, false) == Action::kStayHidden,
                 "poll waits until retry interval when tray is still unready");
    ok &= Expect(policy.Poll(t0 + 500ms, true) == Action::kRetryMinimizeToTray,
                 "TaskbarCreated readiness completes immediately without waiting");
    ok &= Expect(policy.CompleteRetry(true, t0 + 500ms) == Action::kStayHidden,
                 "successful delayed tray registration keeps window hidden");
    ok &= Expect(!policy.pending(),
                 "pending state clears once tray registration succeeds");
  }

  {
    LoginLaunchTrayPolicy policy(3s, 1s);
    ok &= Expect(policy.Begin(true, true, false, t0) == Action::kStayHidden,
                 "grace window starts hidden");
    ok &= Expect(policy.Poll(t0 + 1s, false) == Action::kRetryMinimizeToTray,
                 "first periodic retry fires at 1s");
    ok &= Expect(policy.CompleteRetry(false, t0 + 1s) == Action::kStayHidden,
                 "failed retry before deadline keeps window hidden");
    ok &= Expect(policy.Poll(t0 + 1500ms, false) == Action::kStayHidden,
                 "second retry waits for next interval");
    ok &= Expect(policy.Poll(t0 + 3s, false) == Action::kRetryMinimizeToTray,
                 "final retry fires at grace deadline");
    ok &= Expect(
        policy.CompleteRetry(false, t0 + 3s) == Action::kShowMainWindow,
        "expired grace period falls back to showing main window when no tray exists");
    ok &= Expect(!policy.pending(), "expired grace clears pending state");
  }

  {
    LoginLaunchTrayPolicy policy(30s, 1s);
    ok &= Expect(policy.Begin(true, true, false, t0) == Action::kStayHidden,
                 "pending before user activation");
    policy.Cancel();
    ok &= Expect(!policy.pending(), "explicit window open cancels tray retries");
    ok &= Expect(policy.Poll(t0 + 5s, true) == Action::kStayHidden,
                 "cancelled policy never re-hides an explicitly opened window");
  }

  return ok;
}

bool TestWinTrayIconHostBootAndRestartRecovery() {
  bool ok = true;
  ULONGLONG fake_tick = 1000;
  bool shell_tray_ready = false;
  std::vector<DWORD> notify_calls;
  UINT callback_messages = 0;

  WinTrayIconHost host(
      nullptr, L"CrossDesk",
      [&](MSG* msg) {
        if (msg && msg->message == WM_TRAY_CALLBACK) {
          ++callback_messages;
        }
      },
      [&](DWORD message, NOTIFYICONDATAW* data) -> BOOL {
        notify_calls.push_back(message);
        if (!data || data->cbSize != sizeof(NOTIFYICONDATAW) || !data->hWnd) {
          return FALSE;
        }
        if (message == NIM_DELETE) {
          return TRUE;
        }
        return shell_tray_ready ? TRUE : FALSE;
      },
      [&]() { return fake_tick; });

  const HWND hwnd = host.hwnd();
  ok &= Expect(hwnd != nullptr && IsWindow(hwnd),
               "tray host creates a valid native window");
  ok &= Expect(!IsWindowVisible(hwnd), "tray host window stays invisible");
  ok &= Expect(
      (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0,
      "tray host window uses WS_EX_TOOLWINDOW");
  ok &= Expect(
      FindWindowExW(HWND_MESSAGE, nullptr, WinTrayIconHost::kWindowClassName,
                    nullptr) != hwnd,
      "tray host is not an HWND_MESSAGE window so it receives broadcasts");
  ok &= Expect(!host.IsReady(),
               "tray icon starts unready when Explorer tray is unavailable");
  ok &= Expect(notify_calls.size() == 1 && notify_calls[0] == NIM_ADD,
               "constructor attempts initial NIM_ADD");

  // Throttled ProcessEvents() must not spam Shell_NotifyIconW within 1000ms.
  fake_tick = 1500;
  host.ProcessEvents();
  ok &= Expect(notify_calls.size() == 1,
               "ProcessEvents throttles retries within 1s");

  // Simulate Explorer finishing startup and broadcasting TaskbarCreated.
  shell_tray_ready = true;
  ok &= Expect(host.taskbar_created_message() != 0,
               "TaskbarCreated message is registered");
  SendMessageW(hwnd, host.taskbar_created_message(), 0, 0);
  ok &= Expect(host.IsReady(),
               "TaskbarCreated immediately registers the tray icon");
  ok &= Expect(notify_calls.size() == 2 && notify_calls[1] == NIM_ADD,
               "TaskbarCreated triggers fresh NIM_ADD");

  // Once ready, ProcessEvents() is a zero-call fast path.
  fake_tick = 5000;
  host.ProcessEvents();
  ok &= Expect(notify_calls.size() == 2,
               "ProcessEvents does not call Shell_NotifyIconW while ready");

  // Simulate Explorer crashing and restarting mid-session: TaskbarCreated must
  // force a fresh NIM_ADD even though tray_icon_added_ was previously true.
  SendMessageW(hwnd, host.taskbar_created_message(), 0, 0);
  ok &= Expect(host.IsReady(), "tray icon remains ready after Explorer restart");
  ok &= Expect(notify_calls.size() == 3 && notify_calls[2] == NIM_ADD,
               "Explorer restart re-adds tray icon via NIM_ADD");

  // Simulate boot-storm timeout where NIM_ADD fails even during TaskbarCreated,
  // then recovers on the next 1s ProcessEvents() retry.
  shell_tray_ready = false;
  fake_tick = 6000;
  SendMessageW(hwnd, host.taskbar_created_message(), 0, 0);
  ok &= Expect(!host.IsReady(),
               "failed TaskbarCreated NIM_ADD marks tray unready for retry");
  shell_tray_ready = true;
  fake_tick = 6500;
  host.ProcessEvents();
  ok &= Expect(!host.IsReady(), "retry waits full 1s after failed NIM_ADD");
  fake_tick = 7000;
  host.ProcessEvents();
  ok &= Expect(host.IsReady(),
               "1s periodic retry recovers after boot-storm NIM_ADD timeout");

  // Verify WM_TRAY_CALLBACK routing to the owner callback.
  SendMessageW(hwnd, WM_TRAY_CALLBACK, 1, WM_LBUTTONUP);
  ok &= Expect(callback_messages == 1,
               "WM_TRAY_CALLBACK dispatches to message handler");

  host.RemoveTrayIcon();
  ok &= Expect(!host.IsReady() && notify_calls.back() == NIM_DELETE,
               "RemoveTrayIcon deletes registered icon cleanly");
  return ok;
}

}  // namespace

int main() {
  bool ok = true;
  ok &= TestLoginLaunchPolicy();
  ok &= TestWinTrayIconHostBootAndRestartRecovery();
  if (ok) {
    std::cout << "Windows autostart tray tests passed\n";
    return 0;
  }
  return 1;
}
