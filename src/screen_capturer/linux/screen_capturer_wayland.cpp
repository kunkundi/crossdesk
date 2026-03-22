#include "screen_capturer_wayland.h"

#include "screen_capturer_wayland_build.h"

#if !CROSSDESK_WAYLAND_BUILD_ENABLED
#error "Wayland capturer requires USE_WAYLAND=true and Wayland development headers"
#endif

#include <cstdlib>
#include <cstring>

#include <chrono>
#include <thread>

#include "rd_log.h"

namespace crossdesk {

namespace {

bool IsWaylandSession() {
  const char* session_type = getenv("XDG_SESSION_TYPE");
  if (session_type && strcmp(session_type, "wayland") == 0) {
    return true;
  }

  const char* wayland_display = getenv("WAYLAND_DISPLAY");
  return wayland_display && wayland_display[0] != '\0';
}

}  // namespace

ScreenCapturerWayland::ScreenCapturerWayland() {}

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

  fps_ = fps;
  callback_ = cb;
  display_info_list_.clear();
  display_info_list_.push_back(
      DisplayInfo(display_name_, 0, 0, kFallbackWidth, kFallbackHeight));
  monitor_index_ = 0;
  initial_monitor_index_ = 0;
  frame_width_ = kFallbackWidth;
  frame_height_ = kFallbackHeight;
  frame_stride_ = kFallbackWidth * 4;
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
  paused_ = false;
  running_ = true;
  thread_ = std::thread([this]() { Run(); });
  return 0;
}

int ScreenCapturerWayland::Stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
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
  if (!ConnectSessionBus() || !CreatePortalSession() || !SelectPortalSource() ||
      !StartPortalSession() || !OpenPipeWireRemote() ||
      !SetupPipeWireStream()) {
    running_ = false;
    CleanupPipeWire();
    ClosePortalSession();
    CleanupDbus();
    return;
  }

  while (running_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  CleanupPipeWire();
  ClosePortalSession();
  CleanupDbus();
}

}  // namespace crossdesk