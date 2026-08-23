/*
 * @Author: DI JUNKUN
 * @Date: 2026-03-22
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_DRM_H_
#define _SCREEN_CAPTURER_DRM_H_

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "screen_capturer.h"

namespace crossdesk {

class ScreenCapturerDrm : public ScreenCapturer {
 public:
  ScreenCapturerDrm();
  ~ScreenCapturerDrm();

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
  struct DrmDevice {
    int fd = -1;
    std::string path;
  };

  struct DrmOutput {
    int device_index = -1;
    uint32_t connector_id = 0;
    uint32_t crtc_id = 0;
    std::string name;
    std::string stream_id;
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
  };

 private:
  bool DiscoverOutputs();
  void CloseDevices();
  void CaptureLoop();
  bool CaptureOutputFrame(const DrmOutput& output, bool emit_callback = true);
  bool MapFramebuffer(int fd, uint32_t handle, size_t map_size,
                      uint8_t** mapped_ptr, size_t* mapped_size,
                      int* prime_fd) const;
  void UnmapFramebuffer(uint8_t* mapped_ptr, size_t mapped_size,
                        int prime_fd) const;

 private:
  std::vector<DrmDevice> devices_;
  std::vector<DrmOutput> outputs_;
  std::vector<DisplayInfo> display_info_list_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<int> monitor_index_{0};
  int initial_monitor_index_ = 0;
  std::atomic<bool> show_cursor_{true};
  int fps_ = 60;
  cb_desktop_data callback_;
  int consecutive_failures_ = 0;

  std::vector<uint8_t> y_plane_;
  std::vector<uint8_t> uv_plane_;
};

}  // namespace crossdesk

#endif
