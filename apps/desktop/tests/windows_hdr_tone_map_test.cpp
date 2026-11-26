#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
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
    const auto pack10 = [](float r, float g, float b) -> uint32_t {
      const uint32_t ri = static_cast<uint32_t>(r * 1023.0f + 0.5f) & 0x3FFu;
      const uint32_t gi = static_cast<uint32_t>(g * 1023.0f + 0.5f) & 0x3FFu;
      const uint32_t bi = static_cast<uint32_t>(b * 1023.0f + 0.5f) & 0x3FFu;
      return ri | (gi << 10) | (bi << 20) | (3u << 30);
    };
    const float pq80 = LinearNitsToPq(80.0f);
    const float pq1000 = LinearNitsToPq(1000.0f);
    uint32_t pq_pixels[2] = {pack10(pq80, pq80, pq80),
                             pack10(pq1000, pq1000, pq1000)};
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
bool TestD3d11GpuToneMapperParity(D3D_DRIVER_TYPE driver_type,
                                  const char* driver_label) {
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
    std::cerr << driver_label << ": D3D11CreateDevice failed, hr=0x" << std::hex
              << static_cast<uint32_t>(hr) << std::dec << "\n";
    return false;
  }

  constexpr int kW = 8;
  constexpr int kH = 2;
  const float samples[kW * kH][3] = {
      {0.0f, 0.0f, 0.0f},       {0.05f, 0.15f, 0.45f},
      {0.21586f, 0.21586f, 0.21586f}, {0.75f, 0.20f, 0.10f},
      {1.0f, 1.0f, 1.0f},       {1.8f, 1.8f, 1.8f},
      {4.5f, 1.6f, 0.2f},       {8.0f, 8.0f, 8.0f},
      {-0.12f, 0.55f, 0.65f},   {0.80f, 0.80f, 0.80f},
      {2.5f, 0.4f, 2.2f},       {0.002f, 0.002f, 0.002f},
      {3.0f, 3.0f, 3.0f},       {5.22f, 5.22f, 5.22f},
      {0.33f, 0.66f, 0.99f},    {10.0f, 10.0f, 10.0f},
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
    std::cerr << driver_label << ": CreateTexture2D failed\n";
    return false;
  }

  crossdesk::WindowsHdrOutputInfo hdr_info;
  hdr_info.hdr_enabled = true;
  hdr_info.color_space = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
  hdr_info.sdr_white_scale = 1.5f;
  hdr_info.max_luminance_nits = 600.0f;

  crossdesk::D3d11HdrToneMapper mapper;
  ID3D11Texture2D* out_tex = mapper.ToneMapToBgra8(
      device.Get(), context.Get(), src_tex.Get(), hdr_info);
  if (!out_tex) {
    std::cerr << driver_label << ": ToneMapToBgra8 returned null\n";
    return false;
  }

  D3D11_TEXTURE2D_DESC stg_desc{};
  out_tex->GetDesc(&stg_desc);
  if (stg_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
    std::cerr << driver_label << ": unexpected output format\n";
    return false;
  }
  stg_desc.Usage = D3D11_USAGE_STAGING;
  stg_desc.BindFlags = 0;
  stg_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  stg_desc.MiscFlags = 0;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  if (FAILED(device->CreateTexture2D(&stg_desc, nullptr,
                                     staging.GetAddressOf()))) {
    return false;
  }
  context->CopyResource(staging.Get(), out_tex);

  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    return false;
  }

  std::vector<uint8_t> cpu_ref(kW * kH * 4);
  crossdesk::ConvertScRgbFp16ToBgra8(
      reinterpret_cast<const uint8_t*>(fp16_data.data()), kW * 8,
      cpu_ref.data(), kW * 4, kW, kH,
      hdr_info.ToToneMapParams(DXGI_FORMAT_R16G16B16A16_FLOAT));

  bool ok = true;
  for (int y = 0; y < kH; ++y) {
    const auto* gpu_row =
        static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch;
    const auto* cpu_row = cpu_ref.data() + y * kW * 4;
    for (int x = 0; x < kW * 4; ++x) {
      if (std::abs(static_cast<int>(gpu_row[x]) -
                   static_cast<int>(cpu_row[x])) > 1) {
        std::cerr << driver_label << " GPU/CPU mismatch at (" << (x / 4) << ","
                  << y << ") ch " << (x % 4) << ": gpu=" << int(gpu_row[x])
                  << " cpu=" << int(cpu_row[x]) << "\n";
        ok = false;
      }
    }
  }
  context->Unmap(staging.Get(), 0);

  // Also verify R10G10B10A2_UNORM HDR10 PQ GPU shader parity.
  const auto pack10 = [](float r, float g, float b) -> uint32_t {
    const uint32_t ri = static_cast<uint32_t>(r * 1023.0f + 0.5f) & 0x3FFu;
    const uint32_t gi = static_cast<uint32_t>(g * 1023.0f + 0.5f) & 0x3FFu;
    const uint32_t bi = static_cast<uint32_t>(b * 1023.0f + 0.5f) & 0x3FFu;
    return ri | (gi << 10) | (bi << 20) | (3u << 30);
  };
  uint32_t pq_data[4] = {
      pack10(0.0f, 0.0f, 0.0f),
      pack10(LinearNitsToPq(80.0f), LinearNitsToPq(80.0f),
             LinearNitsToPq(80.0f)),
      pack10(LinearNitsToPq(240.0f), LinearNitsToPq(100.0f),
             LinearNitsToPq(20.0f)),
      pack10(LinearNitsToPq(600.0f), LinearNitsToPq(600.0f),
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
  if (FAILED(device->CreateTexture2D(&pq_desc, &pq_init, pq_tex.GetAddressOf()))) {
    return false;
  }
  ID3D11Texture2D* pq_out =
      mapper.ToneMapToBgra8(device.Get(), context.Get(), pq_tex.Get(), hdr_info);
  if (!pq_out) {
    return false;
  }
  D3D11_TEXTURE2D_DESC pq_stg_desc{};
  pq_out->GetDesc(&pq_stg_desc);
  pq_stg_desc.Usage = D3D11_USAGE_STAGING;
  pq_stg_desc.BindFlags = 0;
  pq_stg_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> pq_stg;
  if (FAILED(device->CreateTexture2D(&pq_stg_desc, nullptr, pq_stg.GetAddressOf()))) {
    return false;
  }
  context->CopyResource(pq_stg.Get(), pq_out);
  D3D11_MAPPED_SUBRESOURCE pq_map{};
  if (FAILED(context->Map(pq_stg.Get(), 0, D3D11_MAP_READ, 0, &pq_map))) {
    return false;
  }
  uint8_t pq_cpu[16] = {};
  crossdesk::ConvertR10G10B10A2ToBgra8(
      reinterpret_cast<const uint8_t*>(pq_data), 16, pq_cpu, 16, 4, 1,
      hdr_info.ToToneMapParams(DXGI_FORMAT_R10G10B10A2_UNORM));
  const auto* pq_gpu = static_cast<const uint8_t*>(pq_map.pData);
  for (int i = 0; i < 16; ++i) {
    if (std::abs(static_cast<int>(pq_gpu[i]) - static_cast<int>(pq_cpu[i])) > 1) {
      std::cerr << driver_label << " PQ GPU/CPU mismatch at byte " << i
                << ": gpu=" << int(pq_gpu[i]) << " cpu=" << int(pq_cpu[i])
                << "\n";
      ok = false;
    }
  }
  context->Unmap(pq_stg.Get(), 0);
  return ok;
}
#endif

}  // namespace

int main() {
  bool ok = true;
  ok &= TestSdrFidelityAndWhiteBalance();
  ok &= TestHdrHighlightRollOffAndSafety();
#ifdef _WIN32
  ok &= TestD3d11GpuToneMapperParity(D3D_DRIVER_TYPE_WARP, "WARP");
  ok &= TestD3d11GpuToneMapperParity(D3D_DRIVER_TYPE_HARDWARE, "Hardware");
#endif
  if (!ok) {
    return 1;
  }
  std::cout << "windows_hdr_tone_map_test passed\n";
  return 0;
}
