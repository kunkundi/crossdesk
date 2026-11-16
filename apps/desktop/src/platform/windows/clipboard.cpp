#include "clipboard.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <cstring>

#include "platform/clipboard_backend.h"
#include "rd_log.h"
#include "filesystem_utf8.h"

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

std::optional<std::vector<std::string>> Clipboard::GetFiles() {
  if (!IsClipboardFormatAvailable(CF_HDROP)) return std::vector<std::string>{};
  // Preserve file precedence when another process briefly holds the clipboard.
  if (!OpenClipboard(nullptr)) return std::nullopt;
  const auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
  std::vector<std::string> paths;
  const UINT count = drop ? DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0) : 0;
  if (count <= 128) for (UINT i = 0; i < count; ++i) {
    const UINT size = DragQueryFileW(drop, i, nullptr, 0);
    std::wstring path(size + 1, L'\0');
    if (!size || !DragQueryFileW(drop, i, path.data(), size + 1)) break;
    path.resize(size);
    const auto utf8 = std::filesystem::path(path).u8string();
    paths.emplace_back(utf8.begin(), utf8.end());
  }
  CloseClipboard();
  if (!count || paths.size() != count) return std::nullopt;
  return paths;
}

uint64_t Clipboard::ChangeCount() { return GetClipboardSequenceNumber(); }

bool Clipboard::SetFiles(const std::vector<std::string>& paths) {
  if (paths.empty()) return false;
  std::wstring list;
  for (const auto& path : paths) {
    const auto native = PathFromUtf8(path);
    if (!native.is_absolute()) return false;
    list.append(native.native()); list.push_back(L'\0');
  }
  list.push_back(L'\0');
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
                               sizeof(DROPFILES) + list.size() * sizeof(wchar_t));
  if (!memory) return false;
  auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
  if (!drop) { GlobalFree(memory); return false; }
  drop->pFiles = sizeof(DROPFILES); drop->fWide = TRUE;
  memcpy(reinterpret_cast<char*>(drop) + sizeof(DROPFILES), list.data(),
         list.size() * sizeof(wchar_t));
  GlobalUnlock(memory);
  // OpenClipboard(NULL) cannot own a clipboard after EmptyClipboard().
  static HWND owner = CreateWindowExW(0, L"STATIC", L"CrossDesk file clipboard",
      0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!owner || !OpenClipboard(owner)) { GlobalFree(memory); return false; }
  const bool ok = EmptyClipboard() && SetClipboardData(CF_HDROP, memory);
  if (!ok) GlobalFree(memory);
  if (ok) {
    // Explorer must copy these temporary files, even if the source used Cut.
    HGLOBAL effect = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (effect) {
      auto* value = static_cast<DWORD*>(GlobalLock(effect));
      if (value) {
        *value = DROPEFFECT_COPY; GlobalUnlock(effect);
        if (!SetClipboardData(RegisterClipboardFormatW(L"Preferred DropEffect"), effect))
          GlobalFree(effect);
      } else GlobalFree(effect);
    }
  }
  CloseClipboard();
  return ok;
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
