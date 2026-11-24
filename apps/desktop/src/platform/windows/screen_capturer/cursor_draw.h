/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-16
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CURSOR_DRAW_H_
#define _CURSOR_DRAW_H_

#include <windows.h>

#include <cstdint>

namespace crossdesk {

// ptScreenPos is the input hotspot; DrawIconEx expects the bitmap's top-left.
inline bool CursorIntersectsCapture(const CURSORINFO& cursor, int left, int top,
                                    int width, int height, POINT* origin = nullptr) {
  if (!(cursor.flags & CURSOR_SHOWING) || !cursor.hCursor) return false;
  struct CursorIconInfo : ICONINFO {
    CursorIconInfo() : ICONINFO{} {}
    ~CursorIconInfo() {
      if (hbmColor) DeleteObject(hbmColor);
      if (hbmMask) DeleteObject(hbmMask);
    }
  } icon;
  if (!GetIconInfo(cursor.hCursor, &icon)) return false;
  BITMAP bitmap{};
  if (!GetObject(icon.hbmColor ? icon.hbmColor : icon.hbmMask, sizeof(bitmap),
                 &bitmap))
    return false;
  const int cursor_width = bitmap.bmWidth;
  const int cursor_height =
      icon.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2;
  const int64_t x =
      static_cast<int64_t>(cursor.ptScreenPos.x) - left - icon.xHotspot;
  const int64_t y =
      static_cast<int64_t>(cursor.ptScreenPos.y) - top - icon.yHotspot;
  if (x >= width || y >= height || x + cursor_width <= 0 ||
      y + cursor_height <= 0)
    return false;
  if (origin) *origin = {static_cast<LONG>(x), static_cast<LONG>(y)};
  return true;
}

inline bool DrawCursorInCapture(HDC dc, const CURSORINFO& cursor, int left,
                                int top, int width, int height) {
  POINT origin{};
  return CursorIntersectsCapture(cursor, left, top, width, height, &origin) &&
         DrawIconEx(dc, origin.x, origin.y, cursor.hCursor, 0, 0, 0, nullptr,
                    DI_NORMAL) != FALSE;
}

}  // namespace crossdesk

#endif