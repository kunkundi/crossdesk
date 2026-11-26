#ifndef _WINDOWS_HDR_CAPTURE_H_
#define _WINDOWS_HDR_CAPTURE_H_

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

#include "hdr_tone_map_math.h"

namespace crossdesk {

struct WindowsHdrOutputInfo {
  bool hdr_enabled = false;
  DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
  float max_luminance_nits = 80.0f;
  float sdr_white_scale = 1.0f;

  HdrToneMapParams ToToneMapParams(DXGI_FORMAT surface_format) const noexcept {
    HdrToneMapParams params;
    params.sdr_white_scale = sdr_white_scale;
    params.max_luminance_nits = max_luminance_nits;
    if (surface_format == DXGI_FORMAT_R10G10B10A2_UNORM) {
      params.mode =
          (color_space == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
              ? HdrInputMode::kPqBt2020
              : HdrInputMode::kPassthroughSdr;
    } else {
      params.mode = HdrInputMode::kScRgbFp16;
    }
    return params;
  }
};

bool IsHdrDesktopTextureFormat(DXGI_FORMAT format) noexcept;
float QueryMonitorSdrWhiteScale(HMONITOR monitor) noexcept;
WindowsHdrOutputInfo QueryDxgiOutputHdrInfo(IDXGIOutput* output) noexcept;
WindowsHdrOutputInfo QueryMonitorHdrInfo(HMONITOR monitor) noexcept;
WindowsHdrOutputInfo QueryWindowHdrInfo(HWND hwnd) noexcept;

// Uses IDXGIOutput5::DuplicateOutput1 with FP16/RGB10A2 when the output is in
// HDR mode, falling back to IDXGIOutput1::DuplicateOutput when unsupported.
HRESULT DuplicateDxgiOutput(
    IDXGIOutput* output, ID3D11Device* device,
    IDXGIOutputDuplication** duplication_out,
    WindowsHdrOutputInfo* hdr_info_out = nullptr) noexcept;

class D3d11HdrToneMapper {
 public:
  D3d11HdrToneMapper() = default;
  ~D3d11HdrToneMapper() = default;

  void Reset() noexcept;

  ID3D11Texture2D* ToneMapToBgra8(ID3D11Device* device,
                                  ID3D11DeviceContext* context,
                                  ID3D11Texture2D* src_texture,
                                  const WindowsHdrOutputInfo& hdr_info);

 private:
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
