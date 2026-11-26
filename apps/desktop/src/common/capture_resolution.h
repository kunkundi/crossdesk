#ifndef CROSSDESK_COMMON_CAPTURE_RESOLUTION_H_
#define CROSSDESK_COMMON_CAPTURE_RESOLUTION_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace crossdesk {

// Hardware H.264 encoders and decoders (including NVENC H.264 and NVDEC) cap
// frame width and height at 4096 pixels. Clamping oversized desktop captures
// to this envelope keeps hardware zero-copy and decode paths operational while
// preserving the desktop coordinate space in DisplayInfo for pointer input.
inline constexpr int kMaxCaptureEncodeWidth = 4096;
inline constexpr int kMaxCaptureEncodeHeight = 4096;

struct CaptureResolution {
  int width = 0;
  int height = 0;

  constexpr bool valid() const {
    return width >= 2 && height >= 2 && ((width | height) & 1) == 0;
  }

  constexpr bool operator==(const CaptureResolution& other) const {
    return width == other.width && height == other.height;
  }

  constexpr bool operator!=(const CaptureResolution& other) const {
    return !(*this == other);
  }
};

constexpr bool IsOversizedCaptureResolution(
    int width, int height, int max_width = kMaxCaptureEncodeWidth,
    int max_height = kMaxCaptureEncodeHeight) {
  if (width <= 0 || height <= 0 || max_width < 2 || max_height < 2) {
    return false;
  }
  return width > (max_width & ~1) || height > (max_height & ~1);
}

inline CaptureResolution ClampCaptureResolution(
    int source_width, int source_height, int max_width = kMaxCaptureEncodeWidth,
    int max_height = kMaxCaptureEncodeHeight) {
  if (source_width < 2 || source_height < 2 || max_width < 2 ||
      max_height < 2) {
    return {};
  }

  const int max_even_width = max_width & ~1;
  const int max_even_height = max_height & ~1;
  if (max_even_width < 2 || max_even_height < 2) {
    return {};
  }

  if (source_width <= max_even_width && source_height <= max_even_height) {
    const int even_width = source_width & ~1;
    const int even_height = source_height & ~1;
    if (even_width < 2 || even_height < 2) {
      return {};
    }
    return {even_width, even_height};
  }

  if (static_cast<int64_t>(source_width) * max_even_height >=
      static_cast<int64_t>(source_height) * max_even_width) {
    const int target_width = max_even_width;
    const int64_t rounded_half =
        (static_cast<int64_t>(source_height) * target_width + source_width) /
        (static_cast<int64_t>(2) * source_width);
    const int target_height = static_cast<int>(
        std::clamp<int64_t>(rounded_half * 2, 2, max_even_height));
    return {target_width, target_height};
  }

  const int target_height = max_even_height;
  const int64_t rounded_half =
      (static_cast<int64_t>(source_width) * target_height + source_height) /
      (static_cast<int64_t>(2) * source_height);
  const int target_width = static_cast<int>(
      std::clamp<int64_t>(rounded_half * 2, 2, max_even_width));
  return {target_width, target_height};
}

inline bool TryGetNv12FrameSize(int width, int height, size_t* size_out) {
  if (!size_out || width < 2 || height < 2 || ((width | height) & 1) != 0) {
    return false;
  }
  const size_t w = static_cast<size_t>(width);
  const size_t h = static_cast<size_t>(height);
  const size_t max_size = (std::numeric_limits<size_t>::max)();
  if (w > max_size / h) {
    return false;
  }
  const size_t luma_size = w * h;
  const size_t chroma_size = luma_size / 2U;
  if (luma_size > max_size - chroma_size) {
    return false;
  }
  *size_out = luma_size + chroma_size;
  return true;
}

struct Nv12PlaneView {
  const uint8_t* y_plane = nullptr;
  int y_stride = 0;
  const uint8_t* uv_plane = nullptr;
  int uv_stride = 0;
  int width = 0;
  int height = 0;
};

struct ScaledNv12FrameView {
  const uint8_t* data = nullptr;
  size_t size = 0;
  int width = 0;
  int height = 0;
  bool downscaled = false;
};

class OversizedNv12FrameScaler {
 public:
  void Reset() {
    source_nv12_.clear();
    scaled_nv12_.clear();
    scratch_.clear();
  }

  template <typename ScaleFn>
  bool ScalePlanes(const Nv12PlaneView& planes, ScaleFn&& scale_fn,
                   ScaledNv12FrameView* out,
                   int max_width = kMaxCaptureEncodeWidth,
                   int max_height = kMaxCaptureEncodeHeight) {
    if (!out || !planes.y_plane || !planes.uv_plane || planes.width < 2 ||
        planes.height < 2 || ((planes.width | planes.height) & 1) != 0 ||
        planes.y_stride < planes.width || planes.uv_stride < planes.width) {
      return false;
    }

    size_t src_bytes = 0;
    if (!TryGetNv12FrameSize(planes.width, planes.height, &src_bytes)) {
      return false;
    }

    const CaptureResolution target = ClampCaptureResolution(
        planes.width, planes.height, max_width, max_height);
    if (!target.valid()) {
      return false;
    }

    const size_t src_luma =
        static_cast<size_t>(planes.width) * static_cast<size_t>(planes.height);
    if (target.width == planes.width && target.height == planes.height &&
        planes.y_stride == planes.width && planes.uv_stride == planes.width &&
        planes.uv_plane == planes.y_plane + src_luma) {
      *out = {planes.y_plane, src_bytes, planes.width, planes.height, false};
      return true;
    }

    size_t dst_bytes = 0;
    if (!TryGetNv12FrameSize(target.width, target.height, &dst_bytes)) {
      return false;
    }

    scaled_nv12_.resize(dst_bytes);
    uint8_t* dst_y = scaled_nv12_.data();
    uint8_t* dst_uv = dst_y + static_cast<size_t>(target.width) *
                                  static_cast<size_t>(target.height);
    if (scale_fn(planes.y_plane, planes.y_stride, planes.uv_plane,
                 planes.uv_stride, planes.width, planes.height, dst_y,
                 target.width, dst_uv, target.width, target.width,
                 target.height, &scratch_) != 0) {
      return false;
    }

    *out = {scaled_nv12_.data(), dst_bytes, target.width, target.height,
            target.width != planes.width || target.height != planes.height};
    return true;
  }

  template <typename ScaleFn>
  bool ScalePacked(const uint8_t* data, size_t size, int width, int height,
                   ScaleFn&& scale_fn, ScaledNv12FrameView* out,
                   int max_width = kMaxCaptureEncodeWidth,
                   int max_height = kMaxCaptureEncodeHeight) {
    size_t src_bytes = 0;
    if (!data || !TryGetNv12FrameSize(width, height, &src_bytes) ||
        size < src_bytes) {
      return false;
    }
    const size_t luma_bytes =
        static_cast<size_t>(width) * static_cast<size_t>(height);
    return ScalePlanes({data, width, data + luma_bytes, width, width, height},
                       std::forward<ScaleFn>(scale_fn), out, max_width,
                       max_height);
  }

  template <typename CopyFn, typename ScaleFn>
  bool MaterializeAndScale(int width, int height, CopyFn&& copy_fn,
                           ScaleFn&& scale_fn, ScaledNv12FrameView* out,
                           int max_width = kMaxCaptureEncodeWidth,
                           int max_height = kMaxCaptureEncodeHeight) {
    size_t src_bytes = 0;
    if (!TryGetNv12FrameSize(width, height, &src_bytes)) {
      return false;
    }
    source_nv12_.resize(src_bytes);
    if (copy_fn(source_nv12_.data(), src_bytes) != 0) {
      return false;
    }
    return ScalePacked(source_nv12_.data(), src_bytes, width, height,
                       std::forward<ScaleFn>(scale_fn), out, max_width,
                       max_height);
  }

 private:
  std::vector<uint8_t> source_nv12_;
  std::vector<uint8_t> scaled_nv12_;
  std::vector<uint8_t> scratch_;
};

}  // namespace crossdesk

#endif  // CROSSDESK_COMMON_CAPTURE_RESOLUTION_H_
