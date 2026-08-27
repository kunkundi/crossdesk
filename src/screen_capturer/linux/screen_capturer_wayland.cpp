#include "screen_capturer_wayland.h"

#include "screen_capturer_wayland_build.h"

#if !CROSSDESK_WAYLAND_BUILD_ENABLED
#error \
    "Wayland capturer requires USE_WAYLAND=true and Wayland development headers"
#endif

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "linux_cursor_shape.h"
#include "platform.h"
#include "rd_log.h"
#include "shared_cursor_state.h"
#include "wayland_portal_shared.h"

namespace crossdesk {

namespace {

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

struct PipeWireRecoveryConfig {
  ScreenCapturerWayland::PipeWireConnectMode mode;
  bool relaxed_connect = false;
};

constexpr auto kPipeWireCloseSettleDelay = std::chrono::milliseconds(200);

}  // namespace

ScreenCapturerWayland::ScreenCapturerWayland()
    : cursor_theme_matcher_(std::make_unique<LinuxCursorThemeMatcher>()) {}

ScreenCapturerWayland::~ScreenCapturerWayland() { Destroy(); }

int ScreenCapturerWayland::Init(const int fps, cb_desktop_data cb) {
  Destroy();

  if (!IsWaylandSession()) {
    LOG_ERROR("Wayland screen capturer requires a Wayland session");
    return -1;
  }

  if (!cb) {
    LOG_ERROR("Wayland screen capturer callback is null");
    return -1;
  }

  if (!CheckPortalAvailability()) {
    LOG_ERROR("xdg-desktop-portal screencast service is unavailable");
    return -1;
  }

  if (!EnsurePipeWireRuntimeAvailable()) {
    LOG_ERROR("Wayland screen capturer requires PipeWire 0.3 runtime");
    return -1;
  }

  fps_ = fps;
  callback_ = cb;
  pointer_granted_ = false;
  shared_session_registered_ = false;
  cursor_metadata_enabled_ = false;
  cursor_metadata_seen_ = false;
  cursor_metadata_missing_buffers_ = 0;
  ClearSharedCursorState();
  display_info_list_.clear();
  display_info_list_.push_back(
      DisplayInfo(display_name_, 0, 0, kFallbackWidth, kFallbackHeight));
  monitor_index_ = 0;
  initial_monitor_index_ = 0;
  frame_width_ = kFallbackWidth;
  frame_height_ = kFallbackHeight;
  frame_stride_ = kFallbackWidth * 4;
  portal_has_logical_size_ = false;
  portal_stream_width_ = 0;
  portal_stream_height_ = 0;
  logical_width_ = kFallbackWidth;
  logical_height_ = kFallbackHeight;
  y_plane_.resize(kFallbackWidth * kFallbackHeight);
  uv_plane_.resize((kFallbackWidth / 2) * (kFallbackHeight / 2) * 2);

  return 0;
}

int ScreenCapturerWayland::Destroy() {
  Stop();
  y_plane_.clear();
  uv_plane_.clear();
  display_info_list_.clear();
  callback_ = nullptr;
  return 0;
}

int ScreenCapturerWayland::Start(bool show_cursor) {
  if (running_) {
    return 0;
  }

  show_cursor_ = show_cursor;
  cursor_metadata_enabled_ = false;
  cursor_metadata_seen_ = false;
  cursor_metadata_missing_buffers_ = 0;
  ClearSharedCursorState();
  paused_ = false;
  pipewire_node_id_ = 0;
  UpdateDisplayGeometry(
      logical_width_ > 0 ? logical_width_ : kFallbackWidth,
      logical_height_ > 0 ? logical_height_ : kFallbackHeight);
  pipewire_format_ready_.store(false);
  pipewire_stream_start_ms_.store(0);
  pipewire_last_frame_ms_.store(0);
  running_ = true;
  thread_ = std::thread([this]() { Run(); });
  return 0;
}

int ScreenCapturerWayland::Stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
  pipewire_node_id_ = 0;
  cursor_metadata_enabled_ = false;
  cursor_metadata_seen_ = false;
  cursor_metadata_missing_buffers_ = 0;
  ClearSharedCursorState();
  UpdateDisplayGeometry(
      logical_width_ > 0 ? logical_width_ : kFallbackWidth,
      logical_height_ > 0 ? logical_height_ : kFallbackHeight);
  return 0;
}

int ScreenCapturerWayland::Pause([[maybe_unused]] int monitor_index) {
  paused_ = true;
  return 0;
}

int ScreenCapturerWayland::Resume([[maybe_unused]] int monitor_index) {
  paused_ = false;
  return 0;
}

int ScreenCapturerWayland::SwitchTo(int monitor_index) {
  if (monitor_index != 0) {
    LOG_WARN("Wayland screencast currently supports one logical display");
    return -1;
  }

  monitor_index_ = 0;
  return 0;
}

int ScreenCapturerWayland::ResetToInitialMonitor() {
  monitor_index_ = initial_monitor_index_;
  return 0;
}

std::vector<DisplayInfo> ScreenCapturerWayland::GetDisplayInfoList() {
  return display_info_list_;
}

void ScreenCapturerWayland::Run() {
  static constexpr PipeWireRecoveryConfig kRecoveryConfigs[] = {
      {PipeWireConnectMode::kTargetObject, false},
      {PipeWireConnectMode::kAny, true},
      {PipeWireConnectMode::kNodeId, false},
      {PipeWireConnectMode::kNodeId, true},
  };

  int recovery_index = 0;
  auto setup_pipewire = [this, &recovery_index]() -> bool {
    const auto& config = kRecoveryConfigs[recovery_index];
    return OpenPipeWireRemote() &&
           SetupPipeWireStream(config.relaxed_connect, config.mode);
  };
  auto setup_pipeline = [this, &setup_pipewire]() -> bool {
    return ConnectSessionBus() && CreatePortalSession() &&
           SelectPortalDevices() && SelectPortalSource() &&
           StartPortalSession() && setup_pipewire();
  };

  if (!setup_pipeline()) {
    running_ = false;
    CleanupPipeWire();
    ClosePortalSession();
    CleanupDbus();
    return;
  }
  while (running_) {
    if (!paused_) {
      const int64_t now = NowMs();
      const int64_t stream_start = pipewire_stream_start_ms_.load();
      const int64_t last_frame = pipewire_last_frame_ms_.load();
      const bool format_ready = pipewire_format_ready_.load();

      const bool format_timeout =
          stream_start > 0 && !format_ready && (now - stream_start) > 1200;
      const bool first_frame_timeout = stream_start > 0 && format_ready &&
                                       last_frame == 0 &&
                                       (now - stream_start) > 4000;
      const bool frame_stall = last_frame > 0 && (now - last_frame) > 5000;

      if (format_timeout || first_frame_timeout || frame_stall) {
        if (recovery_index + 1 >=
            static_cast<int>(sizeof(kRecoveryConfigs) /
                             sizeof(kRecoveryConfigs[0]))) {
          LOG_ERROR(
              "Wayland capture stalled and recovery limit reached, "
              "format_ready={}, stream_start={}, last_frame={}, attempts={}",
              format_ready, stream_start, last_frame, recovery_index);
          running_ = false;
          break;
        }

        ++recovery_index;
        const char* reason =
            format_timeout
                ? "format-timeout"
                : (first_frame_timeout ? "first-frame-timeout" : "frame-stall");
        const auto& config = kRecoveryConfigs[recovery_index];
        LOG_WARN(
            "Wayland capture stalled ({}) - retrying PipeWire only, "
            "attempt {}/{}, mode={}, relaxed_connect={}",
            reason, recovery_index,
            static_cast<int>(sizeof(kRecoveryConfigs) /
                             sizeof(kRecoveryConfigs[0])) -
                1,
            config.mode == PipeWireConnectMode::kTargetObject
                ? "target-object"
                : (config.mode == PipeWireConnectMode::kNodeId ? "node-id"
                                                               : "any"),
            config.relaxed_connect);

        CleanupPipeWire();
        if (!setup_pipewire()) {
          LOG_ERROR("Wayland PipeWire-only recovery failed at attempt {}",
                    recovery_index);
          running_ = false;
          break;
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  CleanupPipeWire();
  if (!session_handle_.empty()) {
    std::this_thread::sleep_for(kPipeWireCloseSettleDelay);
  }
  ClosePortalSession();
  CleanupDbus();
}

}  // namespace crossdesk
