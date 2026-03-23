#include "keyboard_capturer.h"

#include "rd_log.h"

namespace crossdesk {

static OnKeyAction g_on_key_action = nullptr;
static void* g_user_ptr = nullptr;

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION && g_on_key_action) {
    KBDLLHOOKSTRUCT* kbData = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
      g_on_key_action(kbData->vkCode, true, g_user_ptr);
    } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
      g_on_key_action(kbData->vkCode, false, g_user_ptr);
    }
    return 1;
  }

  return CallNextHookEx(NULL, nCode, wParam, lParam);
}

KeyboardCapturer::KeyboardCapturer() {}

KeyboardCapturer::~KeyboardCapturer() {}

int KeyboardCapturer::Hook(OnKeyAction on_key_action, void* user_ptr) {
  g_on_key_action = on_key_action;
  g_user_ptr = user_ptr;

  keyboard_hook_ = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, NULL, 0);
  if (!keyboard_hook_) {
    LOG_ERROR("Failed to install keyboard hook");
    return -1;
  }
  return 0;
}

int KeyboardCapturer::Unhook() {
  if (keyboard_hook_) {
    g_on_key_action = nullptr;
    g_user_ptr = nullptr;
    UnhookWindowsHookEx(keyboard_hook_);
    keyboard_hook_ = nullptr;
  }
  return 0;
}

// apply remote keyboard commands to the local machine
int KeyboardCapturer::SendKeyboardCommand(int key_code, bool is_down) {
  INPUT input = {0};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = (WORD)key_code;

  const UINT scan_code =
      MapVirtualKeyW(static_cast<UINT>(key_code), MAPVK_VK_TO_VSC_EX);
  if (scan_code != 0) {
    input.ki.wVk = 0;
    input.ki.wScan = static_cast<WORD>(scan_code & 0xFF);
    input.ki.dwFlags |= KEYEVENTF_SCANCODE;
    if ((scan_code & 0xFF00) != 0) {
      input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
  }

  if (!is_down) {
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
  }

  UINT sent = SendInput(1, &input, sizeof(INPUT));
  if (sent != 1) {
    LOG_WARN("SendInput failed for key_code={}, is_down={}, err={}", key_code,
             is_down, GetLastError());
    return -1;
  }

  return 0;
}
}  // namespace crossdesk
