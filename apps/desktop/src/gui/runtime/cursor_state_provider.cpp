#include "runtime/cursor_state_provider.h"

#include <remote_action.h>

#if defined(_WIN32)

#include <windows.h>

#include "platform/windows/input/windows_cursor_shape.h"
#include "platform/windows/screen_capturer/dxgi_cursor_state.h"
#include "platform/windows/screen_capturer/secure_desktop_cursor_state.h"
#include "runtime/cursor_position.h"

namespace crossdesk {

struct CursorStateProvider::Impl {};

CursorStateProvider::CursorStateProvider() : impl_(std::make_unique<Impl>()) {}
CursorStateProvider::~CursorStateProvider() = default;

bool CursorStateProvider::Sample(const std::vector<DisplayInfo>& displays,
                                 int preferred_display, CursorState* state) {
  if (!state) return false;

  SecureDesktopCursorSnapshot secure_cursor{};
  if (SharedSecureDesktopCursorState().Get(&secure_cursor)) {
    if (!secure_cursor.valid) return false;
    state->seq = 0;
    NormalizeCursorPosition(secure_cursor.x, secure_cursor.y, displays,
                            preferred_display, state);
    state->visible = secure_cursor.visible != 0;
    state->shape = state->visible
                       ? static_cast<RemoteCursorShape>(secure_cursor.shape)
                       : RemoteCursorShape::none;
    return true;
  }

  CURSORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetCursorInfo(&info)) return false;

  state->seq = 0;
  NormalizeCursorPosition(info.ptScreenPos.x, info.ptScreenPos.y, displays,
                          preferred_display, state);
  void* cursor_monitor = state->position_valid
                             ? displays[state->display_id].handle
                             : nullptr;
  state->visible = SharedDxgiCursorState().ShouldDrawCursor(
      (info.flags & CURSOR_SHOWING) != 0, cursor_monitor);
  state->shape = state->visible ? ShapeFromWindowsCursor(info.hCursor)
                                : RemoteCursorShape::none;
  return true;
}

}  // namespace crossdesk

#elif defined(__linux__) && !defined(__APPLE__)

#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

#include <string>

#include "linux_cursor_shape.h"
#include "platform.h"
#include "shared_cursor_state.h"
#include "runtime/cursor_position.h"

namespace crossdesk {
namespace {

bool CursorHasVisiblePixel(const XFixesCursorImage& image) {
  const size_t pixel_count =
      static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
  for (size_t index = 0; index < pixel_count; ++index) {
    if (((image.pixels[index] >> 24U) & 0xffU) != 0) return true;
  }
  return false;
}

}  // namespace

struct CursorStateProvider::Impl {
  Display* display = XOpenDisplay(nullptr);

  ~Impl() {
    if (display) XCloseDisplay(display);
  }
};

CursorStateProvider::CursorStateProvider() : impl_(std::make_unique<Impl>()) {}
CursorStateProvider::~CursorStateProvider() = default;

bool CursorStateProvider::Sample(const std::vector<DisplayInfo>& displays,
                                 int preferred_display, CursorState* state) {
  if (!state || !impl_) return false;
  ResetCursorPosition(state);

  if (IsWaylandSession()) {
    SharedCursorState shared{};
    if (GetSharedCursorState(&shared)) {
      state->seq = 0;
      state->visible = shared.visible;
      state->shape = shared.visible ? shared.shape : RemoteCursorShape::none;
      return true;
    }
  }

  if (!impl_->display) return false;

  XFixesCursorImage* image = XFixesGetCursorImage(impl_->display);
  if (!image) return false;

  const std::string name = image->name ? image->name : "";

  state->seq = 0;
  state->visible = CursorHasVisiblePixel(*image);
  state->shape = state->visible ? ShapeFromLinuxCursorName(name)
                                : RemoteCursorShape::none;
  NormalizeCursorPosition(image->x, image->y, displays, preferred_display,
                          state);
  XFree(image);
  return true;
}

}  // namespace crossdesk

#endif
