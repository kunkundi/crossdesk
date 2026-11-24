#ifndef _WALLPAPER_PREVIEW_H_
#define _WALLPAPER_PREVIEW_H_

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace crossdesk {

inline constexpr std::string_view kWallpaperPreviewPrefix =
    "crossdesk-recent-wallpaper-v1:";
inline constexpr std::size_t kWallpaperPreviewMaxBytes = 48 * 1024;
inline constexpr int kWallpaperPreviewMaxEdge = 640;

// On macOS this must be called on the main thread.
std::filesystem::path LocalWallpaperPath();
std::string EncodeWallpaperPreview(const std::filesystem::path& path);
bool IsWallpaperPreviewJpeg(std::string_view jpeg);

}  // namespace crossdesk

#endif
