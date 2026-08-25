#include "runtime/cursor_state_provider.h"

#if defined(_WIN32)

#include <windows.h>

namespace crossdesk {
namespace {

bool IsSystemCursor(HCURSOR cursor, LPCWSTR resource) {
  return cursor != nullptr && cursor == LoadCursorW(nullptr, resource);
}

CursorShape ShapeFromWindowsCursor(HCURSOR cursor) {
  if (IsSystemCursor(cursor, IDC_HELP)) return CursorShape::help;
  if (IsSystemCursor(cursor, IDC_HAND)) return CursorShape::pointer;
  if (IsSystemCursor(cursor, IDC_APPSTARTING)) return CursorShape::progress;
  if (IsSystemCursor(cursor, IDC_WAIT)) return CursorShape::wait;
  if (IsSystemCursor(cursor, IDC_CROSS)) return CursorShape::crosshair;
  if (IsSystemCursor(cursor, IDC_IBEAM)) return CursorShape::text;
  if (IsSystemCursor(cursor, IDC_NO)) return CursorShape::not_allowed;
  if (IsSystemCursor(cursor, IDC_SIZEALL)) return CursorShape::move;
  if (IsSystemCursor(cursor, IDC_SIZEWE)) return CursorShape::ew_resize;
  if (IsSystemCursor(cursor, IDC_SIZENS)) return CursorShape::ns_resize;
  if (IsSystemCursor(cursor, IDC_SIZENESW)) return CursorShape::nesw_resize;
  if (IsSystemCursor(cursor, IDC_SIZENWSE)) return CursorShape::nwse_resize;
  if (IsSystemCursor(cursor, IDC_UPARROW)) return CursorShape::n_resize;
  return CursorShape::default_cursor;
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
                                : CursorShape::none;
  return true;
}

}  // namespace crossdesk

#elif defined(__linux__) && !defined(__APPLE__)

#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

// X11/X.h defines CursorShape as a protocol request opcode, which conflicts
// with CrossDesk's CursorShape enum.
#ifdef CursorShape
#undef CursorShape
#endif

#include <algorithm>
#include <cctype>
#include <string>

namespace crossdesk {
namespace {

std::string Lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return std::tolower(ch); });
  return value;
}

bool Contains(const std::string& value, const char* token) {
  return value.find(token) != std::string::npos;
}

CursorShape ShapeFromXCursorName(const std::string& raw_name) {
  const std::string name = Lowercase(raw_name);
  if (Contains(name, "left_ptr_watch") || Contains(name, "progress"))
    return CursorShape::progress;
  if (Contains(name, "watch") || Contains(name, "wait"))
    return CursorShape::wait;
  if (Contains(name, "question") || Contains(name, "help"))
    return CursorShape::help;
  if (Contains(name, "xterm") || Contains(name, "vertical-text") ||
      name == "text")
    return CursorShape::text;
  if (Contains(name, "crosshair") || name == "cross" || name == "tcross")
    return CursorShape::crosshair;
  if (Contains(name, "closedhand") || Contains(name, "grabbing"))
    return CursorShape::grabbing;
  if (Contains(name, "openhand") || Contains(name, "grab"))
    return CursorShape::grab;
  if (Contains(name, "dnd-link") || name == "alias")
    return CursorShape::alias;
  if (Contains(name, "hand") || Contains(name, "pointer") ||
      Contains(name, "link"))
    return CursorShape::pointer;
  if (Contains(name, "dnd-copy") || name == "copy")
    return CursorShape::copy;
  if (Contains(name, "no-drop")) return CursorShape::no_drop;
  if (Contains(name, "not-allowed") || Contains(name, "crossed_circle"))
    return CursorShape::not_allowed;
  if (name == "fleur" || Contains(name, "size_all") || name == "move")
    return CursorShape::move;
  if (Contains(name, "top_left_corner") ||
      Contains(name, "bottom_right_corner") ||
      Contains(name, "nwse-resize") || Contains(name, "size_fdiag"))
    return CursorShape::nwse_resize;
  if (Contains(name, "top_right_corner") ||
      Contains(name, "bottom_left_corner") ||
      Contains(name, "nesw-resize") || Contains(name, "size_bdiag"))
    return CursorShape::nesw_resize;
  if (Contains(name, "sb_h_double_arrow") || Contains(name, "ew-resize") ||
      Contains(name, "size_hor"))
    return CursorShape::ew_resize;
  if (Contains(name, "sb_v_double_arrow") || Contains(name, "ns-resize") ||
      Contains(name, "size_ver"))
    return CursorShape::ns_resize;
  if (Contains(name, "col-resize")) return CursorShape::col_resize;
  if (Contains(name, "row-resize")) return CursorShape::row_resize;
  if (Contains(name, "ne-resize")) return CursorShape::ne_resize;
  if (Contains(name, "nw-resize")) return CursorShape::nw_resize;
  if (Contains(name, "se-resize")) return CursorShape::se_resize;
  if (Contains(name, "sw-resize")) return CursorShape::sw_resize;
  if (Contains(name, "top_side") || name == "n-resize")
    return CursorShape::n_resize;
  if (Contains(name, "right_side") || name == "e-resize")
    return CursorShape::e_resize;
  if (Contains(name, "bottom_side") || name == "s-resize")
    return CursorShape::s_resize;
  if (Contains(name, "left_side") || name == "w-resize")
    return CursorShape::w_resize;
  return CursorShape::default_cursor;
}

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
  if (!state || !impl_ || !impl_->display) return false;

  XFixesCursorImage* image = XFixesGetCursorImage(impl_->display);
  if (!image) return false;

  const std::string name = image->name ? image->name : "";

  state->seq = 0;
  state->visible = CursorHasVisiblePixel(*image);
  state->shape = state->visible ? ShapeFromXCursorName(name)
                                : CursorShape::none;
  XFree(image);
  return true;
}

}  // namespace crossdesk

#endif
