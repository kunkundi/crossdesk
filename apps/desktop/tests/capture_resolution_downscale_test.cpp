#ifdef _WIN32
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#endif

// Included before undefining Windows min/max macros to verify macro immunity.
#include "capture_resolution.h"

#ifdef _WIN32
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <remote_action.h>

#include "cursor_position.h"
#include "display_info.h"
#include "rendering/video_geometry.h"

namespace {

bool Expect(bool condition, const char* message) {
  if (condition) return true;
  std::cerr << "FAIL: " << message << '\n';
  return false;
}

bool ExpectResolution(const crossdesk::CaptureResolution& actual,
                      int expected_w, int expected_h, const char* label) {
  if (actual.width == expected_w && actual.height == expected_h) {
    return true;
  }
  std::cerr << "FAIL: " << label << " expected " << expected_w << 'x'
            << expected_h << ", got " << actual.width << 'x' << actual.height
            << '\n';
  return false;
}

int ReferenceBoxScaleNv12(const uint8_t* src_y, int src_stride_y,
                          const uint8_t* src_uv, int src_stride_uv,
                          int src_width, int src_height, uint8_t* dst_y,
                          int dst_stride_y, uint8_t* dst_uv, int dst_stride_uv,
                          int dst_width, int dst_height,
                          std::vector<uint8_t>* scratch) {
  if (!src_y || !src_uv || !dst_y || !dst_uv || !scratch || src_width < 2 ||
      src_height < 2 || dst_width < 2 || dst_height < 2 ||
      ((src_width | src_height | dst_width | dst_height) & 1) != 0) {
    return -1;
  }
  scratch->resize(static_cast<size_t>(dst_width));

  for (int dy = 0; dy < dst_height; ++dy) {
    const int sy0 = static_cast<int>(
        (static_cast<int64_t>(dy) * src_height) / dst_height);
    const int sy1 = (std::max)(
        sy0 + 1,
        static_cast<int>(((static_cast<int64_t>(dy) + 1) * src_height) /
                         dst_height));
    for (int dx = 0; dx < dst_width; ++dx) {
      const int sx0 = static_cast<int>(
          (static_cast<int64_t>(dx) * src_width) / dst_width);
      const int sx1 = (std::max)(
          sx0 + 1,
          static_cast<int>(((static_cast<int64_t>(dx) + 1) * src_width) /
                           dst_width));
      uint64_t sum = 0;
      uint64_t count = 0;
      for (int sy = sy0; sy < (std::min)(sy1, src_height); ++sy) {
        const uint8_t* row = src_y + static_cast<size_t>(sy) * src_stride_y;
        for (int sx = sx0; sx < (std::min)(sx1, src_width); ++sx) {
          sum += row[sx];
          ++count;
        }
      }
      dst_y[static_cast<size_t>(dy) * dst_stride_y + dx] =
          static_cast<uint8_t>((sum + count / 2) / count);
    }
  }

  const int src_cw = src_width / 2;
  const int src_ch = src_height / 2;
  const int dst_cw = dst_width / 2;
  const int dst_ch = dst_height / 2;
  for (int dy = 0; dy < dst_ch; ++dy) {
    const int sy0 =
        static_cast<int>((static_cast<int64_t>(dy) * src_ch) / dst_ch);
    const int sy1 = (std::max)(
        sy0 + 1,
        static_cast<int>(((static_cast<int64_t>(dy) + 1) * src_ch) / dst_ch));
    for (int dx = 0; dx < dst_cw; ++dx) {
      const int sx0 =
          static_cast<int>((static_cast<int64_t>(dx) * src_cw) / dst_cw);
      const int sx1 = (std::max)(
          sx0 + 1,
          static_cast<int>(((static_cast<int64_t>(dx) + 1) * src_cw) / dst_cw));
      uint64_t sum_u = 0;
      uint64_t sum_v = 0;
      uint64_t count = 0;
      for (int sy = sy0; sy < (std::min)(sy1, src_ch); ++sy) {
        const uint8_t* row = src_uv + static_cast<size_t>(sy) * src_stride_uv;
        for (int sx = sx0; sx < (std::min)(sx1, src_cw); ++sx) {
          sum_u += row[sx * 2];
          sum_v += row[sx * 2 + 1];
          ++count;
        }
      }
      uint8_t* dst = dst_uv + static_cast<size_t>(dy) * dst_stride_uv + dx * 2;
      dst[0] = static_cast<uint8_t>((sum_u + count / 2) / count);
      dst[1] = static_cast<uint8_t>((sum_v + count / 2) / count);
    }
  }
  return 0;
}

void FillQuadrantNv12(uint8_t* y_plane, int y_stride, uint8_t* uv_plane,
                      int uv_stride, int width, int height) {
  const int half_w = width / 2;
  const int half_h = height / 2;
  for (int y = 0; y < height; ++y) {
    uint8_t* row = y_plane + static_cast<size_t>(y) * y_stride;
    for (int x = 0; x < width; ++x) {
      const int quad = (y >= half_h ? 2 : 0) + (x >= half_w ? 1 : 0);
      row[x] = static_cast<uint8_t>(40 + quad * 50);
    }
  }
  for (int cy = 0; cy < height / 2; ++cy) {
    uint8_t* row = uv_plane + static_cast<size_t>(cy) * uv_stride;
    for (int cx = 0; cx < width / 2; ++cx) {
      const int quad = (cy >= half_h / 2 ? 2 : 0) + (cx >= half_w / 2 ? 1 : 0);
      row[cx * 2] = static_cast<uint8_t>(60 + quad * 40);
      row[cx * 2 + 1] = static_cast<uint8_t>(200 - quad * 40);
    }
  }
}

bool VerifyQuadrantNv12(const uint8_t* data, int width, int height,
                        const char* label) {
  const uint8_t* uv = data + static_cast<size_t>(width) * height;
  const int sample_x[2] = {width / 4, (width * 3) / 4};
  const int sample_y[2] = {height / 4, (height * 3) / 4};
  bool ok = true;
  for (int qy = 0; qy < 2; ++qy) {
    for (int qx = 0; qx < 2; ++qx) {
      const int quad = qy * 2 + qx;
      const uint8_t expected_y = static_cast<uint8_t>(40 + quad * 50);
      const uint8_t expected_u = static_cast<uint8_t>(60 + quad * 40);
      const uint8_t expected_v = static_cast<uint8_t>(200 - quad * 40);
      const int px = sample_x[qx];
      const int py = sample_y[qy];
      const uint8_t actual_y = data[static_cast<size_t>(py) * width + px];
      const size_t uv_index =
          static_cast<size_t>(py / 2) * width + static_cast<size_t>(px / 2) * 2;
      const uint8_t actual_u = uv[uv_index];
      const uint8_t actual_v = uv[uv_index + 1];
      ok &= Expect(actual_y == expected_y && actual_u == expected_u &&
                       actual_v == expected_v,
                   label);
    }
  }
  return ok;
}

bool TestResolutionClamping() {
  using crossdesk::ClampCaptureResolution;
  using crossdesk::IsOversizedCaptureResolution;
  using crossdesk::TryGetNv12FrameSize;

  bool ok = true;
  ok &= Expect(!ClampCaptureResolution(0, 0).valid(), "0x0 invalid");
  ok &= Expect(!ClampCaptureResolution(-1920, 1080).valid(), "negative invalid");
  ok &= Expect(!ClampCaptureResolution(1, 1080).valid(), "1px width invalid");
  ok &= Expect(!ClampCaptureResolution(1920, 1).valid(), "1px height invalid");
  ok &= Expect(!ClampCaptureResolution(1920, 1080, 1, 4096).valid(),
               "1px max_width invalid");
  ok &= Expect(!IsOversizedCaptureResolution(0, 0), "0x0 not oversized");
  ok &= Expect(!IsOversizedCaptureResolution(3840, 2160),
               "4K UHD not oversized");
  ok &= Expect(!IsOversizedCaptureResolution(4096, 2160),
               "DCI 4K not oversized");
  ok &= Expect(!IsOversizedCaptureResolution(4096, 4096),
               "4096x4096 boundary not oversized");
  ok &= Expect(IsOversizedCaptureResolution(4098, 2160), "4098x2160 oversized");
  ok &= Expect(IsOversizedCaptureResolution(2160, 4098), "2160x4098 oversized");
  ok &= Expect(IsOversizedCaptureResolution(5120, 1440), "5120x1440 oversized");
  ok &= Expect(IsOversizedCaptureResolution(6016, 3384), "6016x3384 oversized");

  // Standard and odd <= 4096 resolutions stay unscaled (even-aligned).
  ok &= ExpectResolution(ClampCaptureResolution(1920, 1080), 1920, 1080,
                         "1080p");
  ok &= ExpectResolution(ClampCaptureResolution(2560, 1440), 2560, 1440,
                         "1440p");
  ok &= ExpectResolution(ClampCaptureResolution(3840, 2160), 3840, 2160,
                         "4K UHD");
  ok &= ExpectResolution(ClampCaptureResolution(4096, 2160), 4096, 2160,
                         "DCI 4K");
  ok &= ExpectResolution(ClampCaptureResolution(4096, 4096), 4096, 4096,
                         "4096 square");
  ok &= ExpectResolution(ClampCaptureResolution(1919, 1079), 1918, 1078,
                         "odd <= 4096 aligned down");

  // Issue #300 real-world oversized displays must preserve exact aspect ratio
  // without IEEE-754 float truncation (e.g. 6016x3384 -> 4096x2304, not 4094).
  ok &= ExpectResolution(ClampCaptureResolution(6016, 3384), 4096, 2304,
                         "6K Pro Display XDR 16:9");
  ok &= ExpectResolution(ClampCaptureResolution(5120, 1440), 4096, 1152,
                         "32:9 Super Ultrawide");
  ok &= ExpectResolution(ClampCaptureResolution(5120, 2880), 4096, 2304,
                         "5K Studio Display 16:9");
  ok &= ExpectResolution(ClampCaptureResolution(5120, 2160), 4096, 1728,
                         "5K2K 21:9 Ultrawide");
  ok &= ExpectResolution(ClampCaptureResolution(7680, 4320), 4096, 2304,
                         "8K UHD 16:9");
  ok &= ExpectResolution(ClampCaptureResolution(7680, 2160), 4096, 1152,
                         "Dual 4K 32:9");
  ok &= ExpectResolution(ClampCaptureResolution(4500, 3000), 4096, 2730,
                         "Surface Studio 3:2");

  // Rotated / portrait oversized displays.
  ok &= ExpectResolution(ClampCaptureResolution(3384, 6016), 2304, 4096,
                         "Portrait 6K 9:16");
  ok &= ExpectResolution(ClampCaptureResolution(1440, 5120), 1152, 4096,
                         "Portrait 9:32");
  ok &= ExpectResolution(ClampCaptureResolution(2880, 5120), 2304, 4096,
                         "Portrait 5K 9:16");

  // Extreme aspect ratios and INT_MAX overflow safety.
  ok &= ExpectResolution(ClampCaptureResolution(16384, 4), 4096, 2,
                         "extreme wide minimum even height");
  ok &= ExpectResolution(ClampCaptureResolution(4, 16384), 2, 4096,
                         "extreme tall minimum even width");
  ok &= ExpectResolution(
      ClampCaptureResolution((std::numeric_limits<int>::max)(),
                             (std::numeric_limits<int>::max)()),
      4096, 4096, "INT_MAX square overflow safety");

  size_t nv12_bytes = 0;
  ok &= Expect(TryGetNv12FrameSize(4096, 2304, &nv12_bytes) &&
                   nv12_bytes == static_cast<size_t>(4096) * 2304 * 3 / 2,
               "NV12 byte size 4096x2304");
  ok &= Expect(!TryGetNv12FrameSize(4095, 2304, &nv12_bytes),
               "odd width NV12 size rejected");
  return ok;
}

bool TestOversizedNv12FrameScaler() {
  crossdesk::OversizedNv12FrameScaler scaler;
  bool ok = true;

  // 1. Non-oversized packed input passes through with zero copy.
  std::vector<uint8_t> hd_frame(static_cast<size_t>(640) * 360 * 3 / 2, 128);
  crossdesk::ScaledNv12FrameView view{};
  int scale_calls = 0;
  auto counting_scaler = [&](const uint8_t* sy, int ssy, const uint8_t* suv,
                             int ssuv, int sw, int sh, uint8_t* dy, int dsy,
                             uint8_t* duv, int dsuv, int dw, int dh,
                             std::vector<uint8_t>* scratch) {
    ++scale_calls;
    return ReferenceBoxScaleNv12(sy, ssy, suv, ssuv, sw, sh, dy, dsy, duv, dsuv,
                                 dw, dh, scratch);
  };
  ok &= Expect(scaler.ScalePacked(hd_frame.data(), hd_frame.size(), 640, 360,
                                  counting_scaler, &view),
               "640x360 pass-through succeeds");
  ok &= Expect(!view.downscaled && view.data == hd_frame.data() &&
                   view.width == 640 && view.height == 360 && scale_calls == 0,
               "non-oversized frame avoids copy and scale");

  // 2. Oversized packed 600x338? Reject odd; 960x540 clamped to max 640x640 ->
  // 640x360 preserves all four desktop quadrants (full-frame scale, no crop).
  ok &= Expect(!scaler.ScalePacked(hd_frame.data(), hd_frame.size(), 639, 360,
                                   counting_scaler, &view, 320, 320),
               "odd NV12 width rejected");
  const int src_w = 960;
  const int src_h = 540;
  std::vector<uint8_t> oversized_packed(
      static_cast<size_t>(src_w) * src_h * 3 / 2);
  FillQuadrantNv12(oversized_packed.data(), src_w,
                   oversized_packed.data() + static_cast<size_t>(src_w) * src_h,
                   src_w, src_w, src_h);
  ok &= Expect(scaler.ScalePacked(oversized_packed.data(),
                                  oversized_packed.size(), src_w, src_h,
                                  counting_scaler, &view, 640, 640),
               "oversized packed downscale succeeds");
  ok &= Expect(view.downscaled && view.width == 640 && view.height == 360 &&
                   view.size == static_cast<size_t>(640) * 360 * 3 / 2 &&
                   scale_calls == 1,
               "oversized packed clamped to 640x360");
  ok &= VerifyQuadrantNv12(view.data, view.width, view.height,
                           "packed downscale preserves all 4 quadrants");

  // Buffer address stays stable on subsequent frames of the same size.
  const uint8_t* first_buffer = view.data;
  ok &= Expect(scaler.ScalePacked(oversized_packed.data(),
                                  oversized_packed.size(), src_w, src_h,
                                  counting_scaler, &view, 640, 640) &&
                   view.data == first_buffer,
               "scaled buffer reused without reallocation");

  // 3. Strided Nv12PlaneView (MiniRtcCpuNv12Frame path).
  const int y_stride = src_w + 64;
  const int uv_stride = src_w + 128;
  std::vector<uint8_t> strided_y(static_cast<size_t>(y_stride) * src_h, 0);
  std::vector<uint8_t> strided_uv(static_cast<size_t>(uv_stride) * (src_h / 2),
                                  0);
  FillQuadrantNv12(strided_y.data(), y_stride, strided_uv.data(), uv_stride,
                   src_w, src_h);
  ok &= Expect(
      scaler.ScalePlanes({strided_y.data(), y_stride, strided_uv.data(),
                          uv_stride, src_w, src_h},
                         counting_scaler, &view, 640, 640),
      "strided plane downscale succeeds");
  ok &= VerifyQuadrantNv12(view.data, view.width, view.height,
                           "strided plane downscale preserves all 4 quadrants");

  // 4. MaterializeAndScale (CVPixelBuffer / opaque native_frame fallback).
  ok &= Expect(
      scaler.MaterializeAndScale(
          src_w, src_h,
          [&](uint8_t* dst, size_t bytes) {
            if (bytes != oversized_packed.size()) return -1;
            std::copy(oversized_packed.begin(), oversized_packed.end(), dst);
            return 0;
          },
          counting_scaler, &view, 640, 640),
      "MaterializeAndScale succeeds");
  ok &= VerifyQuadrantNv12(view.data, view.width, view.height,
                           "materialized native frame preserves quadrants");

  // Failure propagation when copy_to_nv12 fails.
  ok &= Expect(
      !scaler.MaterializeAndScale(
          src_w, src_h, [](uint8_t*, size_t) { return -1; }, counting_scaler,
          &view, 640, 640),
      "MaterializeAndScale propagates copy failure");
  return ok;
}

bool TestPointerAndCursorMappingZeroOffset() {
  struct DisplayCase {
    const char* name;
    int left;
    int top;
    int width;
    int height;
    float viewer_w;
    float viewer_h;
  };
  const DisplayCase cases[] = {
      {"6K 6016x3384 in 1920x1080 window", 0, 0, 6016, 3384, 1920.0f, 1080.0f},
      {"6K 6016x3384 letterboxed in 1920x1200", 0, 0, 6016, 3384, 1920.0f,
       1200.0f},
      {"32:9 5120x1440 letterboxed in 2560x1440", 0, 0, 5120, 1440, 2560.0f,
       1440.0f},
      {"32:9 5120x1440 secondary negative origin", -5120, -320, 5120, 1440,
       1920.0f, 1080.0f},
      {"Portrait 6K 3384x6016 pillarboxed in 1920x1080", 0, 0, 3384, 6016,
       1920.0f, 1080.0f},
      {"Surface 4500x3000 in 1440x900", 1920, 0, 4500, 3000, 1440.0f, 900.0f},
  };

  bool ok = true;
  for (const auto& tc : cases) {
    const crossdesk::DisplayInfo display(
        tc.name, tc.left, tc.top, tc.left + tc.width, tc.top + tc.height);
    const std::vector<crossdesk::DisplayInfo> displays = {display};
    const auto encoded =
        crossdesk::ClampCaptureResolution(display.width, display.height);
    ok &= Expect(encoded.valid() && encoded.width <= 4096 &&
                     encoded.height <= 4096,
                 tc.name);

    // Compute rendered video box inside viewer area using encoded dimensions,
    // matching Slint/OpenGL/Metal video presentation.
    const double scale =
        (std::min)(static_cast<double>(tc.viewer_w) / encoded.width,
                   static_cast<double>(tc.viewer_h) / encoded.height);
    const double draw_w = encoded.width * scale;
    const double draw_h = encoded.height * scale;
    const double draw_left = (tc.viewer_w - draw_w) * 0.5;
    const double draw_top = (tc.viewer_h - draw_h) * 0.5;

    const double test_uvs[][2] = {
        {0.0, 0.0}, {0.25, 0.25}, {0.5, 0.5}, {0.75, 0.85}, {1.0, 1.0}};
    for (const auto& uv : test_uvs) {
      float pointer_x = static_cast<float>(draw_left + uv[0] * draw_w);
      float pointer_y = static_cast<float>(draw_top + uv[1] * draw_h);
      if (static_cast<double>(pointer_x) < draw_left) {
        pointer_x =
            std::nextafter(pointer_x, std::numeric_limits<float>::infinity());
      } else if (static_cast<double>(pointer_x) > draw_left + draw_w) {
        pointer_x =
            std::nextafter(pointer_x, -std::numeric_limits<float>::infinity());
      }
      if (static_cast<double>(pointer_y) < draw_top) {
        pointer_y =
            std::nextafter(pointer_y, std::numeric_limits<float>::infinity());
      } else if (static_cast<double>(pointer_y) > draw_top + draw_h) {
        pointer_y =
            std::nextafter(pointer_y, -std::numeric_limits<float>::infinity());
      }
      const auto mapped = crossdesk::MapVideoPoint(
          pointer_x, pointer_y, tc.viewer_w, tc.viewer_h, encoded.width,
          encoded.height);
      ok &= Expect(mapped.has_value(), tc.name);
      if (!mapped) continue;

      ok &= Expect(std::fabs(mapped->x - static_cast<float>(uv[0])) < 1e-4f &&
                       std::fabs(mapped->y - static_cast<float>(uv[1])) < 1e-4f,
                   tc.name);

      // Verify MouseController desktop coordinate target has <= 1px error
      // across the full physical display, including the bottom-right corner.
      const int injected_x =
          static_cast<int>(std::clamp(mapped->x, 0.0f, 1.0f) *
                           (display.width - 1)) +
          display.left;
      const int injected_y =
          static_cast<int>(std::clamp(mapped->y, 0.0f, 1.0f) *
                           (display.height - 1)) +
          display.top;
      const int expected_x =
          static_cast<int>(uv[0] * (display.width - 1)) + display.left;
      const int expected_y =
          static_cast<int>(uv[1] * (display.height - 1)) + display.top;
      ok &= Expect(std::abs(injected_x - expected_x) <= 1 &&
                       std::abs(injected_y - expected_y) <= 1,
                   tc.name);

      // Verify host -> viewer cursor normalization matches the same [0, 1] UV.
      if (uv[0] < 1.0 && uv[1] < 1.0) {
        const double host_cursor_x = display.left + uv[0] * display.width;
        const double host_cursor_y = display.top + uv[1] * display.height;
        crossdesk::CursorState cursor_state{};
        ok &= Expect(crossdesk::NormalizeCursorPosition(
                         host_cursor_x, host_cursor_y, displays, 0,
                         &cursor_state) &&
                         cursor_state.position_valid &&
                         std::fabs(cursor_state.x - static_cast<float>(uv[0])) <
                             1e-5f &&
                         std::fabs(cursor_state.y - static_cast<float>(uv[1])) <
                             1e-5f,
                     tc.name);
      }
    }
  }
  return ok;
}

#ifdef _WIN32
bool TestD3D11VideoProcessorOversizedDownscale() {
  using Microsoft::WRL::ComPtr;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL level{};
  HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device,
      &level, &context);
  if (FAILED(hr) || !device || !context) {
    // Headless CI VMs without hardware D3D11 adapters skip GPU blt check.
    return true;
  }

  ComPtr<ID3D11VideoDevice> video_device;
  ComPtr<ID3D11VideoContext> video_context;
  if (FAILED(device.As(&video_device)) || FAILED(context.As(&video_context))) {
    return true;
  }

  constexpr UINT kSrcWidth = 5120;
  constexpr UINT kSrcHeight = 1440;
  const auto clamped = crossdesk::ClampCaptureResolution(kSrcWidth, kSrcHeight);
  if (!ExpectResolution(clamped, 4096, 1152, "D3D11 5120x1440 target")) {
    return false;
  }
  const UINT dst_width = static_cast<UINT>(clamped.width);
  const UINT dst_height = static_cast<UINT>(clamped.height);

  D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
  content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  content.InputWidth = kSrcWidth;
  content.InputHeight = kSrcHeight;
  content.OutputWidth = dst_width;
  content.OutputHeight = dst_height;
  content.InputFrameRate = content.OutputFrameRate = {60, 1};
  content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

  ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
  UINT in_flags = 0;
  UINT out_flags = 0;
  ComPtr<ID3D11VideoProcessor> processor;
  if (FAILED(
          video_device->CreateVideoProcessorEnumerator(&content, &enumerator)) ||
      FAILED(enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                   &in_flags)) ||
      !(in_flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) ||
      FAILED(enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_NV12,
                                                   &out_flags)) ||
      !(out_flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) ||
      FAILED(
          video_device->CreateVideoProcessor(enumerator.Get(), 0, &processor))) {
    return true;
  }

  std::vector<uint8_t> bgra(static_cast<size_t>(kSrcWidth) * kSrcHeight * 4);
  for (UINT y = 0; y < kSrcHeight; ++y) {
    uint8_t* row = bgra.data() + static_cast<size_t>(y) * kSrcWidth * 4;
    for (UINT x = 0; x < kSrcWidth; ++x) {
      const bool right = x >= kSrcWidth / 2;
      const bool bottom = y >= kSrcHeight / 2;
      // Distinct luma levels per quadrant in BGRA.
      const uint8_t v =
          static_cast<uint8_t>(right ? (bottom ? 240 : 180) : (bottom ? 110 : 30));
      row[x * 4 + 0] = v;
      row[x * 4 + 1] = v;
      row[x * 4 + 2] = v;
      row[x * 4 + 3] = 255;
    }
  }

  D3D11_TEXTURE2D_DESC src_desc{};
  src_desc.Width = kSrcWidth;
  src_desc.Height = kSrcHeight;
  src_desc.MipLevels = 1;
  src_desc.ArraySize = 1;
  src_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  src_desc.SampleDesc.Count = 1;
  src_desc.Usage = D3D11_USAGE_DEFAULT;
  src_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  D3D11_SUBRESOURCE_DATA init_data{};
  init_data.pSysMem = bgra.data();
  init_data.SysMemPitch = kSrcWidth * 4;

  ComPtr<ID3D11Texture2D> src_tex;
  ComPtr<ID3D11Texture2D> dst_tex;
  ComPtr<ID3D11Texture2D> staging_tex;
  D3D11_TEXTURE2D_DESC dst_desc = src_desc;
  dst_desc.Width = dst_width;
  dst_desc.Height = dst_height;
  dst_desc.Format = DXGI_FORMAT_NV12;
  D3D11_TEXTURE2D_DESC staging_desc = dst_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

  if (FAILED(device->CreateTexture2D(&src_desc, &init_data, &src_tex)) ||
      FAILED(device->CreateTexture2D(&dst_desc, nullptr, &dst_tex)) ||
      FAILED(device->CreateTexture2D(&staging_desc, nullptr, &staging_tex))) {
    return Expect(false, "D3D11 texture creation for 5120x1440 -> 4096x1152");
  }

  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC in_view_desc{};
  in_view_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC out_view_desc{};
  out_view_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
  ComPtr<ID3D11VideoProcessorInputView> in_view;
  ComPtr<ID3D11VideoProcessorOutputView> out_view;
  if (FAILED(video_device->CreateVideoProcessorInputView(
          src_tex.Get(), enumerator.Get(), &in_view_desc, &in_view)) ||
      FAILED(video_device->CreateVideoProcessorOutputView(
          dst_tex.Get(), enumerator.Get(), &out_view_desc, &out_view))) {
    return Expect(false, "D3D11 video processor view creation");
  }

  const RECT src_rect{0, 0, static_cast<LONG>(kSrcWidth),
                      static_cast<LONG>(kSrcHeight)};
  const RECT dst_rect{0, 0, static_cast<LONG>(dst_width),
                      static_cast<LONG>(dst_height)};
  video_context->VideoProcessorSetStreamSourceRect(processor.Get(), 0, TRUE,
                                                   &src_rect);
  video_context->VideoProcessorSetStreamDestRect(processor.Get(), 0, TRUE,
                                                 &dst_rect);
  video_context->VideoProcessorSetOutputTargetRect(processor.Get(), TRUE,
                                                   &dst_rect);

  D3D11_VIDEO_PROCESSOR_STREAM stream{};
  stream.Enable = TRUE;
  stream.pInputSurface = in_view.Get();
  if (FAILED(video_context->VideoProcessorBlt(processor.Get(), out_view.Get(),
                                              0, 1, &stream))) {
    return Expect(false, "D3D11 VideoProcessorBlt 5120x1440 -> 4096x1152");
  }

  context->CopyResource(staging_tex.Get(), dst_tex.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(staging_tex.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    return Expect(false, "D3D11 Map 4096x1152 NV12 staging");
  }
  const auto* y_plane = static_cast<const uint8_t*>(mapped.pData);
  const uint8_t tl =
      y_plane[static_cast<size_t>(dst_height / 4) * mapped.RowPitch +
              dst_width / 4];
  const uint8_t tr =
      y_plane[static_cast<size_t>(dst_height / 4) * mapped.RowPitch +
              (dst_width * 3) / 4];
  const uint8_t bl =
      y_plane[static_cast<size_t>((dst_height * 3) / 4) * mapped.RowPitch +
              dst_width / 4];
  const uint8_t br =
      y_plane[static_cast<size_t>((dst_height * 3) / 4) * mapped.RowPitch +
              (dst_width * 3) / 4];
  context->Unmap(staging_tex.Get(), 0);

  return Expect(tl < bl && bl < tr && tr < br,
                "D3D11 VideoProcessor preserves all 4 quadrants monotonically");
}
#endif

std::filesystem::path FindRepoRoot() {
  std::filesystem::path current = std::filesystem::current_path();
  while (!current.empty()) {
    if (std::filesystem::exists(current / "xmake.lua") &&
        std::filesystem::exists(current / "apps/desktop/xmake/targets.lua")) {
      return current;
    }
    const std::filesystem::path parent = current.parent_path();
    if (parent == current) break;
    current = parent;
  }
  return {};
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  std::ostringstream out;
  out << file.rdbuf();
  return out.str();
}

bool TestPipelineWiring() {
  const std::filesystem::path repo_root = FindRepoRoot();
  if (repo_root.empty()) {
    // Allow running the binary outside the checkout when only pure tests apply.
    return true;
  }
  const std::string dxgi_gpu = ReadFile(
      repo_root /
      "apps/desktop/src/platform/windows/screen_capturer/dxgi_nv12_frames.cpp");
  const std::string dxgi_cpu = ReadFile(
      repo_root /
      "apps/desktop/src/platform/windows/screen_capturer/screen_capturer_dxgi.cpp");
  const std::string wgc_gpu = ReadFile(
      repo_root /
      "apps/desktop/src/platform/windows/screen_capturer/wgc_session_impl.cpp");
  const std::string wgc_cpu = ReadFile(
      repo_root /
      "apps/desktop/src/platform/windows/screen_capturer/screen_capturer_wgc.cpp");
  const std::string gdi = ReadFile(
      repo_root /
      "apps/desktop/src/platform/windows/screen_capturer/screen_capturer_gdi.cpp");
  const std::string mac_sck = ReadFile(
      repo_root /
      "apps/desktop/src/platform/macos/screen_capturer/screen_capturer_sck_impl.mm");
  const std::string device_mgr = ReadFile(
      repo_root /
      "apps/desktop/src/gui/features/devices/session_device_manager.cpp");

  bool ok = true;
  ok &= Expect(dxgi_gpu.find("ClampCaptureResolution") != std::string::npos,
               "DxgiNv12Frames clamps GPU NV12 output resolution");
  ok &= Expect(dxgi_cpu.find("ClampCaptureResolution") != std::string::npos &&
                   dxgi_cpu.find("libyuv::ARGBScale") != std::string::npos,
               "ScreenCapturerDxgi clamps CPU fallback resolution");
  ok &= Expect(wgc_gpu.find("ScaleOversizedTexture") != std::string::npos &&
                   wgc_cpu.find("ClampCaptureResolution") != std::string::npos,
               "WGC clamps GPU and CPU fallback resolution");
  ok &= Expect(gdi.find("ClampCaptureResolution") != std::string::npos,
               "ScreenCapturerGdi clamps oversized capture resolution");
  ok &= Expect(mac_sck.find("ClampCaptureResolution") != std::string::npos,
               "ScreenCapturerSckImpl clamps SCStreamConfiguration resolution");
  ok &= Expect(
      device_mgr.find("IsOversizedCaptureResolution") != std::string::npos &&
          device_mgr.find("DownscaleOversizedNv12Packed") !=
              std::string::npos &&
          device_mgr.find("DownscaleOversizedNv12Planes") !=
              std::string::npos &&
          device_mgr.find("MaterializeAndDownscaleOversizedNv12") !=
              std::string::npos,
      "SessionDeviceManager guards oversized frames across all platforms");
  return ok;
}

}  // namespace

int main() {
  bool ok = true;
  ok &= TestResolutionClamping();
  ok &= TestOversizedNv12FrameScaler();
  ok &= TestPointerAndCursorMappingZeroOffset();
  ok &= TestPipelineWiring();
#ifdef _WIN32
  ok &= TestD3D11VideoProcessorOversizedDownscale();
#endif
  if (ok) {
    std::cout << "All oversized capture resolution downscale checks passed\n";
  }
  return ok ? 0 : 1;
}
