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

#include "capture_dib.h"
#include "cursor_draw.h"

namespace crossdesk {

// Keep the source texture free of cursors. Each draw starts from the original
// pixels, including when a static desktop is replayed after a cursor toggle.
class CursorFrameCompositor {
 public:
  const uint8_t* Draw(const uint8_t* source, int stride, int width, int height,
                      const CURSORINFO& cursor, int left, int top) {
    if (!source || width <= 0 || height <= 0 || !surface_.EnsureSize(width, height))
      return nullptr;
    const size_t row_bytes = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
      std::memcpy(surface_.bits() + y * row_bytes,
                  source + static_cast<size_t>(y) * stride, row_bytes);
    }
    const bool drawn =
        DrawCursorInCapture(surface_.dc(), cursor, left, top, width, height);
    GdiFlush();
    return drawn ? surface_.bits() : nullptr;
  }

 private:
  CaptureDib surface_;
};

}  // namespace crossdesk

#endif
