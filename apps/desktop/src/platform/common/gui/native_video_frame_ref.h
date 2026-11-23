/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-07
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _NATIVE_VIDEO_FRAME_REF_H_
#define _NATIVE_VIDEO_FRAME_REF_H_

#include <limits>
#include <utility>

#include "minirtc.h"

namespace crossdesk {

class NativeVideoFrameRef {
public:
  NativeVideoFrameRef() = default;

  explicit NativeVideoFrameRef(const MiniRtcNativeVideoFrame *frame) {
    if (frame && frame->owner && frame->retain && frame->release) {
      frame_ = *frame;
      frame_.struct_size = sizeof(frame_);
      frame_.retain(frame_.owner);
      valid_ = true;
    }
  }

  NativeVideoFrameRef(const NativeVideoFrameRef &other)
      : NativeVideoFrameRef(other.Get()) {}

  NativeVideoFrameRef(NativeVideoFrameRef &&other) noexcept
      : frame_(other.frame_), valid_(std::exchange(other.valid_, false)) {
    other.frame_ = {};
  }

  NativeVideoFrameRef &operator=(NativeVideoFrameRef other) noexcept {
    Swap(other);
    return *this;
  }

  ~NativeVideoFrameRef() { Reset(); }

  void Reset() {
    if (valid_ && frame_.owner && frame_.release) {
      frame_.release(frame_.owner);
    }
    frame_ = {};
    valid_ = false;
  }

  void Swap(NativeVideoFrameRef &other) noexcept {
    std::swap(frame_, other.frame_);
    std::swap(valid_, other.valid_);
  }

  const MiniRtcNativeVideoFrame *Get() const { return valid_ ? &frame_ : nullptr; }
  explicit operator bool() const { return valid_; }

private:
  MiniRtcNativeVideoFrame frame_{};
  bool valid_ = false;
};

inline const MiniRtcNativeVideoFrame *GetOpenGlNativeFrame(
    const MiniRtcNativeVideoFrame &frame) {
  const MiniRtcNativeVideoFrame *native = &frame;
  if (native->struct_size < static_cast<uint32_t>(sizeof(MiniRtcNativeVideoFrame)) ||
      !native->owner || !native->retain || !native->release ||
      !native->copy_to_nv12 || native->width == 0 || native->height == 0 ||
      (native->width & 1U) != 0 || (native->height & 1U) != 0 ||
      native->width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
      native->height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    return nullptr;
  }
  if (native->type == MiniRtcNativeVideoFrameCpuNv12) {
    return native->payload.cpu_nv12.y_plane &&
                   native->payload.cpu_nv12.uv_plane &&
                   native->payload.cpu_nv12.y_stride >= native->width &&
                   native->payload.cpu_nv12.uv_stride >= native->width
               ? native
               : nullptr;
  }
  if (native->type == MiniRtcNativeVideoFrameCudaNv12) {
    return native->payload.cuda_nv12.y_device_pointer != 0 &&
                   native->payload.cuda_nv12.uv_device_pointer != 0 &&
                   native->payload.cuda_nv12.y_stride >= native->width &&
                   native->payload.cuda_nv12.uv_stride >= native->width &&
                   native->payload.cuda_nv12.context
               ? native
               : nullptr;
  }
  return nullptr;
}

}  // namespace crossdesk

#endif
