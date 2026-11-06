// Run the real capture thread and callback against a scripted Windows API.
// These tests never install a system hook or inject desktop input.
#include <Windows.h>

#include <array>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <string>
#include <vector>

#include "keyboard_state.h"

namespace mock {

const HWND stream = reinterpret_cast<HWND>(1);
const HWND local = reinterpret_cast<HWND>(2);
HWND foreground = stream;
HOOKPROC callback = nullptr;
std::array<bool, 256> windows_down{};
bool raw_registered = false;
bool old_hook_expired = false;
bool leave_during_install = false;
bool ok = true;
std::string scenario;
std::vector<crossdesk::KeyboardInput> forwarded;
std::vector<std::function<void(MSG&)>> steps;
size_t next_step = 0;
std::promise<void> finished;

void Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL " << scenario << ": " << message << '\n';
    ok = false;
  }
}

HWND Foreground() { return foreground; }
DWORD WindowProcess(HWND, LPDWORD process) {
  *process = GetCurrentProcessId();
  return GetCurrentThreadId();
}
SHORT AsyncKey(int code) { return windows_down[code] ? SHORT(0x8000) : 0; }
HHOOK Install(int, HOOKPROC proc, HINSTANCE, DWORD) {
  callback = proc;
  if (leave_during_install) {
    foreground = local;
    leave_during_install = false;
  }
  return reinterpret_cast<HHOOK>(3);
}
BOOL Remove(HHOOK) {
  if (old_hook_expired) {
    old_hook_expired = false;
    SetLastError(ERROR_INVALID_HOOK_HANDLE);
    return FALSE;
  }
  return TRUE;
}
LRESULT NextHook(HHOOK, int, WPARAM, LPARAM) { return 0; }
UINT Registrations(PRAWINPUTDEVICE devices, PUINT count, UINT) {
  if (!devices) {
    *count = raw_registered ? 1 : 0;
    return 0;
  }
  if (!raw_registered) return 0;
  devices[0] = {1, 6, 0, nullptr};
  return 1;
}
BOOL Register(PCRAWINPUTDEVICE devices, UINT, UINT) {
  raw_registered = (devices[0].dwFlags & RIDEV_REMOVE) == 0;
  return TRUE;
}
BOOL Peek(LPMSG, HWND, UINT, UINT, UINT) { return FALSE; }
UINT_PTR Timer(HWND, UINT_PTR, UINT, TIMERPROC) { return 1; }
BOOL KillTimer(HWND, UINT_PTR) { return TRUE; }
BOOL Post(DWORD, UINT, WPARAM, LPARAM) { return TRUE; }
BOOL GetMessage(LPMSG message, HWND, UINT, UINT) {
  if (next_step == steps.size()) {
    finished.set_value();
    return 0;
  }
  *message = {};
  steps[next_step++](*message);
  return TRUE;
}
void Forward(int key, bool down, uint32_t scan, bool extended, void*) {
  forwarded.push_back({key, down, scan, extended});
}

void Event(int key, bool down, HWND window, bool should_swallow,
           bool generic_modifier = false) {
  foreground = window;
  const UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
  DWORD vk = key;
  if (generic_modifier) {
    if (key == VK_LCONTROL || key == VK_RCONTROL) vk = VK_CONTROL;
    if (key == VK_LSHIFT || key == VK_RSHIFT) vk = VK_SHIFT;
    if (key == VK_LMENU || key == VK_RMENU) vk = VK_MENU;
  }
  const KBDLLHOOKSTRUCT event{vk, scan & 0xFF,
                            (scan & 0xFF00) ? LLKHF_EXTENDED : 0u, 0, 0};
  const bool swallowed = callback(HC_ACTION, down ? WM_KEYDOWN : WM_KEYUP,
                                  reinterpret_cast<LPARAM>(&event)) != 0;
  // The hook decides whether Windows receives this edge.
  if (!swallowed) windows_down[key] = down;
  Expect(swallowed == should_swallow,
         down ? "unexpected key-down routing" : "unexpected key-up routing");
}

void Reset(int key, const std::string& name) {
  foreground = stream;
  callback = nullptr;
  windows_down.fill(false);
  raw_registered = old_hook_expired = leave_during_install = false;
  scenario = name + " key=" + std::to_string(key);
  forwarded.clear();
  steps.clear();
  next_step = 0;
  finished = std::promise<void>();
}

}  // namespace mock

// Windows declarations are already loaded. Redirect only the capture code's
// OS boundary, including the inline raw-input guard, to the scripted desktop.
#define GetForegroundWindow mock::Foreground
#define GetWindowThreadProcessId mock::WindowProcess
#define GetAsyncKeyState mock::AsyncKey
#define SetWindowsHookExW mock::Install
#define UnhookWindowsHookEx mock::Remove
#define CallNextHookEx mock::NextHook
#define GetRegisteredRawInputDevices mock::Registrations
#define RegisterRawInputDevices mock::Register
#define PeekMessageW mock::Peek
#define SetTimer mock::Timer
#define KillTimer mock::KillTimer
#define PostThreadMessageW mock::Post
#define GetMessageW mock::GetMessage

// Logging is not part of the routing contract and needs no global logger here.
#define _RD_LOG_H_
#define LOG_INFO(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#include "../src/platform/windows/input/keyboard/keyboard_capturer.cpp"

namespace crossdesk {
UINT SendInputOnUserDesktop(const INPUT&) { std::abort(); }
UINT SendInputOnUserDesktop(INPUT*, UINT) { std::abort(); }
}  // namespace crossdesk

namespace {

void Run(int key) {
  crossdesk::PlatformKeyboardCapturer capturer;
  auto finished = mock::finished.get_future();
  const int result = capturer.Hook(&mock::Forward, nullptr);
  mock::Expect(result == 0, "capture must start");
  if (result != 0) return;
  finished.wait();
  capturer.Unhook();
  mock::Expect(!mock::windows_down[key], "Windows modifier must be released");
  mock::Expect(!mock::forwarded.empty() && !mock::forwarded.back().is_down,
               "remote modifier must be released");
}

}  // namespace

int main() {
  int scenarios = 0;
  for (const int key : {VK_LCONTROL, VK_RCONTROL, VK_LSHIFT, VK_RSHIFT,
                        VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN}) {
    mock::Reset(key, "captured press and release");
    mock::steps.push_back([=](MSG&) {
      mock::Event(key, true, mock::stream, true);
      mock::Event(key, false, mock::stream, true);
    });
    Run(key);
    ++scenarios;

    for (bool leave : {false, true}) {
      mock::Reset(key, leave ? "focus changes during startup" : "inherited press");
      mock::windows_down[key] = true;
      mock::leave_during_install = leave;
      mock::steps.push_back([=](MSG&) {
        if (leave) mock::Expect(mock::forwarded.empty(),
                               "startup must not forward background modifiers");
        mock::Event(key, false, mock::stream, false, true);
      });
      Run(key);
      ++scenarios;
    }

    for (bool background : {false, true}) {
      // 0: normal renewal, 1: raw input resumed, 2: hook silently expired.
      for (int recovery = 0; recovery < 3; ++recovery) {
        mock::Reset(key, std::string(background ? "background" : "foreground") +
                            " renewal=" + std::to_string(recovery));
        if (!background) mock::windows_down[key] = true;
        mock::steps.push_back([=](MSG& message) {
          if (background) mock::Event(key, true, mock::local, false);
          mock::raw_registered = recovery == 1;
          mock::old_hook_expired = recovery == 2;
          message.message = WM_TIMER;
          message.wParam = 1;
        });
        mock::steps.push_back([=](MSG&) {
          if (background) mock::Expect(mock::forwarded.empty(),
                                       "recovery must not forward background modifiers");
          mock::Event(key, false, mock::stream, false, true);
        });
        Run(key);
        ++scenarios;
      }
    }

    mock::Reset(key, "press missed while hook was unavailable");
    mock::steps.push_back([=](MSG& message) {
      mock::foreground = mock::local;
      mock::windows_down[key] = true;
      mock::old_hook_expired = true;
      message.message = WM_TIMER;
      message.wParam = 1;
    });
    mock::steps.push_back([=](MSG&) {
      mock::Expect(mock::forwarded.empty(),
                   "recovered background press must not be forwarded yet");
      mock::Event(key, false, mock::stream, false);
    });
    Run(key);
    ++scenarios;
  }
  std::cout << scenarios << " keyboard capture scenarios: "
            << (mock::ok ? "PASS" : "FAIL") << '\n';
  return mock::ok ? 0 : 1;
}
