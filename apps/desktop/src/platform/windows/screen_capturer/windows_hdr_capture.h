/*
 * @Author: NandaNg314
 * @Date: 2026-11-26
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WINDOWS_HDR_CAPTURE_H_
#define _WINDOWS_HDR_CAPTURE_H_

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "hdr_tone_map_math.h"

namespace crossdesk {

inline constexpr bool IsHdr10PqColorSpace(
    DXGI_COLOR_SPACE_TYPE color_space) noexcept {
  return color_space == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
         color_space == DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020;
}

// Desktop Duplication does not expose per-frame swapchain color space. Negotiate
// FP16 scRGB (plus BGRA8 SDR) and omit R10G10B10A2_UNORM so DWM converts both
// SDR 10-bit and HDR10 PQ fullscreen surfaces into unambiguous scRGB FP16.
inline constexpr std::array<DXGI_FORMAT, 2> kDxgiHdrDuplicationFormats = {
    DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_B8G8R8A8_UNORM,
};

inline constexpr bool IsHdrFramePoolFormatUnsupported(HRESULT hr) noexcept {
  return hr == E_INVALIDARG || hr == DXGI_ERROR_UNSUPPORTED ||
         hr == DXGI_ERROR_INVALID_CALL || hr == E_NOTIMPL;
}

template <typename GetSizesFn, typename QueryConfigFn>
bool QueryDisplayConfigPathsWithRetry(
    UINT32 flags, std::vector<DISPLAYCONFIG_PATH_INFO>* paths,
    std::vector<DISPLAYCONFIG_MODE_INFO>* modes, GetSizesFn&& get_sizes,
    QueryConfigFn&& query_config) {
  if (!paths || !modes) {
    return false;
  }
  constexpr int kMaxAttempts = 4;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    UINT32 path_count = 0;
    UINT32 mode_count = 0;
    if (get_sizes(flags, &path_count, &mode_count) != ERROR_SUCCESS ||
        path_count == 0) {
      return false;
    }
    paths->resize(path_count);
    modes->resize(mode_count);
    const LONG rc = query_config(flags, &path_count, paths->data(), &mode_count,
                                 modes->data());
    if (rc == ERROR_SUCCESS) {
      paths->resize(path_count);
      modes->resize(mode_count);
      return true;
    }
    if (rc != ERROR_INSUFFICIENT_BUFFER) {
      return false;
    }
  }
  return false;
}

inline float ClampSdrWhiteScale(float scale) noexcept {
  if (!(scale > 0.0f)) {
    return 1.0f;
  }
  return (std::max)(0.5f, (std::min)(scale, 12.5f));
}

inline float ResolveMonitorSdrWhiteScale(std::optional<float> queried_scale,
                                         float fallback_scale = 1.0f) noexcept {
  if (queried_scale.has_value() && *queried_scale > 0.0f) {
    return ClampSdrWhiteScale(*queried_scale);
  }
  return ClampSdrWhiteScale(fallback_scale);
}

struct WindowsHdrOutputInfo {
  bool hdr_enabled = false;
  DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
  float max_luminance_nits = 80.0f;
  float sdr_white_scale = 1.0f;

  HdrToneMapParams ToToneMapParams(
      DXGI_FORMAT surface_format,
      std::optional<DXGI_COLOR_SPACE_TYPE> source_color_space =
          std::nullopt) const noexcept {
    HdrToneMapParams params;
    params.sdr_white_scale = sdr_white_scale;
    params.max_luminance_nits = max_luminance_nits;
    if (surface_format == DXGI_FORMAT_R10G10B10A2_UNORM) {
      // R10G10B10A2_UNORM can carry SDR even on an HDR display. Use ST.2084 PQ
      // decoding only when the source surface itself is confirmed to be PQ.
      params.mode = (source_color_space.has_value() &&
                     IsHdr10PqColorSpace(*source_color_space))
                        ? HdrInputMode::kPqBt2020
                        : HdrInputMode::kPassthroughSdr;
    } else {
      params.mode = HdrInputMode::kScRgbFp16;
    }
    return params;
  }
};

bool IsHdrDesktopTextureFormat(DXGI_FORMAT format) noexcept;
std::optional<float> TryQueryMonitorSdrWhiteScale(HMONITOR monitor) noexcept;
bool RefreshMonitorSdrWhiteScale(HMONITOR monitor,
                                 float* sdr_white_scale) noexcept;
float QueryMonitorSdrWhiteScale(HMONITOR monitor,
                                float fallback_scale = 1.0f) noexcept;
WindowsHdrOutputInfo QueryDxgiOutputHdrInfo(
    IDXGIOutput* output, float fallback_sdr_white_scale = 1.0f) noexcept;
WindowsHdrOutputInfo QueryMonitorHdrInfo(
    HMONITOR monitor, float fallback_sdr_white_scale = 1.0f) noexcept;
WindowsHdrOutputInfo QueryWindowHdrInfo(
    HWND hwnd, float fallback_sdr_white_scale = 1.0f) noexcept;

// Caches per-monitor HDR state via IDXGIFactory1::IsCurrent() so periodic
// checks during SDR capture avoid DXGI adapter/output enumeration and only
// refresh SDR white level on active HDR outputs.
class WindowsHdrOutputCache {
 public:
  WindowsHdrOutputCache() = default;
  ~WindowsHdrOutputCache() = default;

  void Reset() noexcept;
  WindowsHdrOutputInfo QueryForMonitor(HMONITOR monitor) noexcept;
  WindowsHdrOutputInfo QueryForWindow(HWND hwnd) noexcept;

 private:
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory_;
  HMONITOR monitor_ = nullptr;
  WindowsHdrOutputInfo info_{};
  float reported_max_luminance_nits_ = 0.0f;
  bool valid_ = false;
};

// Uses IDXGIOutput5::DuplicateOutput1 with FP16/BGRA8 when the output is in
// HDR mode, falling back to IDXGIOutput1::DuplicateOutput when unsupported.
HRESULT DuplicateDxgiOutput(
    IDXGIOutput* output, ID3D11Device* device,
    IDXGIOutputDuplication** duplication_out,
    WindowsHdrOutputInfo* hdr_info_out = nullptr) noexcept;

class D3d11HdrToneMapper {
 public:
  enum class Backend : uint8_t {
    kNone = 0,
    kPassthroughBgra8 = 1,
    kGpu = 2,
    kCpuFallback = 3,
  };

  D3d11HdrToneMapper() = default;
  ~D3d11HdrToneMapper() = default;

  void Reset() noexcept;
  bool HasAllocatedResources() const noexcept;

  void SetGpuEnabledForTesting(bool enabled) noexcept {
    gpu_disabled_for_testing_ = !enabled;
  }
  Backend last_backend() const noexcept { return last_backend_; }

  ID3D11Texture2D* ToneMapToBgra8(
      ID3D11Device* device, ID3D11DeviceContext* context,
      ID3D11Texture2D* src_texture, const WindowsHdrOutputInfo& hdr_info,
      std::optional<DXGI_COLOR_SPACE_TYPE> source_color_space = std::nullopt);

 private:
  void ReleaseDeviceResources() noexcept;
  bool EnsureGpuPipeline(ID3D11Device* device);
  bool EnsureTextures(ID3D11Device* device, UINT width, UINT height,
                      DXGI_FORMAT src_format, bool need_gpu_views);
  bool ToneMapOnGpu(ID3D11Device* device, ID3D11DeviceContext* context,
                    ID3D11Texture2D* src_texture,
                    const D3D11_TEXTURE2D_DESC& src_desc,
                    const HdrToneMapParams& params);
  bool ToneMapOnCpu(ID3D11Device* device, ID3D11DeviceContext* context,
                    ID3D11Texture2D* src_texture,
                    const D3D11_TEXTURE2D_DESC& src_desc,
                    const HdrToneMapParams& params);

  ID3D11Device* bound_device_ = nullptr;
  UINT width_ = 0;
  UINT height_ = 0;
  DXGI_FORMAT src_format_ = DXGI_FORMAT_UNKNOWN;
  HdrShaderConstants uploaded_constants_{};
  bool constants_valid_ = false;
  bool gpu_disabled_ = false;
  bool gpu_disabled_for_testing_ = false;
  Backend last_backend_ = Backend::kNone;

  Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer_;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_state_;
  Microsoft::WRL::ComPtr<ID3D11BlendState> blend_state_;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_stencil_state_;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> input_texture_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> input_srv_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> output_texture_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> output_rtv_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> cpu_staging_;
  std::vector<uint8_t> cpu_bgra_;
};

}  // namespace crossdesk

#endif  // _WINDOWS_HDR_CAPTURE_H_
