#include "keyboard_capturer.h"

#include <hidusage.h>

#include "rd_log.h"
#include "windows_input_marker.h"

namespace crossdesk {
namespace {

constexpr wchar_t kRawInputWindowClassName[] =
    L"CrossDeskKeyboardRawInputWindow";

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

KeyboardCapturer::KeyboardCapturer() {}

KeyboardCapturer::~KeyboardCapturer() { Unhook(); }

int KeyboardCapturer::Hook(OnKeyAction on_key_action, void* user_ptr) {
  if (capture_thread_.joinable()) {
    return 0;
  }

  on_key_action_ = on_key_action;
  user_ptr_ = user_ptr;
  {
    std::lock_guard<std::mutex> lock(capture_state_mutex_);
    capture_thread_id_ = 0;
    capture_start_complete_ = false;
    capture_start_succeeded_ = false;
  }

  capture_thread_ = std::thread(&KeyboardCapturer::RawInputThreadMain, this);

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

int KeyboardCapturer::Unhook() {
  DWORD capture_thread_id = 0;
  {
    std::lock_guard<std::mutex> lock(capture_state_mutex_);
    capture_thread_id = capture_thread_id_;
  }
  if (capture_thread_id != 0 &&
      !PostThreadMessageW(capture_thread_id, WM_QUIT, 0, 0)) {
    LOG_WARN("Failed to stop keyboard raw input thread, thread_id={}, error={}",
             capture_thread_id, GetLastError());
  }
  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }

  on_key_action_ = nullptr;
  user_ptr_ = nullptr;
  return 0;
}

void KeyboardCapturer::RawInputThreadMain() {
  const DWORD thread_id = GetCurrentThreadId();

  MSG message{};
  PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
  const bool capture_started = CreateRawInputWindow();
  {
    std::lock_guard<std::mutex> lock(capture_state_mutex_);
    capture_thread_id_ = thread_id;
    capture_start_succeeded_ = capture_started;
    capture_start_complete_ = true;
  }
  capture_start_condition_.notify_one();

  if (!capture_started) {
    std::lock_guard<std::mutex> lock(capture_state_mutex_);
    capture_thread_id_ = 0;
    return;
  }

  LOG_INFO("Keyboard raw input capture started, thread_id={}", thread_id);
  while (true) {
    const BOOL get_message_result = GetMessageW(&message, nullptr, 0, 0);
    if (get_message_result <= 0) {
      if (get_message_result < 0) {
        LOG_WARN("Keyboard raw input message loop failed, thread_id={}, "
                 "error={}",
                 thread_id, GetLastError());
      }
      break;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  DestroyRawInputWindow();
  {
    std::lock_guard<std::mutex> lock(capture_state_mutex_);
    capture_thread_id_ = 0;
  }
  LOG_INFO("Keyboard raw input capture stopped, thread_id={}", thread_id);
}

bool KeyboardCapturer::CreateRawInputWindow() {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &KeyboardCapturer::RawInputWindowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = kRawInputWindowClassName;

  if (RegisterClassExW(&window_class) == 0) {
    const DWORD error = GetLastError();
    if (error != ERROR_CLASS_ALREADY_EXISTS) {
      LOG_WARN("Failed to register keyboard raw input window class, error={}",
               error);
      return false;
    }
  }

  raw_input_window_ = CreateWindowExW(
      0, kRawInputWindowClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
      instance, this);
  if (!raw_input_window_) {
    LOG_WARN("Failed to create keyboard raw input window, error={}",
             GetLastError());
    return false;
  }

  RAWINPUTDEVICE keyboard_device{};
  keyboard_device.usUsagePage = HID_USAGE_PAGE_GENERIC;
  keyboard_device.usUsage = HID_USAGE_GENERIC_KEYBOARD;
  keyboard_device.dwFlags = RIDEV_DEVNOTIFY | RIDEV_INPUTSINK;
  keyboard_device.hwndTarget = raw_input_window_;
  if (!RegisterRawInputDevices(&keyboard_device, 1,
                               sizeof(keyboard_device))) {
    LOG_WARN("Failed to register keyboard raw input, error={}", GetLastError());
    DestroyWindow(raw_input_window_);
    raw_input_window_ = nullptr;
    return false;
  }

  raw_input_registered_ = true;
  return true;
}

void KeyboardCapturer::DestroyRawInputWindow() {
  if (raw_input_registered_) {
    RAWINPUTDEVICE keyboard_device{};
    keyboard_device.usUsagePage = HID_USAGE_PAGE_GENERIC;
    keyboard_device.usUsage = HID_USAGE_GENERIC_KEYBOARD;
    keyboard_device.dwFlags = RIDEV_REMOVE;
    keyboard_device.hwndTarget = nullptr;
    if (!RegisterRawInputDevices(&keyboard_device, 1,
                                 sizeof(keyboard_device))) {
      LOG_WARN("Failed to unregister keyboard raw input, error={}",
               GetLastError());
    }
    raw_input_registered_ = false;
  }
  if (raw_input_window_) {
    DestroyWindow(raw_input_window_);
    raw_input_window_ = nullptr;
  }
}

LRESULT CALLBACK KeyboardCapturer::RawInputWindowProc(
    HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
  KeyboardCapturer* capturer = reinterpret_cast<KeyboardCapturer*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
    capturer = static_cast<KeyboardCapturer*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(capturer));
  } else if (message == WM_INPUT && capturer) {
    capturer->HandleRawInput(reinterpret_cast<HRAWINPUT>(l_param));
  } else if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
  }
  return DefWindowProcW(window, message, w_param, l_param);
}

void KeyboardCapturer::HandleRawInput(HRAWINPUT raw_input_handle) {
  RAWINPUT input{};
  UINT input_size = sizeof(input);
  const UINT bytes_read =
      GetRawInputData(raw_input_handle, RID_INPUT, &input, &input_size,
                      sizeof(RAWINPUTHEADER));
  if (bytes_read == static_cast<UINT>(-1) ||
      bytes_read < sizeof(RAWINPUTHEADER) ||
      input.header.dwType != RIM_TYPEKEYBOARD) {
    return;
  }

  const RAWKEYBOARD& keyboard = input.data.keyboard;
  if (keyboard.VKey == 0xFF ||
      keyboard.ExtraInformation ==
          static_cast<ULONG>(kInjectedKeyboardInputMarker)) {
    return;
  }

  bool is_down = false;
  if (keyboard.Message == WM_KEYDOWN || keyboard.Message == WM_SYSKEYDOWN) {
    is_down = true;
  } else if (keyboard.Message != WM_KEYUP &&
             keyboard.Message != WM_SYSKEYUP) {
    return;
  }

  const bool extended = (keyboard.Flags & (RI_KEY_E0 | RI_KEY_E1)) != 0;
  UINT mapped_scan_code = keyboard.MakeCode;
  if ((keyboard.Flags & RI_KEY_E0) != 0) {
    mapped_scan_code |= 0xE000;
  } else if ((keyboard.Flags & RI_KEY_E1) != 0) {
    mapped_scan_code |= 0xE100;
  }
  if (mapped_scan_code == 0xE11D || mapped_scan_code == 0xE02A) {
    return;
  }

  int key_code = static_cast<int>(keyboard.VKey);
  if (key_code == VK_SHIFT || key_code == VK_CONTROL || key_code == VK_MENU) {
    const UINT normalized =
        MapVirtualKeyW(mapped_scan_code, MAPVK_VSC_TO_VK_EX);
    if (normalized != 0) {
      key_code = static_cast<int>(normalized);
    }
  }

  if (on_key_action_) {
    on_key_action_(key_code, is_down, keyboard.MakeCode, extended, user_ptr_);
  }
}

// Apply remote keyboard commands to the local machine.
int KeyboardCapturer::SendKeyboardCommand(int key_code, bool is_down,
                                          uint32_t scan_code, bool extended) {
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

  const UINT sent = SendInput(1, &input, sizeof(INPUT));
  if (sent != 1) {
    LOG_WARN("SendInput failed for key_code={}, is_down={}, err={}", key_code,
             is_down, GetLastError());
    return -1;
  }

  return 0;
}
}  // namespace crossdesk
