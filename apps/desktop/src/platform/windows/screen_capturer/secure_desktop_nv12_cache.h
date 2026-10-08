/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-08
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SECURE_DESKTOP_NV12_CACHE_H_
#define _SECURE_DESKTOP_NV12_CACHE_H_

#include <libyuv.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace crossdesk {

// One cache per capture desktop/resource lifetime. DXGI can report a pointer
// update or timeout without changing any pixels. Keep publishing those frames
// (and fresh cursor metadata) without converting the entire desktop again.
class SecureDesktopNv12Cache {
 public:
  bool Convert(const uint8_t* pixels, int stride, int width, int height,
               bool pixels_changed) {
    updated_ = false;
    if (!pixels || width <= 0 || height <= 0 || (width & 1) || (height & 1) ||
        stride < static_cast<int64_t>(width) * 4) {
      valid_ = false;
      return false;
    }
    if (valid_ && !pixels_changed && width == width_ && height == height_)
      return true;

    valid_ = false;
    const size_t luma_size = static_cast<size_t>(width) * height;
    pixels_.resize(luma_size + luma_size / 2);
    if (libyuv::ARGBToNV12(pixels, stride, pixels_.data(), width,
                           pixels_.data() + luma_size, width, width,
                           height) != 0)
      return false;
    width_ = width;
    height_ = height;
    valid_ = updated_ = true;
    return true;
  }

  bool updated() const { return updated_; }
  const std::vector<uint8_t>& pixels() const { return pixels_; }

 private:
  bool valid_ = false;
  bool updated_ = false;
  int width_ = 0, height_ = 0;
  std::vector<uint8_t> pixels_;
};

}  // namespace crossdesk

#endif  // _SECURE_DESKTOP_NV12_CACHE_H_
