#ifndef _RECENT_WALLPAPER_H_
#define _RECENT_WALLPAPER_H_

#include <cstddef>
#include <string_view>

namespace crossdesk {

inline constexpr std::string_view kWallpaperPreviewPrefix =
    "crossdesk-recent-wallpaper-v1:";
inline constexpr std::size_t kWallpaperPreviewMaxBytes = 48 * 1024;
inline constexpr int kWallpaperPreviewMaxEdge = 640;

inline bool IsWallpaperPreviewMessage(std::string_view message) {
  return message.substr(0, kWallpaperPreviewPrefix.size()) ==
         kWallpaperPreviewPrefix;
}

// The host sends baseline JPEG. Bound the dimensions before passing the
// encoded image to a platform decoder.
inline bool HasWallpaperPreviewDimensions(std::string_view jpeg) {
  if (jpeg.size() < 4 || jpeg.size() > kWallpaperPreviewMaxBytes ||
      jpeg.substr(0, 2) != "\xff\xd8" ||
      jpeg.substr(jpeg.size() - 2) != "\xff\xd9")
    return false;
  const auto byte = [&](std::size_t offset) {
    return static_cast<unsigned char>(jpeg[offset]);
  };
  const auto word = [&](std::size_t offset) {
    return (int(byte(offset)) << 8) | byte(offset + 1);
  };
  for (std::size_t cursor = 2; cursor + 4 <= jpeg.size();) {
    if (byte(cursor++) != 0xff) return false;
    while (cursor < jpeg.size() && byte(cursor) == 0xff) ++cursor;
    if (jpeg.size() - cursor < 3) return false;
    const auto marker = byte(cursor++);
    if (marker == 0xda || marker == 0xd9) return false;
    const auto length = static_cast<std::size_t>(word(cursor));
    if (length < 2 || length > jpeg.size() - cursor) return false;
    if (marker == 0xc0) {
      if (length < 8 || byte(cursor + 2) != 8) return false;
      const int components = byte(cursor + 7);
      if ((components != 1 && components != 3) ||
          length != static_cast<std::size_t>(8 + 3 * components))
        return false;
      const int height = word(cursor + 3);
      const int width = word(cursor + 5);
      return width > 0 && height > 0 && width <= kWallpaperPreviewMaxEdge &&
             height <= kWallpaperPreviewMaxEdge;
    }
    cursor += length;
  }
  return false;
}

inline std::string_view WallpaperPreviewFromMessage(std::string_view message) {
  if (!IsWallpaperPreviewMessage(message)) return {};
  const auto jpeg = message.substr(kWallpaperPreviewPrefix.size());
  return HasWallpaperPreviewDimensions(jpeg) ? jpeg : std::string_view{};
}

}  // namespace crossdesk

#endif
