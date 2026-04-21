#include "screen_capturer_linux.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "platform.h"
#include "rd_log.h"
#if defined(CROSSDESK_HAS_DRM) && CROSSDESK_HAS_DRM
#include "screen_capturer_drm.h"
#endif
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
#include "screen_capturer_wayland.h"
#endif
#include "screen_capturer_x11.h"

namespace crossdesk {

namespace {

#if defined(CROSSDESK_HAS_DRM) && CROSSDESK_HAS_DRM
constexpr bool kDrmBuildEnabled = true;
#else
constexpr bool kDrmBuildEnabled = false;
#endif

#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
constexpr bool kWaylandBuildEnabled = true;
#else
constexpr bool kWaylandBuildEnabled = false;
#endif

}  // namespace

ScreenCapturerLinux::ScreenCapturerLinux() {}

ScreenCapturerLinux::~ScreenCapturerLinux() { Destroy(); }

int ScreenCapturerLinux::Init(const int fps, cb_desktop_data cb) {
  Destroy();

  if (!cb) {
    LOG_ERROR("Linux screen capturer callback is null");
    return -1;
  }

  fps_ = fps;
  callback_orig_ = std::move(cb);
  callback_ = [this](unsigned char* data, int size, int width, int height,
                     const char* display_name) {
    const std::string mapped_name = MapDisplayName(display_name);
    if (callback_orig_) {
      callback_orig_(data, size, width, height, mapped_name.c_str());
    }
  };

  const char* force_backend = getenv("CROSSDESK_SCREEN_BACKEND");
  if (force_backend && force_backend[0] != '\0') {
    if (strcmp(force_backend, "drm") == 0) {
#if defined(CROSSDESK_HAS_DRM) && CROSSDESK_HAS_DRM
      LOG_INFO("Linux screen capturer forced backend: DRM");
      return InitDrm();
#else
      LOG_ERROR(
          "Linux screen capturer forced backend DRM is disabled at build time");
      return -1;
#endif
    }

    if (strcmp(force_backend, "x11") == 0) {
      LOG_INFO("Linux screen capturer forced backend: X11");
      return InitX11();
    }
    if (strcmp(force_backend, "wayland") == 0) {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
      LOG_INFO("Linux screen capturer forced backend: Wayland");
      return InitWayland();
#else
      LOG_ERROR(
          "Linux screen capturer forced backend Wayland is disabled at build "
          "time");
      return -1;
#endif
    }

    LOG_WARN("Unknown CROSSDESK_SCREEN_BACKEND={}, using auto strategy",
             force_backend);
  }

  const bool wayland_session = IsWaylandSession();
  if (wayland_session) {
    if (kDrmBuildEnabled) {
      LOG_INFO("Wayland session detected, prefer DRM -> X11 -> Wayland");
      if (InitDrm() == 0) {
        return 0;
      }
    } else {
      LOG_INFO("Wayland session detected, DRM disabled, prefer X11 -> Wayland");
    }

    if (InitX11() == 0) {
      return 0;
    }

    if (kDrmBuildEnabled) {
      LOG_WARN(
          "DRM and X11 init failed in Wayland session, trying Wayland portal");
    } else {
      LOG_WARN("X11 init failed in Wayland session, trying Wayland portal");
    }
    if (kWaylandBuildEnabled) {
      return InitWayland();
    }
    LOG_ERROR("Wayland session detected but Wayland backend is disabled");
    return -1;
  }

  if (InitX11() == 0) {
    return 0;
  }

  if (kDrmBuildEnabled) {
    LOG_WARN("X11 init failed, trying DRM fallback");
    return InitDrm();
  }

  LOG_ERROR("X11 init failed and DRM backend is disabled");
  return -1;
}

int ScreenCapturerLinux::Destroy() {
  if (impl_) {
    impl_->Destroy();
    impl_.reset();
  }

  backend_ = BackendType::kNone;
  callback_ = nullptr;
  callback_orig_ = nullptr;
  {
    std::lock_guard<std::mutex> lock(alias_mutex_);
    canonical_displays_.clear();
    label_alias_.clear();
  }
  return 0;
}

int ScreenCapturerLinux::Start(bool show_cursor) {
  if (!impl_) {
    LOG_ERROR("Linux screen capturer backend is not initialized");
    return -1;
  }

#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  if (backend_ == BackendType::kWayland) {
    const int refresh_ret = RefreshWaylandBackend();
    if (refresh_ret != 0) {
      LOG_WARN("Linux screen capturer Wayland backend refresh failed: {}",
               refresh_ret);
    }
  }
#endif

  const int ret = impl_->Start(show_cursor);
  if (ret == 0) {
    return 0;
  }

  const char* backend_name = "None";
  if (backend_ == BackendType::kX11) {
    backend_name = "X11";
  } else if (backend_ == BackendType::kDrm) {
    backend_name = "DRM";
  } else if (backend_ == BackendType::kWayland) {
    backend_name = "Wayland";
  }

  LOG_WARN("Linux screen capturer backend {} start failed: {}", backend_name,
           ret);

  if (backend_ == BackendType::kX11 && kDrmBuildEnabled &&
      TryFallbackToDrm(show_cursor)) {
    return 0;
  }
  if (backend_ == BackendType::kX11 && kWaylandBuildEnabled &&
      TryFallbackToWayland(show_cursor)) {
    return 0;
  }
  if (backend_ == BackendType::kDrm && kDrmBuildEnabled) {
    if (TryFallbackToX11(show_cursor)) {
      return 0;
    }
    if (kWaylandBuildEnabled && TryFallbackToWayland(show_cursor)) {
      return 0;
    }
  }
  if (backend_ == BackendType::kWayland && kWaylandBuildEnabled) {
    if (kDrmBuildEnabled && TryFallbackToDrm(show_cursor)) {
      return 0;
    }
    if (TryFallbackToX11(show_cursor)) {
      return 0;
    }
  }

  return ret;
}

int ScreenCapturerLinux::Stop() {
  if (!impl_) {
    return 0;
  }
  const int ret = impl_->Stop();
  UpdateAliasesFromBackend(impl_.get());
  return ret;
}

int ScreenCapturerLinux::Pause(int monitor_index) {
  if (!impl_) {
    return -1;
  }
  return impl_->Pause(monitor_index);
}

int ScreenCapturerLinux::Resume(int monitor_index) {
  if (!impl_) {
    return -1;
  }
  return impl_->Resume(monitor_index);
}

int ScreenCapturerLinux::SwitchTo(int monitor_index) {
  if (!impl_) {
    return -1;
  }
  return impl_->SwitchTo(monitor_index);
}

int ScreenCapturerLinux::ResetToInitialMonitor() {
  if (!impl_) {
    return -1;
  }
  return impl_->ResetToInitialMonitor();
}

std::vector<DisplayInfo> ScreenCapturerLinux::GetDisplayInfoList() {
  if (!impl_) {
    return std::vector<DisplayInfo>();
  }

  // Wayland backend may update display geometry/stream handle asynchronously
  // after Start(). Refresh aliases every time to keep canonical displays fresh.
  UpdateAliasesFromBackend(impl_.get());

  std::lock_guard<std::mutex> lock(alias_mutex_);
  if (!canonical_displays_.empty()) {
    return canonical_displays_;
  }

  return impl_->GetDisplayInfoList();
}

int ScreenCapturerLinux::InitX11() {
  auto backend = std::make_unique<ScreenCapturerX11>();
  const int ret = backend->Init(fps_, callback_);
  if (ret != 0) {
    backend->Destroy();
    LOG_WARN("Linux screen capturer X11 init failed: {}", ret);
    return ret;
  }

  UpdateAliasesFromBackend(backend.get());
  impl_ = std::move(backend);
  backend_ = BackendType::kX11;
  LOG_INFO("Linux screen capturer backend selected: X11");
  return 0;
}

int ScreenCapturerLinux::InitDrm() {
#if defined(CROSSDESK_HAS_DRM) && CROSSDESK_HAS_DRM
  auto backend = std::make_unique<ScreenCapturerDrm>();
  const int ret = backend->Init(fps_, callback_);
  if (ret != 0) {
    backend->Destroy();
    LOG_WARN("Linux screen capturer DRM init failed: {}", ret);
    return ret;
  }

  UpdateAliasesFromBackend(backend.get());
  impl_ = std::move(backend);
  backend_ = BackendType::kDrm;
  LOG_INFO("Linux screen capturer backend selected: DRM");
  return 0;
#else
  LOG_WARN("Linux screen capturer DRM backend is disabled at build time");
  return -1;
#endif
}

int ScreenCapturerLinux::InitWayland() {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  auto backend = std::make_unique<ScreenCapturerWayland>();
  const int ret = backend->Init(fps_, callback_);
  if (ret != 0) {
    backend->Destroy();
    LOG_WARN("Linux screen capturer Wayland init failed: {}", ret);
    return ret;
  }

  UpdateAliasesFromBackend(backend.get());
  impl_ = std::move(backend);
  backend_ = BackendType::kWayland;
  LOG_INFO("Linux screen capturer backend selected: Wayland");
  return 0;
#else
  LOG_WARN("Linux screen capturer Wayland backend is disabled at build time");
  return -1;
#endif
}

int ScreenCapturerLinux::RefreshWaylandBackend() {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  auto backend = std::make_unique<ScreenCapturerWayland>();
  const int ret = backend->Init(fps_, callback_);
  if (ret != 0) {
    backend->Destroy();
    return ret;
  }

  if (impl_) {
    impl_->Destroy();
  }

  UpdateAliasesFromBackend(backend.get());
  impl_ = std::move(backend);
  backend_ = BackendType::kWayland;
  LOG_INFO("Linux screen capturer Wayland backend refreshed before start");
  return 0;
#else
  return -1;
#endif
}

bool ScreenCapturerLinux::TryFallbackToDrm(bool show_cursor) {
#if defined(CROSSDESK_HAS_DRM) && CROSSDESK_HAS_DRM
  auto drm_backend = std::make_unique<ScreenCapturerDrm>();
  int ret = drm_backend->Init(fps_, callback_);
  if (ret != 0) {
    LOG_ERROR("Linux screen capturer fallback DRM init failed: {}", ret);
    return false;
  }

  UpdateAliasesFromBackend(drm_backend.get());
  ret = drm_backend->Start(show_cursor);
  if (ret != 0) {
    drm_backend->Destroy();
    LOG_ERROR("Linux screen capturer fallback DRM start failed: {}", ret);
    return false;
  }

  if (impl_) {
    impl_->Stop();
    impl_->Destroy();
  }

  impl_ = std::move(drm_backend);
  backend_ = BackendType::kDrm;
  LOG_INFO("Linux screen capturer fallback switched to DRM");
  return true;
#else
  (void)show_cursor;
  LOG_WARN("Linux screen capturer DRM fallback is disabled at build time");
  return false;
#endif
}

bool ScreenCapturerLinux::TryFallbackToX11(bool show_cursor) {
  auto x11_backend = std::make_unique<ScreenCapturerX11>();
  int ret = x11_backend->Init(fps_, callback_);
  if (ret != 0) {
    LOG_ERROR("Linux screen capturer fallback X11 init failed: {}", ret);
    return false;
  }

  UpdateAliasesFromBackend(x11_backend.get());
  ret = x11_backend->Start(show_cursor);
  if (ret != 0) {
    x11_backend->Destroy();
    LOG_ERROR("Linux screen capturer fallback X11 start failed: {}", ret);
    return false;
  }

  if (impl_) {
    impl_->Stop();
    impl_->Destroy();
  }

  impl_ = std::move(x11_backend);
  backend_ = BackendType::kX11;
  LOG_INFO("Linux screen capturer fallback switched to X11");
  return true;
}

bool ScreenCapturerLinux::TryFallbackToWayland(bool show_cursor) {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  auto wayland_backend = std::make_unique<ScreenCapturerWayland>();
  int ret = wayland_backend->Init(fps_, callback_);
  if (ret != 0) {
    LOG_ERROR("Linux screen capturer fallback Wayland init failed: {}", ret);
    return false;
  }

  UpdateAliasesFromBackend(wayland_backend.get());
  ret = wayland_backend->Start(show_cursor);
  if (ret != 0) {
    wayland_backend->Destroy();
    LOG_ERROR("Linux screen capturer fallback Wayland start failed: {}", ret);
    return false;
  }

  if (impl_) {
    impl_->Stop();
    impl_->Destroy();
  }

  impl_ = std::move(wayland_backend);
  backend_ = BackendType::kWayland;
  LOG_INFO("Linux screen capturer fallback switched to Wayland");
  return true;
#else
  (void)show_cursor;
  LOG_WARN("Linux screen capturer Wayland fallback is disabled at build time");
  return false;
#endif
}

void ScreenCapturerLinux::UpdateAliasesFromBackend(ScreenCapturer* backend) {
  if (!backend) {
    return;
  }

  const auto backend_displays = backend->GetDisplayInfoList();
  if (backend_displays.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(alias_mutex_);
  label_alias_.clear();

  if (canonical_displays_.empty()) {
    canonical_displays_ = backend_displays;
    for (const auto& display : backend_displays) {
      label_alias_[display.name] = display.name;
    }
    return;
  }

  if (canonical_displays_.size() < backend_displays.size()) {
    for (size_t i = canonical_displays_.size(); i < backend_displays.size();
         ++i) {
      canonical_displays_.push_back(backend_displays[i]);
    }
  }

  for (size_t i = 0; i < backend_displays.size(); ++i) {
    const std::string mapped_name = i < canonical_displays_.size()
                                        ? canonical_displays_[i].name
                                        : backend_displays[i].name;
    label_alias_[backend_displays[i].name] = mapped_name;

    if (i < canonical_displays_.size()) {
      // Keep original stable names, but refresh geometry from active backend.
      canonical_displays_[i].handle = backend_displays[i].handle;
      canonical_displays_[i].is_primary = backend_displays[i].is_primary;
      canonical_displays_[i].left = backend_displays[i].left;
      canonical_displays_[i].top = backend_displays[i].top;
      canonical_displays_[i].right = backend_displays[i].right;
      canonical_displays_[i].bottom = backend_displays[i].bottom;
      canonical_displays_[i].width = backend_displays[i].width;
      canonical_displays_[i].height = backend_displays[i].height;
    }
  }
}

std::string ScreenCapturerLinux::MapDisplayName(
    const char* display_name) const {
  std::string input_name = display_name ? display_name : "";
  if (input_name.empty()) {
    return input_name;
  }

  std::lock_guard<std::mutex> lock(alias_mutex_);
  auto it = label_alias_.find(input_name);
  if (it != label_alias_.end()) {
    return it->second;
  }

  if (canonical_displays_.size() == 1) {
    return canonical_displays_[0].name;
  }

  return input_name;
}

}  // namespace crossdesk
