/*
 * @Author: DI JUNKUN
 * @Date: 2026-03-22
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_WAYLAND_H_
#define _SCREEN_CAPTURER_WAYLAND_H_

struct DBusConnection;
struct pw_context;
struct pw_core;
struct pw_stream;
struct pw_thread_loop;

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "screen_capturer.h"

namespace crossdesk {

class ScreenCapturerWayland : public ScreenCapturer {
 public:
  enum class PipeWireConnectMode { kTargetObject, kNodeId, kAny };

 public:
  ScreenCapturerWayland();
  ~ScreenCapturerWayland();

 public:
  int Init(const int fps, cb_desktop_data cb) override;
  int Destroy() override;
  int Start(bool show_cursor) override;
  int Stop() override;

  int Pause(int monitor_index) override;
  int Resume(int monitor_index) override;

  int SwitchTo(int monitor_index) override;
  int ResetToInitialMonitor() override;

  std::vector<DisplayInfo> GetDisplayInfoList() override;

 private:
  bool CheckPortalAvailability() const;
  bool ConnectSessionBus();
  bool CreatePortalSession();
  bool SelectPortalDevices();
  bool SelectPortalSource();
  bool StartPortalSession();
  bool OpenPipeWireRemote();
  bool SetupPipeWireStream(bool relaxed_connect, PipeWireConnectMode mode);

  void Run();
  void CleanupPipeWire();
  void CleanupDbus();
  void ClosePortalSession();
  void HandlePipeWireBuffer();
  void UpdateDisplayGeometry(int width, int height);

 private:
  static constexpr int kFallbackWidth = 1920;
  static constexpr int kFallbackHeight = 1080;

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<int> monitor_index_{0};
  std::atomic<bool> pipewire_format_ready_{false};
  std::atomic<int64_t> pipewire_stream_start_ms_{0};
  std::atomic<int64_t> pipewire_last_frame_ms_{0};
  int initial_monitor_index_ = 0;
  std::atomic<bool> show_cursor_{true};
  int fps_ = 60;
  cb_desktop_data callback_ = nullptr;
  std::vector<DisplayInfo> display_info_list_;

  DBusConnection* dbus_connection_ = nullptr;
  std::string session_handle_;
  std::string display_name_ = "WAYLAND0";
  uint32_t pipewire_node_id_ = 0;
  int pipewire_fd_ = -1;

  pw_thread_loop* pw_thread_loop_ = nullptr;
  pw_context* pw_context_ = nullptr;
  pw_core* pw_core_ = nullptr;
  pw_stream* pw_stream_ = nullptr;
  void* stream_listener_ = nullptr;
  bool pipewire_initialized_ = false;
  bool pipewire_thread_loop_started_ = false;
  bool pointer_granted_ = false;
  bool shared_session_registered_ = false;
  uint32_t spa_video_format_ = 0;
  int frame_width_ = 0;
  int frame_height_ = 0;
  int frame_stride_ = 0;
  int logical_width_ = 0;
  int logical_height_ = 0;

  std::vector<uint8_t> y_plane_;
  std::vector<uint8_t> uv_plane_;
};

}  // namespace crossdesk

#endif
