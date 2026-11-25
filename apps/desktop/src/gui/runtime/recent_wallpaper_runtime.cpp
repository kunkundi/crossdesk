#include "runtime/gui_runtime.h"

#include "rd_log.h"
#include "wallpaper_preview.h"

namespace crossdesk {

void GuiRuntime::QueueRecentWallpaper(const std::string& id,
                                      PeerEventHandler* source) {
  // A service running as SYSTEM forwards host info to the user-session GUI.
  if (unattended_host_ || !IsAuthorizedController(id)) return;
  const auto events = ControllerEvents(id);
  if (events.get() != source) return;
  std::lock_guard lock(recent_wallpaper_requests_mutex_);
  if (recent_wallpaper_requests_.size() < 32)
    recent_wallpaper_requests_.insert_or_assign(id, events);
}

void GuiRuntime::HandleRecentWallpaperRequests() {
  if (recent_wallpaper_task_.valid()) {
    if (recent_wallpaper_task_.wait_for(std::chrono::seconds(0)) !=
        std::future_status::ready)
      return;
    decltype(recent_wallpaper_requests_) requests;
    {
      std::lock_guard lock(recent_wallpaper_requests_mutex_);
      requests.swap(recent_wallpaper_requests_);
    }
    try {
      const auto jpeg = recent_wallpaper_task_.get();
      if (jpeg.empty()) return;
      const auto message = std::string(kWallpaperPreviewPrefix) + jpeg;
      for (const auto& [id, source] : requests) {
        if (const auto events = source.lock();
            events && ControllerEvents(id) == events && IsAuthorizedController(id))
          SendControllerData(id, message.data(), message.size(),
                             control_data_label_.c_str());
      }
    } catch (const std::exception& error) {
      LOG_WARN("Could not prepare recent wallpaper: {}", error.what());
    }
    return;
  }
  {
    std::lock_guard lock(recent_wallpaper_requests_mutex_);
    if (recent_wallpaper_requests_.empty()) return;
  }
#ifdef __APPLE__
  auto path = LocalWallpaperPath();
  recent_wallpaper_task_ = std::async(std::launch::async, [path = std::move(path)] {
    return EncodeWallpaperPreview(path);
  });
#else
  recent_wallpaper_task_ = std::async(std::launch::async, [] {
    return EncodeWallpaperPreview(LocalWallpaperPath());
  });
#endif
}

}  // namespace crossdesk
