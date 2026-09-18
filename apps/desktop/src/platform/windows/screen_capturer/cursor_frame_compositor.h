/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-18
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CURSOR_FRAME_COMPOSITOR_H_
#define _CURSOR_FRAME_COMPOSITOR_H_

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "cursor_draw.h"

namespace crossdesk {

// Keep the source texture free of cursors. Each draw starts from the original
// pixels, including when a static desktop is replayed after a cursor toggle.
class CursorFrameCompositor {
 public:
  CursorFrameCompositor() = default;
  ~CursorFrameCompositor() { Reset(); }
  CursorFrameCompositor(const CursorFrameCompositor&) = delete;
  CursorFrameCompositor& operator=(const CursorFrameCompositor&) = delete;

  const uint8_t* Draw(const uint8_t* source, int stride, int width, int height,
                      const CURSORINFO& cursor, int left, int top) {
    if (!source || width <= 0 || height <= 0 || !EnsureSurface(width, height))
      return nullptr;
    const size_t row_bytes = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
      std::memcpy(static_cast<uint8_t*>(bits_) + y * row_bytes,
                  source + static_cast<size_t>(y) * stride, row_bytes);
    }
    const bool drawn =
        DrawCursorInCapture(dc_, cursor, left, top, width, height);
    GdiFlush();
    return drawn ? static_cast<const uint8_t*>(bits_) : nullptr;
  }

 private:
  bool EnsureSurface(int width, int height) {
    if (bits_ && width_ == width && height_ == height) return true;
    Reset();
    dc_ = CreateCompatibleDC(nullptr);
    if (!dc_) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
    if (!bitmap_ || !bits_) {
      Reset();
      return false;
    }
    previous_ = SelectObject(dc_, bitmap_);
    if (!previous_ || previous_ == HGDI_ERROR) {
      previous_ = nullptr;
      Reset();
      return false;
    }
    width_ = width;
    height_ = height;
    return true;
  }

  void Reset() {
    if (dc_ && previous_) SelectObject(dc_, previous_);
    if (bitmap_) DeleteObject(bitmap_);
    if (dc_) DeleteDC(dc_);
    dc_ = nullptr;
    bitmap_ = nullptr;
    previous_ = nullptr;
    bits_ = nullptr;
    width_ = height_ = 0;
  }

  HDC dc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ previous_ = nullptr;
  void* bits_ = nullptr;
  int width_ = 0;
  int height_ = 0;
};

}  // namespace crossdesk

#endif
