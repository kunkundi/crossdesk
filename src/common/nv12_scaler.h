#ifndef CROSSDESK_COMMON_NV12_SCALER_H_
#define CROSSDESK_COMMON_NV12_SCALER_H_

#include <cstdint>
#include <vector>

#include "libyuv/scale.h"

namespace crossdesk {

// Scales NV12 by separating the interleaved chroma plane before filtering.
// This avoids the downward chroma rounding bias in libyuv::NV12Scale while
// preserving the requested filtering quality.
int ScaleNv12ViaI420(const uint8_t* src_y, int src_stride_y,
                     const uint8_t* src_uv, int src_stride_uv, int src_width,
                     int src_height, uint8_t* dst_y, int dst_stride_y,
                     uint8_t* dst_uv, int dst_stride_uv, int dst_width,
                     int dst_height, libyuv::FilterMode filtering,
                     std::vector<uint8_t>* scratch_buffer);

}  // namespace crossdesk

#endif  // CROSSDESK_COMMON_NV12_SCALER_H_
