#ifndef _WALLPAPER_PREVIEW_H_
#define _WALLPAPER_PREVIEW_H_

#include <filesystem>
#include <string>

#include "recent_wallpaper.h"

namespace crossdesk {

// On macOS this must be called on the main thread.
std::filesystem::path LocalWallpaperPath();
std::string EncodeWallpaperPreview(const std::filesystem::path& path);
bool IsWallpaperPreviewJpeg(std::string_view jpeg);

}  // namespace crossdesk

#endif
