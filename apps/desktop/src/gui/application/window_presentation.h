#pragma once

#include <slint.h>

namespace crossdesk {

// Winit creates hidden native windows on a later event-loop turn. Wait for
// that window before applying geometry that depends on its frame or DPI.
inline bool NativeWindowReady([[maybe_unused]] const slint::Window& window) {
#if defined(_WIN32)
  return window.win32_hwnd() != nullptr;
#elif defined(__APPLE__)
  return window.appkit_view() != nullptr;
#else
  // Other backends accept initial geometry before show(); Wayland placement
  // is controlled by the compositor and must not wait for a mapped surface.
  return true;
#endif
}

}  // namespace crossdesk
