#include "clipboard.h"

#include <windows.h>

#include <string>

#include "platform/clipboard_backend.h"
#include "rd_log.h"

namespace crossdesk {
namespace {

HWND g_clipboard_window = nullptr;
constexpr const char* kClipboardWindowClass = "CrossDeskClipboardMonitor";

LRESULT CALLBACK ClipboardWindowProc(HWND window, UINT message, WPARAM wparam,
                                     LPARAM lparam) {
  if (message == WM_CLIPBOARDUPDATE) {
    platform::HandleClipboardChange();
    return 0;
  }
  return DefWindowProc(window, message, wparam, lparam);
}

}  // namespace

std::string Clipboard::GetText() {
  if (!OpenClipboard(nullptr)) {
    LOG_ERROR("Clipboard::GetText: failed to open clipboard");
    return {};
  }

  std::string result;
  HANDLE data = GetClipboardData(CF_UNICODETEXT);
  if (data) {
    const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data));
    if (text) {
      const int size =
          WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
      if (size > 1) {
        result.resize(static_cast<size_t>(size));
        WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size,
                            nullptr, nullptr);
        result.resize(static_cast<size_t>(size - 1));
      }
      GlobalUnlock(data);
    }
  }
  CloseClipboard();
  return result;
}

bool Clipboard::SetText(const std::string& text) {
  if (!OpenClipboard(nullptr)) {
    LOG_ERROR("Clipboard::SetText: failed to open clipboard");
    return false;
  }
  if (!EmptyClipboard()) {
    LOG_ERROR("Clipboard::SetText: failed to empty clipboard");
    CloseClipboard();
    return false;
  }

  const int size =
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  HGLOBAL memory =
      size > 0 ? GlobalAlloc(GMEM_MOVEABLE, size * sizeof(wchar_t)) : nullptr;
  if (!memory) {
    LOG_ERROR("Clipboard::SetText: failed to allocate memory");
    CloseClipboard();
    return false;
  }

  wchar_t* output = static_cast<wchar_t*>(GlobalLock(memory));
  if (!output) {
    GlobalFree(memory);
    CloseClipboard();
    return false;
  }
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, output, size);
  GlobalUnlock(memory);
  if (!SetClipboardData(CF_UNICODETEXT, memory)) {
    GlobalFree(memory);
    CloseClipboard();
    return false;
  }
  CloseClipboard();
  return true;
}

bool Clipboard::HasText() {
  if (!OpenClipboard(nullptr)) return false;
  const bool result = IsClipboardFormatAvailable(CF_UNICODETEXT) ||
                      IsClipboardFormatAvailable(CF_TEXT);
  CloseClipboard();
  return result;
}

namespace platform {

void RunClipboardMonitor(int) {
  WNDCLASSA window_class = {};
  window_class.lpfnWndProc = ClipboardWindowProc;
  window_class.hInstance = GetModuleHandle(nullptr);
  window_class.lpszClassName = kClipboardWindowClass;
  RegisterClassA(&window_class);

  g_clipboard_window = CreateWindowA(
      kClipboardWindowClass, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
      nullptr, nullptr);
  if (!g_clipboard_window ||
      !AddClipboardFormatListener(g_clipboard_window)) {
    LOG_ERROR("Failed to initialize clipboard monitor window");
    if (g_clipboard_window) DestroyWindow(g_clipboard_window);
    g_clipboard_window = nullptr;
    return;
  }

  LOG_INFO("Clipboard event monitoring started (Windows)");
  MSG message = {};
  while (ClipboardMonitoring()) {
    const BOOL result = GetMessage(&message, nullptr, 0, 0);
    if (result == 0 || result == -1) break;
    TranslateMessage(&message);
    DispatchMessage(&message);
  }

  RemoveClipboardFormatListener(g_clipboard_window);
  DestroyWindow(g_clipboard_window);
  g_clipboard_window = nullptr;
  UnregisterClassA(kClipboardWindowClass, GetModuleHandle(nullptr));
}

void WakeClipboardMonitor() {
  if (g_clipboard_window) PostMessage(g_clipboard_window, WM_QUIT, 0, 0);
}

}  // namespace platform
}  // namespace crossdesk
