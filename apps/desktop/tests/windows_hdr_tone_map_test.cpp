#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

#include "hdr_tone_map_math.h"

#ifdef _WIN32
#include "windows_hdr_capture.h"
#endif

namespace {

float SrgbByteToLinear(uint8_t v) {
  const float c = static_cast<float>(v) / 255.0f;
  return (c <= 0.04045f) ? (c / 12.92f)
                         : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float LinearNitsToPq(float nits) {
  const float y = (std::max)(0.0f, (std::min)(nits / 10000.0f, 1.0f));
  constexpr float kM1 = 0.1593017578125f;
  constexpr float kM2 = 78.84375f;
  constexpr float kC1 = 0.8359375f;
  constexpr float kC2 = 18.8515625f;
  constexpr float kC3 = 18.6875f;
  const float ym = std::pow(y, kM1);
  return std::pow((kC1 + kC2 * ym) / (1.0f + kC3 * ym), kM2);
}

uint32_t PackR10G10B10A2(float r, float g, float b) {
  const uint32_t ri = static_cast<uint32_t>(r * 1023.0f + 0.5f) & 0x3FFu;
  const uint32_t gi = static_cast<uint32_t>(g * 1023.0f + 0.5f) & 0x3FFu;
  const uint32_t bi = static_cast<uint32_t>(b * 1023.0f + 0.5f) & 0x3FFu;
  return ri | (gi << 10) | (bi << 20) | (3u << 30);
}

bool ExpectNearByte(const char* label, int actual, int expected, int tol = 1) {
  if (std::abs(actual - expected) <= tol) {
    return true;
  }
  std::cerr << label << ": expected " << expected << " (+/-" << tol << "), got "
            << actual << "\n";
  return false;
}

bool ExpectTrue(const char* label, bool condition) {
  if (condition) {
    return true;
  }
  std::cerr << label << ": condition failed\n";
  return false;
}

bool TestSdrFidelityAndWhiteBalance() {
  bool ok = true;
  crossdesk::HdrToneMapParams params;
  params.sdr_white_scale = 1.0f;
  params.max_luminance_nits = 417.7f;
  params.mode = crossdesk::HdrInputMode::kScRgbFp16;

  // Verify exact bit-identity for SDR UI values up to the 0.80 linear knee.
  for (int v : {0, 16, 64, 128, 180, 200, 225}) {
    const float lin = SrgbByteToLinear(static_cast<uint8_t>(v));
    uint16_t fp16[4] = {crossdesk::FloatToHalf(lin),
                        crossdesk::FloatToHalf(lin),
                        crossdesk::FloatToHalf(lin),
                        crossdesk::FloatToHalf(1.0f)};
    uint8_t bgra[4] = {};
    ok &= ExpectTrue("ConvertScRgbFp16ToBgra8 gray",
                     crossdesk::ConvertScRgbFp16ToBgra8(
                         reinterpret_cast<const uint8_t*>(fp16), 8, bgra, 4, 1,
                         1, params));
    ok &= ExpectNearByte("SDR gray B", bgra[0], v, 1);
    ok &= ExpectNearByte("SDR gray G", bgra[1], v, 1);
    ok &= ExpectNearByte("SDR gray R", bgra[2], v, 1);
    ok &= ExpectNearByte("SDR gray A", bgra[3], 255, 0);
  }

  // Verify BGRA channel ordering on saturated primary colors.
  {
    const float red_lin = SrgbByteToLinear(200);
    uint16_t fp16[4] = {crossdesk::FloatToHalf(red_lin),
                        crossdesk::FloatToHalf(0.0f),
                        crossdesk::FloatToHalf(0.0f),
                        crossdesk::FloatToHalf(1.0f)};
    uint8_t bgra[4] = {};
    crossdesk::ConvertScRgbFp16ToBgra8(reinterpret_cast<const uint8_t*>(fp16),
                                       8, bgra, 4, 1, 1, params);
    ok &= ExpectNearByte("Primary red -> B=0", bgra[0], 0, 0);
    ok &= ExpectNearByte("Primary red -> G=0", bgra[1], 0, 0);
    ok &= ExpectNearByte("Primary red -> R=200", bgra[2], 200, 1);
  }

  // Issue #247 regression: elevated Windows SDR brightness slider (3.0x = 240 nits)
  // must not blow out midtones or 75% gray to 255.
  params.sdr_white_scale = 3.0f;
  params.max_luminance_nits = 600.0f;
  for (int v : {64, 128, 192}) {
    const float scaled = SrgbByteToLinear(static_cast<uint8_t>(v)) * 3.0f;
    uint16_t fp16[4] = {crossdesk::FloatToHalf(scaled),
                        crossdesk::FloatToHalf(scaled),
                        crossdesk::FloatToHalf(scaled),
                        crossdesk::FloatToHalf(1.0f)};
    uint8_t bgra[4] = {};
    crossdesk::ConvertScRgbFp16ToBgra8(reinterpret_cast<const uint8_t*>(fp16),
                                       8, bgra, 4, 1, 1, params);
    ok &= ExpectNearByte("Elevated SDRWhiteLevel preserved", bgra[2], v, 1);
  }

  return ok;
}

bool TestHdrHighlightRollOffAndSafety() {
  bool ok = true;
  crossdesk::HdrToneMapParams params;
  params.sdr_white_scale = 1.0f;
  params.max_luminance_nits = 1000.0f;
  params.mode = crossdesk::HdrInputMode::kScRgbFp16;

  // Monotonic highlight compression from 80 nits (1.0) up to 1000 nits (12.5).
  const float levels[] = {0.8f, 1.0f, 2.0f, 4.0f, 8.0f, 12.5f};
  int prev = -1;
  int sdr_white_code = 0;
  for (int i = 0; i < 6; ++i) {
    uint16_t fp16[4] = {crossdesk::FloatToHalf(levels[i]),
                        crossdesk::FloatToHalf(levels[i]),
                        crossdesk::FloatToHalf(levels[i]),
                        crossdesk::FloatToHalf(1.0f)};
    uint8_t bgra[4] = {};
    crossdesk::ConvertScRgbFp16ToBgra8(reinterpret_cast<const uint8_t*>(fp16),
                                       8, bgra, 4, 1, 1, params);
    ok &= ExpectTrue("HDR highlight monotonic", bgra[2] >= prev);
    prev = bgra[2];
    if (i == 1) sdr_white_code = bgra[2];
  }
  ok &= ExpectTrue("SDR white crisp without premature clip",
                   sdr_white_code >= 240 && sdr_white_code < 255);
  ok &= ExpectNearByte("Peak luminance reaches 255", prev, 255, 0);

  // Hue preservation on bright HDR orange highlight (no yellow skew).
  {
    uint16_t orange[4] = {crossdesk::FloatToHalf(4.0f),
                          crossdesk::FloatToHalf(1.5f),
                          crossdesk::FloatToHalf(0.2f),
                          crossdesk::FloatToHalf(1.0f)};
    uint8_t bgra[4] = {};
    crossdesk::ConvertScRgbFp16ToBgra8(reinterpret_cast<const uint8_t*>(orange),
                                       8, bgra, 4, 1, 1, params);
    ok &= ExpectTrue("HDR orange keeps R > G > B",
                     bgra[2] > bgra[1] && bgra[1] > bgra[0]);
  }

  // Wide-gamut negative scRGB & NaN / Inf handling.
  {
    uint16_t special[3][4] = {
        {crossdesk::FloatToHalf(-0.15f), crossdesk::FloatToHalf(0.50f),
         crossdesk::FloatToHalf(0.60f), crossdesk::FloatToHalf(1.0f)},
        {0x7E00u, 0xFC00u, 0x0000u, 0x3C00u},  // NaN, -Inf, 0
        {0x7C00u, 0x7C00u, 0x7C00u, 0x3C00u},  // +Inf specular
    };
    uint8_t out[3][4] = {};
    crossdesk::ConvertScRgbFp16ToBgra8(
        reinterpret_cast<const uint8_t*>(special), 8,
        reinterpret_cast<uint8_t*>(out), 4, 1, 3, params);
    ok &= ExpectTrue("Wide-gamut cyan preserves G/B > R",
                     out[0][0] > out[0][2] && out[0][1] > out[0][2]);
    ok &= ExpectNearByte("NaN -> 0", out[1][2], 0, 0);
    ok &= ExpectNearByte("-Inf -> 0", out[1][1], 0, 0);
    ok &= ExpectNearByte("+Inf -> 255", out[2][2], 255, 0);
  }

  // HDR10 PQ R10G10B10A2 conversion.
  {
    params.mode = crossdesk::HdrInputMode::kPqBt2020;
    const float pq80 = LinearNitsToPq(80.0f);
    const float pq1000 = LinearNitsToPq(1000.0f);
    uint32_t pq_pixels[2] = {PackR10G10B10A2(pq80, pq80, pq80),
                             PackR10G10B10A2(pq1000, pq1000, pq1000)};
    uint8_t bgra[8] = {};
    ok &= ExpectTrue("ConvertR10G10B10A2ToBgra8",
                     crossdesk::ConvertR10G10B10A2ToBgra8(
                         reinterpret_cast<const uint8_t*>(pq_pixels), 8, bgra,
                         8, 2, 1, params));
    ok &= ExpectTrue("PQ 80 nits < PQ 1000 nits", bgra[2] < bgra[6]);
    ok &= ExpectTrue("PQ 80 nits near SDR white", bgra[2] >= 240);
    ok &= ExpectNearByte("PQ 1000 nits reaches 255", bgra[6], 255, 1);
  }

  return ok;
}

#ifdef _WIN32
bool TestWindowsHdrPolicyAndWhiteScaleResilience() {
  bool ok = true;

  // 1. DuplicateOutput1 format negotiation must prefer FP16 + BGRA8 and omit
  // ambiguous R10G10B10A2_UNORM.
  ok &= ExpectTrue("DuplicateOutput1 first format is FP16",
                   crossdesk::kDxgiHdrDuplicationFormats[0] ==
                       DXGI_FORMAT_R16G16B16A16_FLOAT);
  for (DXGI_FORMAT fmt : crossdesk::kDxgiHdrDuplicationFormats) {
    ok &= ExpectTrue("DuplicateOutput1 omits ambiguous R10G10B10A2",
                     fmt != DXGI_FORMAT_R10G10B10A2_UNORM);
  }

  // 2. Unconfirmed R10G10B10A2_UNORM on an HDR display must NOT decode as PQ
  // (mid-gray 128 must stay 128 instead of blowing out to ~247).
  crossdesk::WindowsHdrOutputInfo hdr_info;
  hdr_info.hdr_enabled = true;
  hdr_info.color_space = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
  hdr_info.sdr_white_scale = 1.0f;
  hdr_info.max_luminance_nits = 600.0f;

  const auto unconfirmed_params =
      hdr_info.ToToneMapParams(DXGI_FORMAT_R10G10B10A2_UNORM);
  ok &= ExpectTrue("Unconfirmed R10G10B10A2 uses SDR passthrough",
                   unconfirmed_params.mode ==
                       crossdesk::HdrInputMode::kPassthroughSdr);
  const uint32_t sdr_mid_gray =
      PackR10G10B10A2(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f);
  uint8_t sdr_out[4] = {};
  ok &= crossdesk::ConvertR10G10B10A2ToBgra8(
      reinterpret_cast<const uint8_t*>(&sdr_mid_gray), 4, sdr_out, 4, 1, 1,
      unconfirmed_params);
  ok &= ExpectNearByte("Unconfirmed 10-bit SDR mid-gray stays 128", sdr_out[2],
                       128, 1);

  const auto confirmed_pq_params = hdr_info.ToToneMapParams(
      DXGI_FORMAT_R10G10B10A2_UNORM,
      DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
  ok &= ExpectTrue("Confirmed PQ R10G10B10A2 uses PQ decode",
                   confirmed_pq_params.mode ==
                       crossdesk::HdrInputMode::kPqBt2020);

  // 3. QueryDisplayConfig retry on ERROR_INSUFFICIENT_BUFFER and preserving the
  // last valid white scale (e.g. 3.0x) when refresh fails.
  {
    int size_calls = 0;
    int query_calls = 0;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    const bool retried = crossdesk::QueryDisplayConfigPathsWithRetry(
        QDC_ONLY_ACTIVE_PATHS, &paths, &modes,
        [&](UINT32, UINT32* path_count, UINT32* mode_count) -> LONG {
          ++size_calls;
          *path_count = static_cast<UINT32>(size_calls);
          *mode_count = static_cast<UINT32>(size_calls * 2);
          return ERROR_SUCCESS;
        },
        [&](UINT32, UINT32* path_count, DISPLAYCONFIG_PATH_INFO*,
            UINT32* mode_count, DISPLAYCONFIG_MODE_INFO*) -> LONG {
          ++query_calls;
          if (query_calls == 1) {
            return ERROR_INSUFFICIENT_BUFFER;
          }
          return (*path_count == 2 && *mode_count == 4) ? ERROR_SUCCESS
                                                        : ERROR_GEN_FAILURE;
        });
    ok &= ExpectTrue("ERROR_INSUFFICIENT_BUFFER re-queries sizes and succeeds",
                     retried && size_calls == 2 && query_calls == 2 &&
                         paths.size() == 2 && modes.size() == 4);

    // Failed refresh on a 3x white-level desktop must retain 3.0f so mid-gray
    // 128 stays 128 instead of jumping to 210.
    float retained_white_scale = 3.0f;
    ok &= ExpectTrue(
        "Null monitor refresh reports failure",
        !crossdesk::RefreshMonitorSdrWhiteScale(nullptr, &retained_white_scale));
    ok &= ExpectTrue("Failed refresh retains previous 3.0x white scale",
                     std::abs(retained_white_scale - 3.0f) < 1e-6f);
    crossdesk::HdrToneMapParams retained_params;
    retained_params.sdr_white_scale = retained_white_scale;
    retained_params.max_luminance_nits = 600.0f;
    const float desktop_mid_gray = SrgbByteToLinear(128) * 3.0f;
    uint16_t fp16_mid[4] = {crossdesk::FloatToHalf(desktop_mid_gray),
                            crossdesk::FloatToHalf(desktop_mid_gray),
                            crossdesk::FloatToHalf(desktop_mid_gray),
                            crossdesk::FloatToHalf(1.0f)};
    uint8_t mid_out[4] = {};
    crossdesk::ConvertScRgbFp16ToBgra8(
        reinterpret_cast<const uint8_t*>(fp16_mid), 8, mid_out, 4, 1, 1,
        retained_params);
    ok &= ExpectNearByte("Retained 3x white scale keeps mid-gray at 128",
                         mid_out[2], 128, 1);
  }

  // 4. WGC FP16 format-unsupported HRESULT classification.
  ok &= ExpectTrue("E_INVALIDARG classified as format unsupported",
                   crossdesk::IsHdrFramePoolFormatUnsupported(E_INVALIDARG));
  ok &= ExpectTrue(
      "DXGI_ERROR_UNSUPPORTED classified as format unsupported",
      crossdesk::IsHdrFramePoolFormatUnsupported(DXGI_ERROR_UNSUPPORTED));
  ok &= ExpectTrue(
      "DXGI_ERROR_DEVICE_REMOVED is NOT cached as format unsupported",
      !crossdesk::IsHdrFramePoolFormatUnsupported(DXGI_ERROR_DEVICE_REMOVED));
  ok &= ExpectTrue(
      "DXGI_ERROR_DEVICE_RESET is NOT cached as format unsupported",
      !crossdesk::IsHdrFramePoolFormatUnsupported(DXGI_ERROR_DEVICE_RESET));
  ok &= ExpectTrue("E_OUTOFMEMORY is NOT cached as format unsupported",
                   !crossdesk::IsHdrFramePoolFormatUnsupported(E_OUTOFMEMORY));

  return ok;
}

bool ReadbackBgra8Texture(ID3D11Device* device, ID3D11DeviceContext* context,
                          ID3D11Texture2D* tex, std::vector<uint8_t>* out_bytes,
                          UINT* out_width, UINT* out_height) {
  if (!device || !context || !tex || !out_bytes) {
    return false;
  }
  D3D11_TEXTURE2D_DESC desc{};
  tex->GetDesc(&desc);
  if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
    return false;
  }
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()))) {
    return false;
  }
  context->CopyResource(staging.Get(), tex);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    return false;
  }
  out_bytes->resize(static_cast<size_t>(desc.Width) * desc.Height * 4);
  for (UINT y = 0; y < desc.Height; ++y) {
    const auto* src_row =
        static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch;
    std::copy_n(src_row, static_cast<size_t>(desc.Width) * 4,
                out_bytes->data() + static_cast<size_t>(y) * desc.Width * 4);
  }
  context->Unmap(staging.Get(), 0);
  if (out_width) *out_width = desc.Width;
  if (out_height) *out_height = desc.Height;
  return true;
}

bool RunToneMapperParitySuite(D3D_DRIVER_TYPE driver_type,
                              const char* suite_label, bool enable_gpu,
                              crossdesk::D3d11HdrToneMapper::Backend expected) {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
  const HRESULT hr = D3D11CreateDevice(
      nullptr, driver_type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr,
      0, D3D11_SDK_VERSION, device.GetAddressOf(), &feature_level,
      context.GetAddressOf());
  if (FAILED(hr)) {
    if (driver_type == D3D_DRIVER_TYPE_HARDWARE) {
      std::cout << "Skipping hardware D3D11 test (unavailable in CI)\n";
      return true;
    }
    std::cerr << suite_label << ": D3D11CreateDevice failed, hr=0x" << std::hex
              << static_cast<uint32_t>(hr) << std::dec << "\n";
    return false;
  }

  constexpr int kW = 8;
  constexpr int kH = 2;
  const float samples[kW * kH][3] = {
      {0.0f, 0.0f, 0.0f},             {0.05f, 0.15f, 0.45f},
      {0.21586f, 0.21586f, 0.21586f}, {0.75f, 0.20f, 0.10f},
      {1.0f, 1.0f, 1.0f},             {1.8f, 1.8f, 1.8f},
      {4.5f, 1.6f, 0.2f},             {8.0f, 8.0f, 8.0f},
      {-0.12f, 0.55f, 0.65f},         {0.80f, 0.80f, 0.80f},
      {2.5f, 0.4f, 2.2f},             {0.002f, 0.002f, 0.002f},
      {3.0f, 3.0f, 3.0f},             {5.22f, 5.22f, 5.22f},
      {0.33f, 0.66f, 0.99f},          {10.0f, 10.0f, 10.0f},
  };

  std::vector<uint16_t> fp16_data(kW * kH * 4);
  for (int i = 0; i < kW * kH; ++i) {
    fp16_data[i * 4 + 0] = crossdesk::FloatToHalf(samples[i][0]);
    fp16_data[i * 4 + 1] = crossdesk::FloatToHalf(samples[i][1]);
    fp16_data[i * 4 + 2] = crossdesk::FloatToHalf(samples[i][2]);
    fp16_data[i * 4 + 3] = crossdesk::FloatToHalf(1.0f);
  }

  D3D11_TEXTURE2D_DESC src_desc{};
  src_desc.Width = kW;
  src_desc.Height = kH;
  src_desc.MipLevels = 1;
  src_desc.ArraySize = 1;
  src_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  src_desc.SampleDesc.Count = 1;
  src_desc.Usage = D3D11_USAGE_DEFAULT;
  src_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  D3D11_SUBRESOURCE_DATA init_data{};
  init_data.pSysMem = fp16_data.data();
  init_data.SysMemPitch = kW * 8;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> src_tex;
  if (FAILED(device->CreateTexture2D(&src_desc, &init_data,
                                     src_tex.GetAddressOf()))) {
    std::cerr << suite_label << ": CreateTexture2D failed\n";
    return false;
  }

  crossdesk::WindowsHdrOutputInfo hdr_info;
  hdr_info.hdr_enabled = true;
  hdr_info.color_space = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
  hdr_info.sdr_white_scale = 1.5f;
  hdr_info.max_luminance_nits = 600.0f;

  crossdesk::D3d11HdrToneMapper mapper;
  mapper.SetGpuEnabledForTesting(enable_gpu);

  ID3D11Texture2D* out_tex =
      mapper.ToneMapToBgra8(device.Get(), context.Get(), src_tex.Get(), hdr_info);
  if (!out_tex) {
    std::cerr << suite_label << ": ToneMapToBgra8 returned null\n";
    return false;
  }
  if (mapper.last_backend() != expected) {
    std::cerr << suite_label << ": unexpected FP16 backend (got "
              << static_cast<int>(mapper.last_backend()) << ", expected "
              << static_cast<int>(expected) << ")\n";
    return false;
  }

  std::vector<uint8_t> actual_bytes;
  if (!ReadbackBgra8Texture(device.Get(), context.Get(), out_tex, &actual_bytes,
                            nullptr, nullptr)) {
    std::cerr << suite_label << ": ReadbackBgra8Texture failed\n";
    return false;
  }

  std::vector<uint8_t> cpu_ref(kW * kH * 4);
  crossdesk::ConvertScRgbFp16ToBgra8(
      reinterpret_cast<const uint8_t*>(fp16_data.data()), kW * 8,
      cpu_ref.data(), kW * 4, kW, kH,
      hdr_info.ToToneMapParams(DXGI_FORMAT_R16G16B16A16_FLOAT));

  bool ok = true;
  for (int i = 0; i < kW * kH * 4; ++i) {
    if (std::abs(static_cast<int>(actual_bytes[i]) -
                 static_cast<int>(cpu_ref[i])) > 1) {
      std::cerr << suite_label << " FP16 mismatch at byte " << i << ": actual="
                << int(actual_bytes[i]) << " ref=" << int(cpu_ref[i]) << "\n";
      ok = false;
    }
  }

  // Confirmed HDR10 PQ R10G10B10A2_UNORM parity + backend check.
  uint32_t pq_data[4] = {
      PackR10G10B10A2(0.0f, 0.0f, 0.0f),
      PackR10G10B10A2(LinearNitsToPq(80.0f), LinearNitsToPq(80.0f),
                      LinearNitsToPq(80.0f)),
      PackR10G10B10A2(LinearNitsToPq(240.0f), LinearNitsToPq(100.0f),
                      LinearNitsToPq(20.0f)),
      PackR10G10B10A2(LinearNitsToPq(600.0f), LinearNitsToPq(600.0f),
                      LinearNitsToPq(600.0f)),
  };
  D3D11_TEXTURE2D_DESC pq_desc = src_desc;
  pq_desc.Width = 4;
  pq_desc.Height = 1;
  pq_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  D3D11_SUBRESOURCE_DATA pq_init{};
  pq_init.pSysMem = pq_data;
  pq_init.SysMemPitch = 16;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> pq_tex;
  if (FAILED(
          device->CreateTexture2D(&pq_desc, &pq_init, pq_tex.GetAddressOf()))) {
    return false;
  }
  ID3D11Texture2D* pq_out = mapper.ToneMapToBgra8(
      device.Get(), context.Get(), pq_tex.Get(), hdr_info,
      DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
  if (!pq_out || mapper.last_backend() != expected) {
    std::cerr << suite_label << ": PQ backend check failed\n";
    return false;
  }
  std::vector<uint8_t> pq_actual;
  if (!ReadbackBgra8Texture(device.Get(), context.Get(), pq_out, &pq_actual,
                            nullptr, nullptr)) {
    return false;
  }
  uint8_t pq_cpu[16] = {};
  crossdesk::ConvertR10G10B10A2ToBgra8(
      reinterpret_cast<const uint8_t*>(pq_data), 16, pq_cpu, 16, 4, 1,
      hdr_info.ToToneMapParams(DXGI_FORMAT_R10G10B10A2_UNORM,
                               DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020));
  for (int i = 0; i < 16; ++i) {
    if (std::abs(static_cast<int>(pq_actual[i]) - static_cast<int>(pq_cpu[i])) >
        1) {
      std::cerr << suite_label << " PQ mismatch at byte " << i
                << ": actual=" << int(pq_actual[i])
                << " ref=" << int(pq_cpu[i]) << "\n";
      ok = false;
    }
  }

  // Unconfirmed R10G10B10A2_UNORM on the same HDR display must preserve SDR 128.
  uint32_t sdr10_pixel =
      PackR10G10B10A2(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f);
  D3D11_TEXTURE2D_DESC sdr10_desc = pq_desc;
  sdr10_desc.Width = 1;
  sdr10_desc.Height = 1;
  D3D11_SUBRESOURCE_DATA sdr10_init{&sdr10_pixel, 4, 0};
  Microsoft::WRL::ComPtr<ID3D11Texture2D> sdr10_tex;
  if (FAILED(device->CreateTexture2D(&sdr10_desc, &sdr10_init,
                                     sdr10_tex.GetAddressOf()))) {
    return false;
  }
  ID3D11Texture2D* sdr10_out = mapper.ToneMapToBgra8(
      device.Get(), context.Get(), sdr10_tex.Get(), hdr_info);
  if (!sdr10_out || mapper.last_backend() != expected) {
    std::cerr << suite_label << ": unconfirmed SDR10 backend check failed\n";
    return false;
  }
  std::vector<uint8_t> sdr10_actual;
  if (!ReadbackBgra8Texture(device.Get(), context.Get(), sdr10_out,
                            &sdr10_actual, nullptr, nullptr)) {
    return false;
  }
  ok &= ExpectNearByte(suite_label, sdr10_actual[2], 128, 1);

  // Verify Reset() releases allocated conversion textures and CPU buffers.
  ok &= ExpectTrue("Mapper has allocated resources before Reset",
                   mapper.HasAllocatedResources());
  mapper.Reset();
  ok &= ExpectTrue("Mapper releases all resources on Reset",
                   !mapper.HasAllocatedResources());
  return ok;
}
#endif

}  // namespace

int main() {
  bool ok = true;
  ok &= TestSdrFidelityAndWhiteBalance();
  ok &= TestHdrHighlightRollOffAndSafety();
#ifdef _WIN32
  ok &= TestWindowsHdrPolicyAndWhiteScaleResilience();
  ok &= RunToneMapperParitySuite(
      D3D_DRIVER_TYPE_WARP, "WARP GPU", true,
      crossdesk::D3d11HdrToneMapper::Backend::kGpu);
  ok &= RunToneMapperParitySuite(
      D3D_DRIVER_TYPE_HARDWARE, "Hardware GPU", true,
      crossdesk::D3d11HdrToneMapper::Backend::kGpu);
  ok &= RunToneMapperParitySuite(
      D3D_DRIVER_TYPE_WARP, "WARP CPU Fallback", false,
      crossdesk::D3d11HdrToneMapper::Backend::kCpuFallback);
#endif
  if (!ok) {
    return 1;
  }
  std::cout << "windows_hdr_tone_map_test passed\n";
  return 0;
}
