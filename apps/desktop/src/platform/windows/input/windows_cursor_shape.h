#ifndef CROSSDESK_WINDOWS_CURSOR_SHAPE_H_
#define CROSSDESK_WINDOWS_CURSOR_SHAPE_H_

#include <windows.h>

#include <remote_cursor_shape.h>

namespace crossdesk {

inline RemoteCursorShape ShapeFromWindowsCursor(HCURSOR cursor) {
  const auto is_system_cursor = [cursor](LPCWSTR resource) {
    return cursor != nullptr && cursor == LoadCursorW(nullptr, resource);
  };
  if (is_system_cursor(IDC_HELP)) return RemoteCursorShape::help;
  if (is_system_cursor(IDC_HAND)) return RemoteCursorShape::pointer;
  if (is_system_cursor(IDC_APPSTARTING)) return RemoteCursorShape::progress;
  if (is_system_cursor(IDC_WAIT)) return RemoteCursorShape::wait;
  if (is_system_cursor(IDC_CROSS)) return RemoteCursorShape::crosshair;
  if (is_system_cursor(IDC_IBEAM)) return RemoteCursorShape::text;
  if (is_system_cursor(IDC_NO)) return RemoteCursorShape::not_allowed;
  if (is_system_cursor(IDC_SIZEALL)) return RemoteCursorShape::move;
  if (is_system_cursor(IDC_SIZEWE)) return RemoteCursorShape::ew_resize;
  if (is_system_cursor(IDC_SIZENS)) return RemoteCursorShape::ns_resize;
  if (is_system_cursor(IDC_SIZENESW)) return RemoteCursorShape::nesw_resize;
  if (is_system_cursor(IDC_SIZENWSE)) return RemoteCursorShape::nwse_resize;
  if (is_system_cursor(IDC_UPARROW)) return RemoteCursorShape::n_resize;
  return RemoteCursorShape::default_cursor;
}

}  // namespace crossdesk

#endif  // CROSSDESK_WINDOWS_CURSOR_SHAPE_H_
