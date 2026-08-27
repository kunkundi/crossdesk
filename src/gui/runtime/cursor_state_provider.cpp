#include "runtime/cursor_state_provider.h"

#if defined(_WIN32)

#include <windows.h>

namespace crossdesk {
namespace {

bool IsSystemCursor(HCURSOR cursor, LPCWSTR resource) {
  return cursor != nullptr && cursor == LoadCursorW(nullptr, resource);
}

RemoteCursorShape ShapeFromWindowsCursor(HCURSOR cursor) {
  if (IsSystemCursor(cursor, IDC_HELP)) return RemoteCursorShape::help;
  if (IsSystemCursor(cursor, IDC_HAND)) return RemoteCursorShape::pointer;
  if (IsSystemCursor(cursor, IDC_APPSTARTING))
    return RemoteCursorShape::progress;
  if (IsSystemCursor(cursor, IDC_WAIT)) return RemoteCursorShape::wait;
  if (IsSystemCursor(cursor, IDC_CROSS)) return RemoteCursorShape::crosshair;
  if (IsSystemCursor(cursor, IDC_IBEAM)) return RemoteCursorShape::text;
  if (IsSystemCursor(cursor, IDC_NO))
    return RemoteCursorShape::not_allowed;
  if (IsSystemCursor(cursor, IDC_SIZEALL)) return RemoteCursorShape::move;
  if (IsSystemCursor(cursor, IDC_SIZEWE)) return RemoteCursorShape::ew_resize;
  if (IsSystemCursor(cursor, IDC_SIZENS)) return RemoteCursorShape::ns_resize;
  if (IsSystemCursor(cursor, IDC_SIZENESW))
    return RemoteCursorShape::nesw_resize;
  if (IsSystemCursor(cursor, IDC_SIZENWSE))
    return RemoteCursorShape::nwse_resize;
  if (IsSystemCursor(cursor, IDC_UPARROW)) return RemoteCursorShape::n_resize;
  return RemoteCursorShape::default_cursor;
}

}  // namespace

struct CursorStateProvider::Impl {};

CursorStateProvider::CursorStateProvider() : impl_(std::make_unique<Impl>()) {}
CursorStateProvider::~CursorStateProvider() = default;

bool CursorStateProvider::Sample(CursorState* state) {
  if (!state) return false;

  CURSORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetCursorInfo(&info)) return false;

  state->seq = 0;
  state->visible = (info.flags & CURSOR_SHOWING) != 0;
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

bool CursorStateProvider::Sample(CursorState* state) {
  if (!state || !impl_) return false;

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
  XFree(image);
  return true;
}

}  // namespace crossdesk

#endif
