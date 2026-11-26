#ifndef CROSSDESK_COMMON_NV12_SCALER_H_
#define CROSSDESK_COMMON_NV12_SCALER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "capture_resolution.h"
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

using CopyNativeNv12Fn = int (*)(void* owner, uint8_t* destination,
                                 size_t destination_size);

bool DownscaleOversizedNv12Packed(OversizedNv12FrameScaler* scaler,
                                  const uint8_t* data, size_t size, int width,
                                  int height, ScaledNv12FrameView* out);

bool DownscaleOversizedNv12Planes(OversizedNv12FrameScaler* scaler,
                                  const Nv12PlaneView& planes,
                                  ScaledNv12FrameView* out);

bool MaterializeAndDownscaleOversizedNv12(OversizedNv12FrameScaler* scaler,
                                          int width, int height,
                                          CopyNativeNv12Fn copy_to_nv12,
                                          void* owner,
                                          ScaledNv12FrameView* out);

}  // namespace crossdesk

#endif  // CROSSDESK_COMMON_NV12_SCALER_H_
