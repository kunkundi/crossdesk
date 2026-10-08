#include "keyboard_capturer.h"

#include "rd_log.h"
#include "windows_input_injector.h"
#include "windows_input_marker.h"
#include "windows_key_metadata.h"

namespace crossdesk {
namespace {

constexpr UINT kHookRenewIntervalMs = 500;
constexpr ULONGLONG kHookStallTimeoutMs = 2000;

bool PreferSideSpecificVkInjection(int key_code) {
  switch (key_code) {
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
      return true;
    default:
      return false;
  }
}

}  // namespace

thread_local PlatformKeyboardCapturer*
    PlatformKeyboardCapturer::active_capturer_ = nullptr;

PlatformKeyboardCapturer::~PlatformKeyboardCapturer() { Unhook(); }

bool PlatformKeyboardCapturer::IsHookActive() const {
  if (!hook_active_.load()) return false;
  const auto last_pump_tick = last_hook_pump_tick_.load();
  return GetTickCount64() - last_pump_tick < kHookStallTimeoutMs;
}

int PlatformKeyboardCapturer::Hook(OnKeyAction on_key_action, void* user_ptr) {
  if (capture_thread_.joinable()) {
    return IsHookActive() ? 0 : -1;
  }

  // Hook() is called only while the stream has focus. Pin capture to that
  // window so a focus change cannot steal input before the UI stops capture.
  const HWND foreground_window = GetForegroundWindow();
  DWORD process_id = 0;
  GetWindowThreadProcessId(foreground_window, &process_id);
  if (!on_key_action || !foreground_window ||
      process_id != GetCurrentProcessId()) {
    return -1;
  }
  capture_window_ = foreground_window;
  on_key_action_ = on_key_action;
  user_ptr_ = user_ptr;
  {
    std::lock_guard<std::mutex> lock(capture_state_mutex_);
    capture_thread_id_ = 0;
    capture_start_complete_ = false;
    capture_start_succeeded_ = false;
  }

  capture_thread_ = std::thread(&PlatformKeyboardCapturer::CaptureThreadMain, this);

  std::unique_lock<std::mutex> lock(capture_state_mutex_);
  capture_start_condition_.wait(
      lock, [this] { return capture_start_complete_; });
  const bool capture_started = capture_start_succeeded_;
  lock.unlock();

  if (!capture_started) {
    if (capture_thread_.joinable()) {
      capture_thread_.join();
    }
    on_key_action_ = nullptr;
    user_ptr_ = nullptr;
    return -1;
  }
  return 0;
}

int PlatformKeyboardCapturer::Unhook() {
  hook_active_.store(false);
  DWORD capture_thread_id = 0;
  {
    std::lock_guard<std::mutex> lock(capture_state_mutex_);
    capture_thread_id = capture_thread_id_;
  }
  if (capture_thread_id != 0 &&
      !PostThreadMessageW(capture_thread_id, WM_QUIT, 0, 0)) {
    LOG_WARN("Failed to stop keyboard hook thread, thread_id={}, error={}",
             capture_thread_id, GetLastError());
  }
  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }

  on_key_action_ = nullptr;
  user_ptr_ = nullptr;
  capture_window_ = nullptr;
  local_keys_down_.fill(false);
  captured_keys_.fill({});
  return 0;
}

void PlatformKeyboardCapturer::SnapshotLocalKeys() {
  local_keys_down_.fill(false);
  for (int code = VK_BACK; code < 0xFF; ++code)
    local_keys_down_[code] = (GetAsyncKeyState(code) & 0x8000) != 0;
  ForwardLocalModifiers();
}

void PlatformKeyboardCapturer::ForwardKey(int code, bool down,
                                         uint32_t scan_code, bool extended) {
  captured_keys_[code] = down ? KeyboardKey{code, scan_code, extended}
                              : KeyboardKey{};
  on_key_action_(code, down, scan_code, extended, user_ptr_);
}

void PlatformKeyboardCapturer::ForwardLocalModifiers() {
  // Windows already saw these presses before capture began. Forward held
  // modifiers now, but let their eventual physical releases reach Windows
  // too. Swallowing those releases would leave the controller's keys stuck.
  for (const int code : {VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL,
                         VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN}) {
    if (!local_keys_down_[code] || captured_keys_[code].key_code != 0) continue;
    const UINT scan = MapVirtualKeyW(code, MAPVK_VK_TO_VSC_EX);
    ForwardKey(code, true, scan & 0xFF, (scan & 0xFF00) != 0);
  }
}

void PlatformKeyboardCapturer::ReleaseCapturedKeys() {
  for (const bool modifiers : {false, true}) {
    for (const auto key : captured_keys_) {
      if (key.key_code != 0 && IsKeyboardModifier(key.key_code) == modifiers)
        ForwardKey(key.key_code, false, key.scan_code, key.extended);
    }
  }
}

bool PlatformKeyboardCapturer::RenewKeyboardHook() {
  bool raw_keyboard_suspended = false;
  if (!raw_keyboard_guard_.Suspend(raw_keyboard_suspended)) {
    hook_active_.store(false);
    LOG_WARN("Failed to suspend raw keyboard registration, error={}",
             GetLastError());
    return false;
  }
  // Windows can silently remove a timed-out hook. Renew periodically instead
  // of relying on a bool set at startup. Install first to avoid an input gap.
  const HHOOK replacement = SetWindowsHookExW(
      WH_KEYBOARD_LL, &PlatformKeyboardCapturer::KeyboardHookProc,
      GetModuleHandleW(nullptr), 0);
  if (!replacement) {
    hook_active_.store(false);
    LOG_WARN("Failed to install keyboard hook, error={}", GetLastError());
    return false;
  }
  const HHOOK previous = keyboard_hook_;
  keyboard_hook_ = replacement;
  if (raw_keyboard_suspended && !previous)
    LOG_INFO("Suspended raw keyboard input for shortcut capture");
  bool reset_keys = previous && raw_keyboard_suspended;
  if (previous && !UnhookWindowsHookEx(previous)) {
    const DWORD error = GetLastError();
    if (error != ERROR_INVALID_HOOK_HANDLE) {
      // Do not keep capturing through two live hooks: a passed-through
      // inherited release could otherwise be handled twice and swallowed.
      // Renewal stops immediately, so only this one old hook needs cleanup.
      retired_hook_ = previous;
      hook_active_.store(false);
      LOG_WARN("Failed to retire keyboard hook, error={}", error);
      return false;
    }
    reset_keys = true;
    LOG_WARN("Recovered keyboard hook, error={}", error);
  }
  if (reset_keys) {
    // Events may have escaped while the hook was unavailable or a backend
    // re-enabled raw input. Cancel uncertain presses and adopt local state.
    ReleaseCapturedKeys();
    if (GetForegroundWindow() == capture_window_)
      SnapshotLocalKeys();
    else
      local_keys_down_.fill(false);
    LOG_WARN("Reset captured keys after keyboard capture recovery");
  }
  last_hook_pump_tick_.store(GetTickCount64());
  hook_active_.store(true);
  return true;
}

void PlatformKeyboardCapturer::RemoveKeyboardHook() {
  if (keyboard_hook_) UnhookWindowsHookEx(keyboard_hook_);
  keyboard_hook_ = nullptr;
  if (retired_hook_) UnhookWindowsHookEx(retired_hook_);
  retired_hook_ = nullptr;
  if (!raw_keyboard_guard_.Restore())
    LOG_WARN("Failed to restore raw keyboard registration, error={}",
             GetLastError());
}

void PlatformKeyboardCapturer::CaptureThreadMain() {
  const DWORD thread_id = GetCurrentThreadId();

  MSG message{};
  PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
  active_capturer_ = this;
  // Raw Input observes shortcuts but cannot prevent Alt+Tab, Win+D, etc.
  // from acting on the controller. Keep the hook off the UI/network thread:
  // its callback only queues events for SessionDeviceManager to forward.
  const bool installed = RenewKeyboardHook();
  const UINT_PTR timer = installed
                             ? SetTimer(nullptr, 0, kHookRenewIntervalMs, nullptr)
                             : 0;
  const bool capture_started = installed && timer != 0;
  if (installed && !timer) {
    LOG_WARN("Failed to create keyboard hook renewal timer, error={}",
             GetLastError());
  }
  hook_active_.store(capture_started);
  if (capture_started && GetForegroundWindow() == capture_window_)
    SnapshotLocalKeys();
  {
    std::lock_guard<std::mutex> lock(capture_state_mutex_);
    capture_thread_id_ = thread_id;
    capture_start_succeeded_ = capture_started;
    capture_start_complete_ = true;
  }
  capture_start_condition_.notify_one();

  if (!capture_started) {
    RemoveKeyboardHook();
    active_capturer_ = nullptr;
    std::lock_guard<std::mutex> lock(capture_state_mutex_);
    capture_thread_id_ = 0;
    return;
  }

  LOG_INFO("Keyboard hook capture started, thread_id={}", thread_id);
  while (true) {
    const BOOL get_message_result = GetMessageW(&message, nullptr, 0, 0);
    if (get_message_result <= 0) {
      if (get_message_result < 0) {
        LOG_WARN("Keyboard hook message loop failed, thread_id={}, "
                 "error={}",
                 thread_id, GetLastError());
      }
      break;
    }
    last_hook_pump_tick_.store(GetTickCount64());
    if (message.message == WM_TIMER && message.wParam == timer) {
      if (!RenewKeyboardHook()) break;
      continue;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  hook_active_.store(false);
  KillTimer(nullptr, timer);
  RemoveKeyboardHook();
  active_capturer_ = nullptr;
  {
    std::lock_guard<std::mutex> lock(capture_state_mutex_);
    capture_thread_id_ = 0;
  }
  LOG_INFO("Keyboard hook capture stopped, thread_id={}", thread_id);
}

LRESULT CALLBACK PlatformKeyboardCapturer::KeyboardHookProc(
    int code, WPARAM message, LPARAM data) {
  if (code == HC_ACTION && active_capturer_ && data != 0 &&
      active_capturer_->HandleKeyboardInput(
          message, *reinterpret_cast<const KBDLLHOOKSTRUCT*>(data),
          GetForegroundWindow())) {
    return 1;
  }
  return CallNextHookEx(nullptr, code, message, data);
}

bool PlatformKeyboardCapturer::HandleKeyboardInput(
    WPARAM message, const KBDLLHOOKSTRUCT& keyboard, HWND foreground_window) {
  if (!hook_active_.load() || !on_key_action_ || !capture_window_ ||
      keyboard.vkCode < VK_BACK || keyboard.vkCode >= 0xFF ||
      keyboard.dwExtraInfo == kInjectedKeyboardInputMarker) {
    return false;
  }
  // Do not reject LLKHF_INJECTED wholesale: an upstream remote session or
  // accessibility tool can be the controller's keyboard source.

  bool is_down = false;
  if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
    is_down = true;
  } else if (message != WM_KEYUP && message != WM_SYSKEYUP) {
    return false;
  }

  const bool extended = (keyboard.flags & LLKHF_EXTENDED) != 0;
  UINT mapped_scan_code = keyboard.scanCode & 0xFF;
  if (extended) {
    mapped_scan_code |= 0xE000;
  }
  int key_code = static_cast<int>(keyboard.vkCode);
  if (key_code == VK_SHIFT || key_code == VK_CONTROL || key_code == VK_MENU) {
    const UINT normalized =
        MapVirtualKeyW(mapped_scan_code, MAPVK_VSC_TO_VK_EX);
    if (normalized != 0) {
      key_code = static_cast<int>(normalized);
    }
  }

  if (foreground_window != capture_window_) {
    // This event goes to Windows, even if focus changed before the UI thread
    // has noticed. Remember ownership in case focus returns before Unhook().
    local_keys_down_[key_code] = is_down;
    if (!is_down && captured_keys_[key_code].key_code != 0)
      ForwardKey(key_code, false, keyboard.scanCode, extended);
    return false;
  }
  ForwardLocalModifiers();
  const bool release_local = !is_down && local_keys_down_[key_code];
  if (!is_down) local_keys_down_[key_code] = false;
  ForwardKey(key_code, is_down, keyboard.scanCode, extended);
  return !release_local;
}

// Apply remote keyboard commands to the local machine.
int PlatformKeyboardCapturer::SendKeyboardCommand(int key_code, bool is_down,
                                          uint32_t scan_code, bool extended) {
  if (scan_code == 0) {
    // Some layouts omit the E0 prefix for navigation keys in MapVirtualKey.
    // Preserve explicit metadata, including non-extended numpad navigation.
    LookupWindowsKeyMetadataFromVk(key_code, &scan_code, &extended);
  }
  INPUT input = {0};
  input.type = INPUT_KEYBOARD;
  input.ki.dwExtraInfo =
      static_cast<ULONG_PTR>(kInjectedKeyboardInputMarker);

  const bool prefer_vk = PreferSideSpecificVkInjection(key_code);
  const UINT resolved_scan_code =
      scan_code != 0
          ? static_cast<UINT>(scan_code & 0xFF) | (extended ? 0xE000u : 0u)
          : MapVirtualKeyW(static_cast<UINT>(key_code), MAPVK_VK_TO_VSC_EX);

  if (scan_code != 0 && !prefer_vk) {
    input.ki.wVk = 0;
    input.ki.wScan = static_cast<WORD>(scan_code & 0xFF);
    input.ki.dwFlags |= KEYEVENTF_SCANCODE;
    if (extended) {
      input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
  } else {
    input.ki.wVk = static_cast<WORD>(key_code);

    if (prefer_vk && resolved_scan_code != 0) {
      input.ki.wScan = static_cast<WORD>(resolved_scan_code & 0xFF);
      if ((resolved_scan_code & 0xFF00) != 0) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
      }
    } else if (resolved_scan_code != 0) {
      input.ki.wVk = 0;
      input.ki.wScan = static_cast<WORD>(resolved_scan_code & 0xFF);
      input.ki.dwFlags |= KEYEVENTF_SCANCODE;
      if ((resolved_scan_code & 0xFF00) != 0) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
      }
    }
  }

  if (!is_down) {
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
  }

  const UINT sent = SendInputOnUserDesktop(input);
  if (sent != 1) {
    const DWORD error = GetLastError();
    LOG_WARN("SendInput failed for key_code={}, is_down={}, err={}", key_code,
             is_down, error);
    SetLastError(error);
    return -1;
  }

  return 0;
}
}  // namespace crossdesk
