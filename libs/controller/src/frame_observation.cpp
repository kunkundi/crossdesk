#include "frame_image.h"
#include <stdexcept>

namespace crossdesk {
uint64_t FrameSignature(const DecodedFrame &frame, ImageRegion region) {
  if (!frame.width || !frame.height || (frame.width & 1) || (frame.height & 1) ||
      region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
      uint64_t(region.x) + region.width > frame.width ||
      uint64_t(region.y) + region.height > frame.height ||
      frame.nv12.size() < size_t(frame.width) * frame.height * 3 / 2)
    throw std::invalid_argument("INVALID_REGION");
  uint64_t hash = 14695981039346656037ULL;
  const auto row = [&](size_t offset, int count) {
    for (int x = 0; x < count; ++x) {
      hash ^= frame.nv12[offset + x];
      hash *= 1099511628211ULL;
    }
  };
  for (int y = region.y; y < region.y + region.height; ++y)
    row(size_t(y) * frame.width + region.x, region.width);
  const int chroma_x = region.x & ~1;
  const int chroma_end = (region.x + region.width + 1) & ~1;
  for (int y = region.y / 2; y < (region.y + region.height + 1) / 2; ++y)
    row(size_t(frame.width) * frame.height + size_t(y) * frame.width + chroma_x,
        chroma_end - chroma_x);
  return hash;
}
} // namespace crossdesk
