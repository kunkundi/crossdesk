#include "keyboard_capturer.h"

#include "rd_log.h"

namespace crossdesk {

static OnKeyAction g_on_key_action = nullptr;
static void* g_user_ptr = nullptr;

static int NormalizeModifierVkCode(const KBDLLHOOKSTRUCT* kb_data) {
  if (kb_data == nullptr) {
    return -1;
  }

  if (kb_data->vkCode != VK_SHIFT && kb_data->vkCode != VK_CONTROL &&
      kb_data->vkCode != VK_MENU) {
    return static_cast<int>(kb_data->vkCode);
  }

  UINT scan_code = static_cast<UINT>(kb_data->scanCode & 0xFF);
  if ((kb_data->flags & LLKHF_EXTENDED) != 0) {
    scan_code |= 0xE000;
  }

  const UINT normalized_vk = MapVirtualKeyW(scan_code, MAPVK_VSC_TO_VK_EX);
  if (normalized_vk != 0) {
    return static_cast<int>(normalized_vk);
  }

  return static_cast<int>(kb_data->vkCode);
}

static bool PreferSideSpecificVkInjection(int key_code) {
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

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION && g_on_key_action) {
    KBDLLHOOKSTRUCT* kbData = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    const int key_code = NormalizeModifierVkCode(kbData);

    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
      g_on_key_action(key_code, true, kbData->scanCode,
                      (kbData->flags & LLKHF_EXTENDED) != 0, g_user_ptr);
    } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
      g_on_key_action(key_code, false, kbData->scanCode,
                      (kbData->flags & LLKHF_EXTENDED) != 0, g_user_ptr);
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
int KeyboardCapturer::SendKeyboardCommand(int key_code, bool is_down,
                                          uint32_t scan_code, bool extended) {
  INPUT input = {0};
  input.type = INPUT_KEYBOARD;

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
    input.ki.wVk = (WORD)key_code;

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

  UINT sent = SendInput(1, &input, sizeof(INPUT));
  if (sent != 1) {
    LOG_WARN("SendInput failed for key_code={}, is_down={}, err={}", key_code,
             is_down, GetLastError());
    return -1;
  }

  return 0;
}
}  // namespace crossdesk
