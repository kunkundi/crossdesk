#include <chrono>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <random>

#include "stb_image_write.h"
#include "thumbnail.h"
#include "wallpaper_preview.h"

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

struct TemporaryDirectory {
  std::filesystem::path path;
  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

}  // namespace

int main() {
  using crossdesk::Thumbnail;
  const auto scratch = std::filesystem::temp_directory_path() /
      ("crossdesk-wallpaper-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  if (!std::filesystem::create_directory(scratch)) return 1;
  const TemporaryDirectory cleanup{scratch};
  const auto source = scratch / "wallpaper.png";
  const unsigned char red[] = {255, 0, 0, 255, 0, 0};
  if (!stbi_write_png(source.string().c_str(), 2, 1, 3, red, 6)) return 1;
  const auto jpeg = crossdesk::EncodeWallpaperPreview(source);
  const auto message = std::string(crossdesk::kWallpaperPreviewPrefix) + jpeg;
  if (!Check(crossdesk::WallpaperPreviewFromMessage(message) == jpeg &&
                 crossdesk::IsWallpaperPreviewJpeg(jpeg),
             "fixture wallpaper must encode as a valid cover"))
    return 1;

  // user gets a preview even for a detailed wallpaper
  // Given a large wallpaper with detail at every pixel
  // When it is resized and encoded for the recent connection card
  // Then the cover remains a decodable image within the transfer limit
  const auto detailed = scratch / "detailed.png";
  std::mt19937 random(17);
  std::vector<unsigned char> noise(640 * 640 * 3);
  for (auto& pixel : noise) pixel = static_cast<unsigned char>(random());
  if (!stbi_write_png(detailed.string().c_str(), 640, 640, 3, noise.data(),
                      640 * 3) ||
      !Check(crossdesk::IsWallpaperPreviewJpeg(
                 crossdesk::EncodeWallpaperPreview(detailed)),
             "a detailed wallpaper should still fit in a valid cover"))
    return 1;

  const auto cache = scratch / "history";
  Thumbnail history(cache.string());
  std::vector<std::pair<std::string, Thumbnail::RecentConnection>> entries;
  const Thumbnail::LanTarget lan{"192.0.2.1", std::string(32, 'a'), 52888};
  const auto lan_id = Thumbnail::LanRecordId(lan.device_id);
  const char frame[]{64, 64, 64, 64, static_cast<char>(128), static_cast<char>(128)};
  constexpr auto cloud = "123456789";

  // user sees each device's own cover after reconnecting
  // Given an older cloud device and a LAN device with a desktop wallpaper
  // When both devices are connected and their session previews become available
  // Then the LAN cover shows its wallpaper and the cloud cover shows its frame
  if (!Check(history.SaveToThumbnail(frame, 2, 2, cloud, "Legacy", "") == 0 &&
                 history.SaveToThumbnail(nullptr, 0, 0, lan_id, "LAN", "ABC123",
                     crossdesk::HostPlatform::MacOS, &lan, jpeg) == 0 &&
                 history.SaveToThumbnail(frame, 2, 2, lan_id, "", "ABC123",
                     crossdesk::HostPlatform::Unknown, &lan, jpeg.substr(0, 8)) == 0,
             "could not store recent connections"))
    return 1;
  if (!Check(history.LoadThumbnail(entries) == 0 && entries.size() == 2,
             "both devices should appear in recent connections"))
    return 1;
  for (const auto& recent : entries) {
    const auto& entry = recent.second;
    std::vector<unsigned char> rgba;
    int width = 0, height = 0;
    if (!Check(history.DecodeImage(entry.image_path, 2, 1, &rgba, &width, &height),
               "both covers should decode"))
      return 1;
    if (entry.remote_id == lan_id) {
      if (!Check(width == 2 && height == 1 && rgba[0] > 230 && rgba[1] < 30,
                 "closing the session must preserve the red wallpaper"))
        return 1;
    } else if (!Check(entry.remote_id == cloud && rgba[0] == rgba[1],
                      "an older host should keep its grayscale frame")) {
      return 1;
    }
  }

  // user sees wallpapers with session previews disabled
  // Given a device with a wallpaper and an older device with a saved frame
  // When the user disables screen previews and reconnects
  // Then the wallpaper is still available and the older frame is removed
  if (!Check(history.SetSavePreviews(false) == 0 &&
                 history.LoadThumbnail(entries) == 0 && entries.size() == 2 &&
                 std::filesystem::is_regular_file(cache / (lan_id + ".jpg")) &&
                 !std::filesystem::exists(cache / "123456789.png") &&
                 history.SaveToThumbnail(frame, 2, 2, cloud, "Cloud", "",
                     crossdesk::HostPlatform::MacOS, nullptr, jpeg) == 0 &&
                 history.LoadThumbnail(entries) == 0 && entries.size() == 2 &&
                 !entries[0].second.image_path.empty() &&
                 !entries[1].second.image_path.empty(),
             "wallpapers must be saved and loaded with screen previews disabled"))
    return 1;

  // user removes a LAN connection and its wallpaper
  // Given a LAN device with a saved cover
  // When the user deletes that connection
  // Then its cover and connection record are removed
  return Check(history.DeleteThumbnail(lan_id) == 0 &&
                   history.LoadThumbnail(entries) == 0 && entries.size() == 1 &&
                   entries[0].second.remote_id == cloud &&
                   std::distance(std::filesystem::directory_iterator(cache),
                                 std::filesystem::directory_iterator{}) == 2,
               "removing a LAN connection must remove its cached cover") ? 0 : 1;
}
