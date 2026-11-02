#include "frame_image.h"
#include <algorithm>
#include <libyuv.h>
#include <png.h>
#include <stdexcept>

namespace crossdesk {
FrameImage EncodeFrameImage(const DecodedFrame &f, ImageRegion r, int max_edge,
                            size_t max_bytes) {
  if (!f.width || !f.height || f.width > 8192 || f.height > 8192 ||
      (f.width & 1) || (f.height & 1) ||
      uint64_t(f.width) * f.height > 16777216 ||
      f.nv12.size() != size_t(f.width) * f.height * 3 / 2 || max_edge < 64 ||
      max_edge > 2048 || max_bytes < 4096 || max_bytes > 2 * 1024 * 1024)
    throw std::invalid_argument("INVALID_IMAGE_REQUEST");
  if (!r.width && !r.height)
    r = {0, 0, int(f.width), int(f.height)};
  if (r.x < 0 || r.y < 0 || r.width < 1 || r.height < 1 ||
      r.width > int(f.width) || r.height > int(f.height) ||
      r.x > int(f.width) - r.width || r.y > int(f.height) - r.height)
    throw std::invalid_argument("INVALID_REGION");
  std::vector<uint8_t> argb(size_t(f.width) * f.height * 4);
  if (libyuv::NV12ToARGB(f.nv12.data(), f.width,
                         f.nv12.data() + size_t(f.width) * f.height, f.width,
                         argb.data(), f.width * 4, f.width, f.height))
    throw std::runtime_error("IMAGE_CONVERSION_FAILED");
  FrameImage result;
  result.region = r;
  for (int edge = max_edge;; edge = std::max(32, edge * 3 / 4)) {
    const double scale =
        std::min(1.0, double(edge) / std::max(r.width, r.height));
    result.width = std::max(1, int(r.width * scale));
    result.height = std::max(1, int(r.height * scale));
    std::vector<uint8_t> scaled(size_t(result.width) * result.height * 4);
    std::vector<uint8_t> rgb(size_t(result.width) * result.height * 3);
    if (libyuv::ARGBScale(argb.data() + (size_t(r.y) * f.width + r.x) * 4,
                          f.width * 4, r.width, r.height, scaled.data(),
                          result.width * 4, result.width, result.height,
                          libyuv::kFilterBox) ||
        libyuv::ARGBToRAW(scaled.data(), result.width * 4, rgb.data(),
                          result.width * 3, result.width, result.height))
      throw std::runtime_error("IMAGE_CONVERSION_FAILED");
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = result.width;
    image.height = result.height;
    image.format = PNG_FORMAT_RGB;
    png_alloc_size_t size = 0;
    if (!png_image_write_to_memory(&image, nullptr, &size, 0, rgb.data(), 0,
                                   nullptr)) {
      png_image_free(&image);
      throw std::runtime_error("IMAGE_ENCODING_FAILED");
    }
    if (size <= max_bytes) {
      result.png.resize(size);
      const auto ok = png_image_write_to_memory(
          &image, result.png.data(), &size, 0, rgb.data(), 0, nullptr);
      png_image_free(&image);
      if (!ok)
        throw std::runtime_error("IMAGE_ENCODING_FAILED");
      result.png.resize(size);
      return result;
    }
    png_image_free(&image);
    if (edge == 32)
      throw std::runtime_error("IMAGE_TOO_LARGE");
  }
}
std::string ImageBase64(const std::vector<uint8_t> &data) {
  constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    const uint32_t n = (uint32_t(data[i]) << 16) |
                       (i + 1 < data.size() ? uint32_t(data[i + 1]) << 8 : 0) |
                       (i + 2 < data.size() ? data[i + 2] : 0);
    out += alphabet[n >> 18];
    out += alphabet[(n >> 12) & 63];
    out += i + 1 < data.size() ? alphabet[(n >> 6) & 63] : '=';
    out += i + 2 < data.size() ? alphabet[n & 63] : '=';
  }
  return out;
}
} // namespace crossdesk
