/*
 * @Author: DI JUNKUN
 * @Date: 2026-02-27
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_WIN_H_
#define _SCREEN_CAPTURER_WIN_H_

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "screen_capturer.h"

namespace crossdesk {

class ScreenCapturerWin : public ScreenCapturer {
 public:
  ScreenCapturerWin();
  ~ScreenCapturerWin();

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
  std::unique_ptr<ScreenCapturer> impl_;
  bool impl_is_wgc_plugin_ = false;
  int fps_ = 60;
  cb_desktop_data cb_;
  cb_desktop_data cb_orig_;

  std::unordered_map<void*, std::string> handle_to_canonical_;
  std::unordered_map<std::string, std::string> label_alias_;
  std::mutex alias_mutex_;
  std::vector<DisplayInfo> canonical_displays_;
  std::unordered_set<std::string> canonical_labels_;
  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> show_cursor_{true};
  std::atomic<int> monitor_index_{0};
  int initial_monitor_index_ = 0;
  std::atomic<bool> secure_desktop_capture_active_{false};
  std::thread secure_capture_thread_;
  HANDLE secure_frame_mapping_ = nullptr;
  HANDLE secure_frame_ready_event_ = nullptr;
  uint8_t* secure_frame_view_ = nullptr;
  size_t secure_frame_view_size_ = 0;
  DWORD secure_shared_session_id_ = 0xFFFFFFFF;
  int secure_shared_left_ = 0;
  int secure_shared_top_ = 0;
  int secure_shared_width_ = 0;
  int secure_shared_height_ = 0;
  int secure_shared_fps_ = 0;
  bool secure_shared_show_cursor_ = true;
  std::string secure_shared_stage_;
  bool secure_shared_capture_started_ = false;

  void BuildCanonicalFromImpl();
  void RebuildAliasesFromImpl();
  void StopSecureCaptureThread();
  void SecureDesktopCaptureLoop();
  bool GetCurrentCaptureRegion(int* left, int* top, int* width, int* height,
                               std::string* display_name);
  bool StartSecureDesktopSharedCapture(DWORD session_id, int left, int top,
                                       int width, int height,
                                       const std::string& stage,
                                       bool show_cursor, int fps,
                                       std::string* error_out);
  void StopSecureDesktopSharedCapture(DWORD session_id);
  bool OpenSecureDesktopSharedFrame(DWORD session_id, size_t min_size,
                                    std::string* error_out);
  bool ReadSecureDesktopSharedFrame(DWORD wait_ms,
                                    std::vector<uint8_t>* nv12_frame_out,
                                    int* width_out, int* height_out,
                                    std::string* error_out);
  void CloseSecureDesktopSharedFrame();
};
}  // namespace crossdesk
#endif
