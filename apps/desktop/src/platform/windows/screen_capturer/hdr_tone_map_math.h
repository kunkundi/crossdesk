#ifndef _HDR_TONE_MAP_MATH_H_
#define _HDR_TONE_MAP_MATH_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crossdesk {

enum class HdrInputMode : uint32_t {
  kScRgbFp16 = 0,
  kPqBt2020 = 1,
  kPassthroughSdr = 2,
};

struct HdrToneMapParams {
  float sdr_white_scale = 1.0f;
  float max_luminance_nits = 400.0f;
  HdrInputMode mode = HdrInputMode::kScRgbFp16;
};

struct HdrShaderConstants {
  float inv_sdr_white_scale = 1.0f;
  float max_headroom = 5.0f;
  float shoulder_c = 0.0f;
  uint32_t mode = 0;
};

static_assert(sizeof(HdrShaderConstants) == 16,
              "HdrShaderConstants must match 16-byte HLSL cbuffer layout");

inline constexpr float kHdrToneMapKnee = 0.80f;

inline HdrShaderConstants ComputeHdrShaderConstants(
    const HdrToneMapParams& params) noexcept {
  float sdr_scale = params.sdr_white_scale;
  if (!(sdr_scale > 0.0f)) {
    sdr_scale = 1.0f;
  }
  sdr_scale = (std::max)(0.5f, (std::min)(sdr_scale, 12.5f));

  const float sdr_nits = sdr_scale * 80.0f;
  float max_nits = params.max_luminance_nits;
  if (!(max_nits > 80.0f)) {
    max_nits = (std::max)(400.0f, sdr_nits * 2.0f);
  } else {
    max_nits = (std::max)(max_nits, sdr_nits);
  }

  const float max_headroom = (std::max)(max_nits / sdr_nits, 1.0f);
  float shoulder_c = 0.0f;
  if (max_headroom > 1.0001f) {
    const float out_span = 1.0f - kHdrToneMapKnee;
    const float in_span = max_headroom - kHdrToneMapKnee;
    shoulder_c = (in_span - out_span) / (out_span * in_span);
  }

  HdrShaderConstants constants;
  constants.inv_sdr_white_scale = 1.0f / sdr_scale;
  constants.max_headroom = max_headroom;
  constants.shoulder_c = shoulder_c;
  constants.mode = static_cast<uint32_t>(params.mode);
  return constants;
}

inline float HalfToFloat(uint16_t h) noexcept {
  const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
  uint32_t exp = (h >> 10) & 0x1Fu;
  uint32_t mant = h & 0x03FFu;
  uint32_t bits = 0;
  if (exp == 0) {
    if (mant == 0) {
      bits = sign;
    } else {
      exp = 1;
      while ((mant & 0x0400u) == 0) {
        mant <<= 1;
        --exp;
      }
      mant &= 0x03FFu;
      bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
  } else if (exp == 31) {
    bits = sign | 0x7F800000u | (mant << 13);
  } else {
    bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
  }
  float result = 0.0f;
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}

inline uint16_t FloatToHalf(float value) noexcept {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  const uint16_t sign = static_cast<uint16_t>((bits >> 16) & 0x8000u);
  const int32_t exp = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
  uint32_t mant = bits & 0x007FFFFFu;

  if (((bits >> 23) & 0xFFu) == 0xFFu) {
    if (mant != 0) {
      return static_cast<uint16_t>(sign | 0x7E00u);
    }
    return static_cast<uint16_t>(sign | 0x7C00u);
  }
  if (exp <= 0) {
    if (exp < -10) {
      return sign;
    }
    mant = (mant | 0x00800000u) >> (1 - exp);
    return static_cast<uint16_t>(sign | ((mant + 0x00001000u) >> 13));
  }
  if (exp >= 31) {
    return static_cast<uint16_t>(sign | 0x7C00u);
  }
  const uint32_t rounded = mant + 0x00001000u;
  if (rounded & 0x00800000u) {
    if (exp + 1 >= 31) {
      return static_cast<uint16_t>(sign | 0x7C00u);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp + 1) << 10));
  }
  return static_cast<uint16_t>(
      sign | (static_cast<uint32_t>(exp) << 10) | (rounded >> 13));
}

inline float SanitizeHdrChannel(float x) noexcept {
  uint32_t bits = 0;
  std::memcpy(&bits, &x, sizeof(bits));
  if ((bits & 0x7F800000u) == 0x7F800000u) {
    if ((bits & 0x007FFFFFu) != 0u) {
      return 0.0f;
    }
    return (bits & 0x80000000u) != 0u ? 0.0f : 1000.0f;
  }
  return (std::max)(-65504.0f, (std::min)(x, 65504.0f));
}

inline float PqChannelToLinear10000(float pq) noexcept {
  const float n = (std::max)(0.0f, (std::min)(pq, 1.0f));
  constexpr float kM1 = 0.1593017578125f;
  constexpr float kM2 = 78.84375f;
  constexpr float kC1 = 0.8359375f;
  constexpr float kC2 = 18.8515625f;
  constexpr float kC3 = 18.6875f;
  const float np = std::pow(n, 1.0f / kM2);
  const float num = (std::max)(np - kC1, 0.0f);
  const float den = (std::max)(kC2 - kC3 * np, 1e-6f);
  return std::pow(num / den, 1.0f / kM1);
}

inline void PqBt2020ToScRgb80Nits(float& r, float& g, float& b) noexcept {
  // 1.0 in SMPTE ST.2084 is 10,000 nits, which equals 125.0 in 80-nit scRGB.
  const float r2020 = PqChannelToLinear10000(r) * 125.0f;
  const float g2020 = PqChannelToLinear10000(g) * 125.0f;
  const float b2020 = PqChannelToLinear10000(b) * 125.0f;

  r =  1.6604910f * r2020 - 0.5876411f * g2020 - 0.0728499f * b2020;
  g = -0.1245505f * r2020 + 1.1328999f * g2020 - 0.0083494f * b2020;
  b = -0.0181508f * r2020 - 0.1005789f * g2020 + 1.1187297f * b2020;
}

inline void GamutCompressBt709(float& r, float& g, float& b) noexcept {
  const float min_c = (std::min)(r, (std::min)(g, b));
  if (min_c < 0.0f) {
    const float y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    if (y > 0.0f) {
      const float t = y / (std::max)(y - min_c, 1e-6f);
      r = y + t * (r - y);
      g = y + t * (g - y);
      b = y + t * (b - y);
    } else {
      r = 0.0f;
      g = 0.0f;
      b = 0.0f;
    }
  }
  r = (std::max)(r, 0.0f);
  g = (std::max)(g, 0.0f);
  b = (std::max)(b, 0.0f);
}

inline float ApplyHdrShoulderCurve(float x, float max_headroom,
                                   float shoulder_c) noexcept {
  if (x <= kHdrToneMapKnee) {
    return x;
  }
  if (shoulder_c <= 0.0f) {
    return (std::min)(x, 1.0f);
  }
  const float clamped = (std::min)(x, max_headroom);
  const float u = clamped - kHdrToneMapKnee;
  const float mapped =
      kHdrToneMapKnee + u / (std::max)(1.0f + shoulder_c * u, 1e-6f);
  return (std::max)(0.0f, (std::min)(mapped, 1.0f));
}

inline uint8_t LinearToSrgbByte(float lin) noexcept {
  const float clamped = (std::max)(0.0f, (std::min)(lin, 1.0f));
  const float srgb =
      (clamped <= 0.0031308f)
          ? (clamped * 12.92f)
          : (1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f);
  const float byte_val =
      (std::max)(0.0f, (std::min)(srgb * 255.0f + 0.5f, 255.0f));
  return static_cast<uint8_t>(byte_val);
}

inline uint8_t UnormFloatToByte(float v) noexcept {
  const float clamped = (std::max)(0.0f, (std::min)(v, 1.0f));
  return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
}

inline void ToneMapPixelToBgra8(float r, float g, float b,
                                const HdrShaderConstants& constants,
                                uint8_t out_bgra[4]) noexcept {
  if (constants.mode == static_cast<uint32_t>(HdrInputMode::kPassthroughSdr)) {
    out_bgra[0] = UnormFloatToByte(b);
    out_bgra[1] = UnormFloatToByte(g);
    out_bgra[2] = UnormFloatToByte(r);
    out_bgra[3] = 255;
    return;
  }

  r = SanitizeHdrChannel(r);
  g = SanitizeHdrChannel(g);
  b = SanitizeHdrChannel(b);

  if (constants.mode == static_cast<uint32_t>(HdrInputMode::kPqBt2020)) {
    PqBt2020ToScRgb80Nits(r, g, b);
  }

  GamutCompressBt709(r, g, b);
  r *= constants.inv_sdr_white_scale;
  g *= constants.inv_sdr_white_scale;
  b *= constants.inv_sdr_white_scale;

  const float peak = (std::max)(r, (std::max)(g, b));
  if (peak > kHdrToneMapKnee) {
    const float mapped_peak = ApplyHdrShoulderCurve(
        peak, constants.max_headroom, constants.shoulder_c);
    const float scale = mapped_peak / (std::max)(peak, 1e-6f);
    float hr = r * scale;
    float hg = g * scale;
    float hb = b * scale;
    if (peak > 1.0f && constants.max_headroom > 1.0f) {
      const float norm_hdr = (std::max)(
          0.0f,
          (std::min)((peak - 1.0f) /
                         (std::max)(constants.max_headroom - 1.0f, 1e-6f),
                     1.0f));
      const float desat = 0.35f * norm_hdr * norm_hdr;
      hr += (mapped_peak - hr) * desat;
      hg += (mapped_peak - hg) * desat;
      hb += (mapped_peak - hb) * desat;
    }
    r = hr;
    g = hg;
    b = hb;
  }

  out_bgra[0] = LinearToSrgbByte(b);
  out_bgra[1] = LinearToSrgbByte(g);
  out_bgra[2] = LinearToSrgbByte(r);
  out_bgra[3] = 255;
}

inline bool ConvertScRgbFp16ToBgra8(const uint8_t* src, int src_stride,
                                    uint8_t* dst, int dst_stride, int width,
                                    int height,
                                    const HdrToneMapParams& params) noexcept {
  if (!src || !dst || width <= 0 || height <= 0 || src_stride < width * 8 ||
      dst_stride < width * 4) {
    return false;
  }
  const HdrShaderConstants constants = ComputeHdrShaderConstants(params);
  for (int y = 0; y < height; ++y) {
    const auto* src_row = reinterpret_cast<const uint16_t*>(
        src + static_cast<std::size_t>(y) * src_stride);
    uint8_t* dst_row = dst + static_cast<std::size_t>(y) * dst_stride;
    for (int x = 0; x < width; ++x) {
      const float r = HalfToFloat(src_row[x * 4 + 0]);
      const float g = HalfToFloat(src_row[x * 4 + 1]);
      const float b = HalfToFloat(src_row[x * 4 + 2]);
      ToneMapPixelToBgra8(r, g, b, constants, dst_row + x * 4);
    }
  }
  return true;
}

inline bool ConvertR10G10B10A2ToBgra8(const uint8_t* src, int src_stride,
                                      uint8_t* dst, int dst_stride, int width,
                                      int height,
                                      const HdrToneMapParams& params) noexcept {
  if (!src || !dst || width <= 0 || height <= 0 || src_stride < width * 4 ||
      dst_stride < width * 4) {
    return false;
  }
  const HdrShaderConstants constants = ComputeHdrShaderConstants(params);
  constexpr float kInv1023 = 1.0f / 1023.0f;
  for (int y = 0; y < height; ++y) {
    const auto* src_row = reinterpret_cast<const uint32_t*>(
        src + static_cast<std::size_t>(y) * src_stride);
    uint8_t* dst_row = dst + static_cast<std::size_t>(y) * dst_stride;
    for (int x = 0; x < width; ++x) {
      const uint32_t packed = src_row[x];
      const float r = static_cast<float>((packed >> 0) & 0x3FFu) * kInv1023;
      const float g = static_cast<float>((packed >> 10) & 0x3FFu) * kInv1023;
      const float b = static_cast<float>((packed >> 20) & 0x3FFu) * kInv1023;
      ToneMapPixelToBgra8(r, g, b, constants, dst_row + x * 4);
    }
  }
  return true;
}

}  // namespace crossdesk

#endif  // _HDR_TONE_MAP_MATH_H_
