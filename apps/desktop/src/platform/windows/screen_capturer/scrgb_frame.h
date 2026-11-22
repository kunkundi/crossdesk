/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-23
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCRGB_FRAME_H_
#define _SCRGB_FRAME_H_

#include "hdr_video_frame.h"

namespace crossdesk {

// DXGI and Windows.Graphics.Capture FP16 surfaces contain linear scRGB,
// with BT.709 primaries and 80 nits per unit of channel intensity.
struct ScRgbFrame {
  const uint8_t* bytes;
  size_t pitch;
  int width, height;

  minirtc::HdrRgb Pixel(int x, int y) const {
    static const auto half = [] {
      std::array<float, 65536> table{};
      for (unsigned i = 0; i < table.size(); ++i) {
        int e = (i >> 10) & 31;
        float m = static_cast<float>(i & 1023);
        float f = e == 0 ? std::ldexp(m, -24) : e == 31 ? 0.f : std::ldexp(m + 1024.f, e - 25);
        table[i] = (i & 32768) ? -f : f;
      }
      return table;
    }();
    auto p = reinterpret_cast<const uint16_t*>(bytes + y * pitch) + 4 * x;
    return {half[p[0]], half[p[1]], half[p[2]]};
  }

  void CopyBgra(float white, std::vector<uint8_t>& bgra) const {
    bgra.resize(size_t(width) * height * 4);
    float scale = 80.f / white;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        auto c = Pixel(x, y);
        auto* p = bgra.data() + (size_t(y) * width + x) * 4;
        p[0] = static_cast<uint8_t>(.5f + 255.f * minirtc::LinearToSdr(c.b * scale));
        p[1] = static_cast<uint8_t>(.5f + 255.f * minirtc::LinearToSdr(c.g * scale));
        p[2] = static_cast<uint8_t>(.5f + 255.f * minirtc::LinearToSdr(c.r * scale));
        p[3] = 255;
      }
    }
  }

  // Map logical desktop coordinates back to the unrotated duplication surface.
  std::shared_ptr<minirtc::HdrVideoFrame> ToHdr10(int w, int h, int rotation, float white,
                                                  const uint8_t* original = nullptr, int stride = 0,
                                                  const uint8_t* with_cursor = nullptr) const {
    auto result = minirtc::HdrVideoFrame::Create(w, h, white);
    if (!result) {
      return {};
    }
    auto read = [&](int x, int y) {
      int sx = x, sy = y;
      if (rotation == 90) {
        sx = y;
        sy = height - 1 - x;
      }
      if (rotation == 180) {
        sx = width - 1 - x;
        sy = height - 1 - y;
      }
      if (rotation == 270) {
        sx = width - 1 - y;
        sy = x;
      }
      auto c = Pixel(sx, sy);
      if (with_cursor) {
        auto p = with_cursor + (size_t(y) * w + x) * 4;
        if (std::memcmp(p, original + size_t(y) * stride + x * 4, 3) != 0) {
          auto linear = [white](uint8_t v) {
            float s = v / 255.f;
            return (s <= .04045f ? s / 12.92f : std::pow((s + .055f) / 1.055f, 2.4f)) * white /
                   80.f;
          };
          c = {linear(p[2]), linear(p[1]), linear(p[0])};
        }
      }
      using minirtc::ScRgbToPq;
      return minirtc::HdrRgb{ScRgbToPq(.627404f * c.r + .329283f * c.g + .043313f * c.b),
                             ScRgbToPq(.069097f * c.r + .919540f * c.g + .011362f * c.b),
                             ScRgbToPq(.016391f * c.r + .088013f * c.g + .895595f * c.b)};
    };
    for (int y = 0; y < h; y += 2) {
      for (int x = 0; x < w; x += 2) {
        result->WritePqBlock(x, y, read);
      }
    }
    return result;
  }
};

}  // namespace crossdesk

#endif
