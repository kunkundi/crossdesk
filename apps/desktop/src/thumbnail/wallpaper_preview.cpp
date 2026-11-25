#include "wallpaper_preview.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <vector>

#include "libyuv.h"
#include "stb_image.h"
#include "stb_image_write.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__) && !defined(__OHOS__)
#include <cstdio>
#include <cstdlib>
#endif

namespace crossdesk {

#ifdef __APPLE__
std::vector<unsigned char> LoadMacWallpaper(const std::filesystem::path& path,
                                            int& width, int& height);
#endif

namespace {

using Pixels = std::vector<unsigned char>;

#ifndef __APPLE__
Pixels LoadWallpaper(const std::filesystem::path& path, int& width,
                     int& height) {
  constexpr std::size_t kMaxSourceBytes = 64 * 1024 * 1024;
  constexpr int kMaxSourcePixels = 40 * 1024 * 1024;
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) return {};
  const auto size = std::filesystem::file_size(path, error);
  if (error || size == 0 || size > kMaxSourceBytes) return {};
  std::ifstream input(path, std::ios::binary);
  Pixels file(static_cast<std::size_t>(size));
  if (!input.read(reinterpret_cast<char*>(file.data()),
                  static_cast<std::streamsize>(file.size())))
    return {};

  int source_width = 0, source_height = 0;
  const int length = static_cast<int>(file.size());
  if (!stbi_info_from_memory(file.data(), length, &source_width, &source_height,
                             nullptr) ||
      source_width <= 0 || source_height <= 0 ||
      source_width > kMaxSourcePixels / source_height)
    return {};

  const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> source(
      stbi_load_from_memory(file.data(), length, &source_width, &source_height,
                            nullptr, 4),
      stbi_image_free);
  if (!source) return {};

  const int edge = std::max({kWallpaperPreviewMaxEdge, source_width, source_height});
  width = std::max(1, int(int64_t(source_width) * kWallpaperPreviewMaxEdge / edge));
  height = std::max(1, int(int64_t(source_height) * kWallpaperPreviewMaxEdge / edge));
  Pixels pixels(static_cast<std::size_t>(width) * height * 4);
  if (libyuv::ARGBScale(source.get(), source_width * 4, source_width,
                        source_height, pixels.data(), width * 4, width, height,
                        libyuv::kFilterBox) != 0)
    return {};
  return pixels;
}
#endif

#if defined(__linux__) && !defined(__OHOS__)
std::string ReadSetting(const char* command) {
  FILE* process = popen(command, "r");
  if (!process) return {};
  std::array<char, 8192> buffer{};
  const bool read = fgets(buffer.data(), buffer.size(), process) != nullptr;
  const int status = pclose(process);
  if (!read || status != 0) return {};
  std::string value(buffer.data());
  // gsettings prints a quoted GVariant string followed by a newline.
  if (value.size() < 3 || value.front() != '\'' ||
      value.compare(value.size() - 2, 2, "'\n") != 0)
    return {};
  return value.substr(1, value.size() - 3);
}

std::filesystem::path LocalFileUri(std::string_view uri) {
  if (uri.substr(0, 8) != "file:///") return {};
  uri.remove_prefix(7);
  std::string path;
  for (std::size_t i = 0; i < uri.size(); ++i) {
    char byte = uri[i];
    if (byte == '%') {
      if (i + 2 >= uri.size()) return {};
      const auto hex = [](char digit) -> int {
        if (digit >= '0' && digit <= '9') return digit - '0';
        if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
        if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
        return -1;
      };
      const int high = hex(uri[++i]);
      const int low = hex(uri[++i]);
      if (high < 0 || low < 0) return {};
      byte = static_cast<char>(high * 16 + low);
    } else if (byte == '\\') {
      return {};
    }
    if (byte == '\0') return {};
    path.push_back(byte);
  }
  return path;
}
#endif

void AppendJpeg(void* context, void* data, int size) {
  auto& jpeg = *static_cast<std::string*>(context);
  jpeg.append(static_cast<const char*>(data), static_cast<std::size_t>(size));
}

std::string EncodeJpeg(const Pixels& pixels, int width, int height, int quality) {
  std::string jpeg;
  if (!stbi_write_jpg_to_func(AppendJpeg, &jpeg, width, height, 4, pixels.data(),
                              quality) ||
      jpeg.size() > kWallpaperPreviewMaxBytes)
    return {};
  return jpeg;
}

}  // namespace

#ifndef __APPLE__
std::filesystem::path LocalWallpaperPath() {
#ifdef _WIN32
  wchar_t wallpaper[MAX_PATH]{};
  if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, wallpaper, 0))
    return {};
  const std::filesystem::path path(wallpaper);
  if (path.is_absolute() && GetDriveTypeW(path.root_path().c_str()) == DRIVE_FIXED)
    return path;
#elif defined(__linux__) && !defined(__OHOS__)
  const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
  if (!desktop || (":" + std::string(desktop) + ":").find(":GNOME:") ==
                      std::string::npos)
    return {};
  if (ReadSetting("/usr/bin/timeout 2s /usr/bin/gsettings get "
                  "org.gnome.desktop.interface color-scheme 2>/dev/null") ==
      "prefer-dark") {
    auto dark = LocalFileUri(ReadSetting(
        "/usr/bin/timeout 2s /usr/bin/gsettings get "
        "org.gnome.desktop.background picture-uri-dark 2>/dev/null"));
    if (!dark.empty()) return dark;
  }
  return LocalFileUri(ReadSetting("/usr/bin/timeout 2s /usr/bin/gsettings get "
                                  "org.gnome.desktop.background picture-uri 2>/dev/null"));
#endif
  return {};
}
#endif

std::string EncodeWallpaperPreview(const std::filesystem::path& path) {
  if (path.empty()) return {};
  int width = 0, height = 0;
#ifdef __APPLE__
  const auto pixels = LoadMacWallpaper(path, width, height);
#else
  const auto pixels = LoadWallpaper(path, width, height);
#endif
  if (pixels.empty()) return {};

  for (const int quality : {75, 55, 35}) {
    auto jpeg = EncodeJpeg(pixels, width, height, quality);
    if (!jpeg.empty()) return jpeg;
  }

  // Keep detailed wallpapers within a single bounded control message.
  const int smaller_width = std::max(1, width / 2);
  const int smaller_height = std::max(1, height / 2);
  Pixels smaller(static_cast<std::size_t>(smaller_width) * smaller_height * 4);
  if (libyuv::ARGBScale(pixels.data(), width * 4, width, height, smaller.data(),
                        smaller_width * 4, smaller_width, smaller_height,
                        libyuv::kFilterBox) != 0)
    return {};
  return EncodeJpeg(smaller, smaller_width, smaller_height, 35);
}

bool IsWallpaperPreviewJpeg(std::string_view jpeg) {
  if (jpeg.size() < 4 || jpeg.size() > kWallpaperPreviewMaxBytes ||
      jpeg.substr(0, 2) != "\xff\xd8" ||
      jpeg.substr(jpeg.size() - 2) != "\xff\xd9")
    return false;
  const auto* bytes = reinterpret_cast<const unsigned char*>(jpeg.data());
  const auto size = static_cast<int>(jpeg.size());
  int width = 0, height = 0;
  if (!stbi_info_from_memory(bytes, size, &width, &height, nullptr) ||
      width <= 0 || height <= 0 || width > kWallpaperPreviewMaxEdge ||
      height > kWallpaperPreviewMaxEdge)
    return false;
  const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
      stbi_load_from_memory(bytes, size, &width, &height, nullptr, 4),
      stbi_image_free);
  return pixels != nullptr;
}

}  // namespace crossdesk
