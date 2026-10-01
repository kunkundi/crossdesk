/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-02
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VIDEO_PICTURE_H_
#define _VIDEO_PICTURE_H_

#include <minirtc.h>
#include <climits>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

namespace crossdesk::android_controller {
struct VideoPicture {
  uint32_t width, height;
  uint64_t captured_us, received_us, generation;
  std::vector<uint8_t> pixels;
  std::shared_ptr<void> native_owner;
  MiniRtcCpuNv12Frame planes{};
  MiniRtcAndroidMediaCodecFrame media_codec{};

  bool IsSurface() const { return media_codec.render && native_owner; }
  int RenderSurface(void* window) const {
    return IsSurface() ? media_codec.render(native_owner.get(), window) : -1;
  }

  const uint8_t* Y() const { return native_owner ? planes.y_plane : pixels.data(); }
  const uint8_t* UV() const { return native_owner ? planes.uv_plane : pixels.data() + size_t(width) * height; }
  int YStride() const { return native_owner ? planes.y_stride : width; }
  int UVStride() const { return native_owner ? planes.uv_stride : width; }

  static std::optional<VideoPicture> RetainOrCopy(const MiniRtcVideoFrame& frame,
                                                uint64_t generation) {
    const auto w = frame.width, h = frame.height;
    if (!w || !h || w > 8192 || h > 8192 || (w & 1) || (h & 1) ||
        static_cast<uint64_t>(w) * h > 33554432) return std::nullopt;
    VideoPicture result{w, h, frame.captured_timestamp, frame.received_timestamp,
                        generation, {}, {}, {}, {}};
    const auto* native = frame.native_frame;
    const bool valid_native = native && native->struct_size >= sizeof(*native) &&
        native->width == w && native->height == h && native->owner;
    if (valid_native && native->type == MiniRtcNativeVideoFrameAndroidMediaCodec &&
        native->retain && native->release && native->payload.android_media_codec.render) {
      native->retain(native->owner);
      result.native_owner = std::shared_ptr<void>(native->owner, native->release);
      result.media_codec = native->payload.android_media_codec;
      return result;
    }
    if (valid_native && native->type == MiniRtcNativeVideoFrameCpuNv12 &&
        native->retain && native->release) {
      const auto& planes = native->payload.cpu_nv12;
      if (planes.y_plane && planes.uv_plane && planes.y_stride >= w &&
          planes.uv_stride >= w && planes.y_stride <= INT_MAX && planes.uv_stride <= INT_MAX) {
        native->retain(native->owner);
        result.native_owner = std::shared_ptr<void>(native->owner, native->release);
        result.planes = planes;
        return result;
      }
    }
    const size_t size = size_t(w) * h * 3 / 2;
    if (frame.data && frame.size >= size) {
      result.pixels.resize(size);
      std::memcpy(result.pixels.data(), frame.data, size);
    } else if (valid_native && native->copy_to_nv12) {
      result.pixels.resize(size);
      if (native->copy_to_nv12(native->owner, result.pixels.data(), size) != 0)
        return std::nullopt;
    } else {
      return std::nullopt;
    }
    return result;
  }
};
}  // namespace crossdesk::android_controller

#endif