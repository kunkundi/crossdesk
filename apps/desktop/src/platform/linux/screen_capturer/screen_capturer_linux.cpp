#include "screen_capturer_linux.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include <display_stream_id.h>
#include "platform.h"
#include "privacy_controller.h"
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
                     const char* reported_stream_id,
                     const MiniRtcNativeVideoFrame* native_frame) {
    const std::string mapped_stream_id = MapStreamId(reported_stream_id);
    if (mapped_stream_id.empty()) {
      if (!invalid_stream_id_logged_.exchange(true,
                                              std::memory_order_relaxed)) {
        LOG_WARN("Linux capturer dropping frame without a registered stream "
                 "id: reported='{}', size={}x{}, bytes={}",
                 reported_stream_id ? reported_stream_id : "", width, height,
                 size);
      }
      return;
    }
    invalid_stream_id_logged_.store(false, std::memory_order_relaxed);
    {
      std::lock_guard lock(privacy_mutex_);
      if (privacy_ && !capture_paused_ && !capture_announced_) {
        capture_announced_ = true;
        privacy_->CaptureChanged(true);
      }
    }
    if (callback_orig_) {
      callback_orig_(data, size, width, height, mapped_stream_id.c_str(),
                     native_frame);
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
  Stop();
  if (impl_) {
    impl_->Destroy();
    impl_.reset();
  }

  backend_ = BackendType::kNone;
  callback_ = nullptr;
  callback_orig_ = nullptr;
  current_monitor_index_.store(0, std::memory_order_relaxed);
  invalid_stream_id_logged_.store(false, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(alias_mutex_);
    canonical_displays_.clear();
    backend_indices_.clear();
    stream_id_alias_.clear();
  }
  return 0;
}

int ScreenCapturerLinux::Start(bool show_cursor) {
  if (!impl_) {
    LOG_ERROR("Linux screen capturer backend is not initialized");
    return -1;
  }

  {
    std::lock_guard lock(privacy_mutex_);
    capture_paused_ = false;
  }

  // X11 output names, DRM connectors and Wayland stream handles may all change
  // after an HDMI hotplug, so rebuild the current backend before each session.
  const int refresh_ret = RefreshCurrentBackend();
  if (refresh_ret != 0) {
    LOG_WARN("Linux screen capturer backend refresh failed: {}", refresh_ret);
  }

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
  {
    std::lock_guard lock(privacy_mutex_);
    capture_announced_ = false;
    if (privacy_) privacy_->CaptureChanged(false);
  }
  UpdateAliasesFromBackend(impl_.get());
  return ret;
}

int ScreenCapturerLinux::Pause(int monitor_index) {
  if (!impl_) {
    return -1;
  }
  const int ret = impl_->Pause(monitor_index);
  {
    // An in-flight frame must not announce running again after a pause.
    std::lock_guard lock(privacy_mutex_);
    capture_paused_ = true;
    capture_announced_ = false;
    if (privacy_) privacy_->CaptureChanged(false);
  }
  return ret;
}

int ScreenCapturerLinux::Resume(int monitor_index) {
  if (!impl_) {
    return -1;
  }
  {
    std::lock_guard lock(privacy_mutex_);
    capture_paused_ = false;
  }
  return impl_->Resume(monitor_index);
}

void ScreenCapturerLinux::SetX11VideoWanted(bool wanted) {
  if (backend_ != BackendType::kX11) return;
  // Automatic privacy on connection needs a first real frame before the
  // guard can engage. An idle subscription must not report a capture fault.
  if (!wanted) {
    std::lock_guard lock(privacy_mutex_);
    wanted = !capture_announced_ && privacy_ && privacy_->Requested();
  }
  static_cast<ScreenCapturerX11*>(impl_.get())->SetVideoWanted(wanted);
}

int ScreenCapturerLinux::SwitchTo(int monitor_index) {
  if (!impl_) {
    return -1;
  }
  const int index = BackendIndex(monitor_index);
  if (index < 0) return -1;
  const int ret = impl_->SwitchTo(index);
  if (ret == 0) {
    current_monitor_index_.store(monitor_index, std::memory_order_relaxed);
  }
  return ret;
}

int ScreenCapturerLinux::ResetToInitialMonitor() {
  return SwitchTo(0);
}

int ScreenCapturerLinux::BackendIndex(int slot) const {
  std::lock_guard lock(alias_mutex_);
  if (slot < 0 || static_cast<size_t>(slot) >= backend_indices_.size()) return -1;
  return backend_indices_[slot];
}

void ScreenCapturerLinux::SelectBackendMonitor(ScreenCapturer* backend, int requested) {
  int slot = requested;
  int index = BackendIndex(slot);
  if (index < 0) {
    std::lock_guard lock(alias_mutex_);
    const auto first = std::find_if(backend_indices_.begin(), backend_indices_.end(),
                                    [](int i) { return i >= 0; });
    if (first == backend_indices_.end()) {
      current_monitor_index_.store(-1);
      return;
    }
    slot = static_cast<int>(first - backend_indices_.begin());
    index = *first;
  }
  current_monitor_index_.store(backend->SwitchTo(index) == 0 ? slot : -1);
}

bool ScreenCapturerLinux::RefreshIdleDisplays(const std::function<bool(size_t)>& accept) {
  const auto now = std::chrono::steady_clock::now();
  if (!impl_ || UsesWaylandPortal() || now < next_idle_display_probe_) return false;
  next_idle_display_probe_ = now + std::chrono::seconds(1);
  auto publish = [&](const std::vector<DisplayInfo>& displays) {
    std::lock_guard lock(alias_mutex_);
    if (displays.empty() || displays == canonical_displays_ || !accept(displays.size()))
      return false;
    canonical_displays_ = displays;
    return true;
  };
  bool changed = false;
  if (backend_ == BackendType::kX11)
    changed = static_cast<ScreenCapturerX11*>(impl_.get())->RefreshDisplays(publish);
#if defined(CROSSDESK_HAS_DRM) && CROSSDESK_HAS_DRM
  if (backend_ == BackendType::kDrm)
    changed = static_cast<ScreenCapturerDrm*>(impl_.get())->RefreshIdleOutputs(publish);
#endif
  if (changed) {
    current_monitor_index_.store(0);
    UpdateAliasesFromBackend(impl_.get());
  }
  return changed;
}

std::vector<DisplayInfo> ScreenCapturerLinux::GetDisplayInfoList() {
  if (!impl_) {
    return std::vector<DisplayInfo>();
  }

  // Wayland backend may update display geometry/stream handle asynchronously
  // after Start(). Refresh aliases every time to keep canonical displays fresh.
  UpdateAliasesFromBackend(impl_.get());

  std::lock_guard<std::mutex> lock(alias_mutex_);
  return canonical_displays_;
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

int ScreenCapturerLinux::RefreshCurrentBackend() {
  std::unique_ptr<ScreenCapturer> backend;
  const char* backend_name = "unknown";
  switch (backend_) {
    case BackendType::kX11:
      backend = std::make_unique<ScreenCapturerX11>();
      backend_name = "X11";
      break;
    case BackendType::kDrm:
#if defined(CROSSDESK_HAS_DRM) && CROSSDESK_HAS_DRM
      backend = std::make_unique<ScreenCapturerDrm>();
      backend_name = "DRM";
      break;
#else
      return -1;
#endif
    case BackendType::kWayland:
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
      backend = std::make_unique<ScreenCapturerWayland>();
      backend_name = "Wayland";
      break;
#else
      return -1;
#endif
    case BackendType::kNone:
      return -1;
  }

  const int ret = backend->Init(fps_, callback_);
  if (ret != 0) {
    backend->Destroy();
    return ret;
  }

  const int requested_monitor =
      current_monitor_index_.load(std::memory_order_relaxed);
  UpdateAliasesFromBackend(backend.get());
  SelectBackendMonitor(backend.get(), requested_monitor);

  if (impl_) {
    impl_->Destroy();
  }

  impl_ = std::move(backend);
  LOG_INFO("Linux screen capturer {} backend refreshed before start",
           backend_name);
  return 0;
}

bool ScreenCapturerLinux::TryFallbackToDrm(bool show_cursor) {
#if defined(CROSSDESK_HAS_DRM) && CROSSDESK_HAS_DRM
  return StartFallback(std::make_unique<ScreenCapturerDrm>(), BackendType::kDrm, "DRM", show_cursor);
#else
  return false;
#endif
}

bool ScreenCapturerLinux::TryFallbackToX11(bool show_cursor) {
  return StartFallback(std::make_unique<ScreenCapturerX11>(), BackendType::kX11, "X11", show_cursor);
}

bool ScreenCapturerLinux::TryFallbackToWayland(bool show_cursor) {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  return StartFallback(std::make_unique<ScreenCapturerWayland>(), BackendType::kWayland,
                       "Wayland", show_cursor);
#else
  return false;
#endif
}

bool ScreenCapturerLinux::StartFallback(std::unique_ptr<ScreenCapturer> candidate,
    BackendType type, const char* name, bool show_cursor) {
  if (const int ret = candidate->Init(fps_, callback_); ret != 0) {
    LOG_ERROR("Linux screen capturer fallback {} init failed: {}", name, ret);
    return false;
  }

  const int requested = current_monitor_index_.load();
  std::vector<DisplayInfo> previous;
  {
    std::lock_guard lock(alias_mutex_);
    previous = canonical_displays_;
  }
  UpdateAliasesFromBackend(candidate.get(), true);
  SelectBackendMonitor(candidate.get(), requested);
  if (const int ret = candidate->Start(show_cursor); ret != 0) {
    candidate->Destroy();
    {
      std::lock_guard lock(alias_mutex_);
      canonical_displays_ = std::move(previous);
    }
    UpdateAliasesFromBackend(impl_.get());
    current_monitor_index_.store(requested);
    LOG_ERROR("Linux screen capturer fallback {} start failed: {}", name, ret);
    return false;
  }

  impl_->Destroy();
  impl_ = std::move(candidate);
  backend_ = type;
  LOG_INFO("Linux screen capturer fallback switched to {}", name);
  return true;
}

void ScreenCapturerLinux::UpdateAliasesFromBackend(ScreenCapturer* backend, bool fallback) {
  const auto displays = backend->GetDisplayInfoList();
  std::lock_guard lock(alias_mutex_);
  if (canonical_displays_.empty()) canonical_displays_ = displays;
  stream_id_alias_.clear();
  backend_indices_.assign(canonical_displays_.size(), -1);

  // Reserve all native identities before assigning slots across backends.
  // A newly inserted output must never claim a surviving display's stream.
  std::vector<bool> used(displays.size(), false);
  auto bind = [&](size_t slot, size_t index) {
    used[index] = true;
    canonical_displays_[slot] = displays[index];
    if (displays[index].width <= 0 || displays[index].height <= 0) return;
    backend_indices_[slot] = static_cast<int>(index);
    stream_id_alias_[MakeDisplayStreamId(index)] = MakeDisplayStreamId(slot);
  };
  for (size_t slot = 0; slot < canonical_displays_.size(); ++slot) {
    const auto& old = canonical_displays_[slot];
    size_t match = displays.size();
    for (size_t i = 0; i < displays.size(); ++i) {
      if (used[i] || old.name != displays[i].name ||
          (old.handle != displays[i].handle && !UsesWaylandPortal())) continue;
      if (match == displays.size()) match = i;
      if (old == displays[i]) {
        match = i;
        break;
      }
    }
    if (match < displays.size()) bind(slot, match);
  }
  for (size_t slot = 0; slot < canonical_displays_.size(); ++slot) {
    if (backend_indices_[slot] >= 0) continue;
    auto& old = canonical_displays_[slot];
    if (fallback) {
      // X11, DRM and the portal use different native names and handles.
      size_t match = displays.size();
      for (size_t i = 0; i < displays.size(); ++i) {
        if (used[i]) continue;
        if (match == displays.size() || i == slot) match = i;
        const auto& d = displays[i];
        if (d.left == old.left && d.top == old.top &&
            d.width == old.width && d.height == old.height) {
          match = i;
          break;
        }
      }
      if (match < displays.size()) {
        bind(slot, match);
        continue;
      }
    }
    old.left = old.top = old.right = old.bottom = old.width = old.height = 0;
    old.is_primary = false;
  }
}

std::string ScreenCapturerLinux::MapStreamId(const char* reported_stream_id) const {
  std::lock_guard lock(alias_mutex_);
  const auto it = stream_id_alias_.find(reported_stream_id ? reported_stream_id : "");
  return it == stream_id_alias_.end() ? std::string{} : it->second;
}

}  // namespace crossdesk
