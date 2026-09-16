#include "window_icons.h"

#include <commctrl.h>

#include <memory>
#include <utility>

namespace crossdesk {
namespace {

constexpr UINT_PTR kIconSubclassId = 1;

struct WindowIcons {
  HICON small_icon = nullptr;
  HICON large_icon = nullptr;

  ~WindowIcons() {
    if (small_icon) DestroyIcon(small_icon);
    if (large_icon) DestroyIcon(large_icon);
  }

  bool Load(UINT dpi) {
    const HMODULE module = GetModuleHandleW(nullptr);
    small_icon = static_cast<HICON>(LoadImageW(
        module, L"IDI_ICON1", IMAGE_ICON,
        GetSystemMetricsForDpi(SM_CXSMICON, dpi),
        GetSystemMetricsForDpi(SM_CYSMICON, dpi), 0));
    large_icon = static_cast<HICON>(LoadImageW(
        module, L"IDI_ICON1", IMAGE_ICON,
        GetSystemMetricsForDpi(SM_CXICON, dpi),
        GetSystemMetricsForDpi(SM_CYICON, dpi), 0));
    return small_icon && large_icon;
  }

  void Apply(HWND window) const {
    // Do not destroy the replaced handles: Slint/winit owns those images.
    SendMessageW(window, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(small_icon));
    SendMessageW(window, WM_SETICON, ICON_BIG,
                 reinterpret_cast<LPARAM>(large_icon));
  }
};

LRESULT CALLBACK IconSubclassProc(HWND window, UINT message, WPARAM wparam,
                                  LPARAM lparam, UINT_PTR subclass_id,
                                  DWORD_PTR reference_data) {
  auto* icons = reinterpret_cast<WindowIcons*>(reference_data);
  if (message == WM_SETICON) {
    // Slint supplies the same PNG for both sizes, including during deferred
    // window initialization. Keep using the matching frames from our ICO.
    if (wparam == ICON_SMALL) {
      lparam = reinterpret_cast<LPARAM>(icons->small_icon);
    } else if (wparam == ICON_BIG) {
      lparam = reinterpret_cast<LPARAM>(icons->large_icon);
    }
    return DefSubclassProc(window, message, wparam, lparam);
  }
  if (message == WM_DPICHANGED) {
    WindowIcons next;
    if (next.Load(HIWORD(wparam))) {
      std::swap(icons->small_icon, next.small_icon);
      std::swap(icons->large_icon, next.large_icon);
      icons->Apply(window);
      // 'next' releases the previous pair only after both were replaced.
    }
  }
  if (message == WM_NCDESTROY) {
    const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
    RemoveWindowSubclass(window, IconSubclassProc, subclass_id);
    delete icons;
    return result;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

}  // namespace

bool ConfigureWindowsWindowIcons(HWND window) {
  if (!window) {
    return false;
  }
  DWORD_PTR existing = 0;
  if (GetWindowSubclass(window, IconSubclassProc, kIconSubclassId, &existing)) {
    return true;
  }
  auto icons = std::make_unique<WindowIcons>();
  const UINT dpi = GetDpiForWindow(window);
  if (!dpi || !icons->Load(dpi) ||
      !SetWindowSubclass(window, IconSubclassProc, kIconSubclassId,
                         reinterpret_cast<DWORD_PTR>(icons.get()))) {
    return false;
  }
  // The subclass owns the pair until WM_NCDESTROY, even if Slint hides and
  // recreates its native window while the component itself stays alive.
  auto* installed = icons.release();
  installed->Apply(window);
  return true;
}

}  // namespace crossdesk
