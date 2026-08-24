#include "nv12_scaler.h"

#include <cstddef>
#include <limits>

#include "libyuv/planar_functions.h"

namespace crossdesk {

int ScaleNv12ViaI420(const uint8_t* src_y, int src_stride_y,
                     const uint8_t* src_uv, int src_stride_uv, int src_width,
                     int src_height, uint8_t* dst_y, int dst_stride_y,
                     uint8_t* dst_uv, int dst_stride_uv, int dst_width,
                     int dst_height, libyuv::FilterMode filtering,
                     std::vector<uint8_t>* scratch_buffer) {
  if (!src_y || !src_uv || !dst_y || !dst_uv || !scratch_buffer ||
      src_width <= 0 || src_height <= 0 || dst_width <= 0 || dst_height <= 0 ||
      (src_width & 1) != 0 || (src_height & 1) != 0 || (dst_width & 1) != 0 ||
      (dst_height & 1) != 0) {
    return -1;
  }

  const int src_chroma_width = src_width / 2;
  const int src_chroma_height = src_height / 2;
  const int dst_chroma_width = dst_width / 2;
  const int dst_chroma_height = dst_height / 2;
  const size_t src_chroma_size =
      static_cast<size_t>(src_chroma_width) * src_chroma_height;
  const size_t dst_chroma_size =
      static_cast<size_t>(dst_chroma_width) * dst_chroma_height;
  const size_t max_size = std::numeric_limits<size_t>::max();
  if (src_chroma_size > max_size / 2 || dst_chroma_size > max_size / 2) {
    return -1;
  }

  const size_t src_planes_size = 2 * src_chroma_size;
  const size_t dst_planes_size = 2 * dst_chroma_size;
  if (src_planes_size > max_size - dst_planes_size) {
    return -1;
  }
  scratch_buffer->resize(src_planes_size + dst_planes_size);
  uint8_t* src_u = scratch_buffer->data();
  uint8_t* src_v = src_u + src_chroma_size;
  uint8_t* dst_u = src_v + src_chroma_size;
  uint8_t* dst_v = dst_u + dst_chroma_size;

  libyuv::SplitUVPlane(src_uv, src_stride_uv, src_u, src_chroma_width, src_v,
                       src_chroma_width, src_chroma_width, src_chroma_height);
  const int result = libyuv::I420Scale(
      src_y, src_stride_y, src_u, src_chroma_width, src_v, src_chroma_width,
      src_width, src_height, dst_y, dst_stride_y, dst_u, dst_chroma_width,
      dst_v, dst_chroma_width, dst_width, dst_height, filtering);
  if (result != 0) {
    return result;
  }

  libyuv::MergeUVPlane(dst_u, dst_chroma_width, dst_v, dst_chroma_width, dst_uv,
                       dst_stride_uv, dst_chroma_width, dst_chroma_height);
  return 0;
}

}  // namespace crossdesk
