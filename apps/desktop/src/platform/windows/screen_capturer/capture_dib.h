/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-28
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CAPTURE_DIB_H_
#define _CAPTURE_DIB_H_

#include <windows.h>
#include <cstdint>

namespace crossdesk {

class CaptureDib {
 public:
  CaptureDib() = default;
  ~CaptureDib() { Reset(); }
  CaptureDib(const CaptureDib&) = delete;
  CaptureDib& operator=(const CaptureDib&) = delete;

  HDC dc() const { return dc_; }
  uint8_t* bits() const { return static_cast<uint8_t*>(bits_); }

  bool EnsureSize(int width, int height, HDC compatible = nullptr) {
    if (bits_ && width_ == width && height_ == height) return true;
    Reset();
    dc_ = CreateCompatibleDC(compatible);
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

 private:
  HDC dc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ previous_ = nullptr;
  void* bits_ = nullptr;
  int width_ = 0;
  int height_ = 0;
};

}  // namespace crossdesk

#endif
