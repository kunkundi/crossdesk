/*
 * @Author: DI JUNKUN
 * @Date: 2026-02-27
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_DXGI_H_
#define _SCREEN_CAPTURER_DXGI_H_

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rd_log.h"
#include "screen_capturer.h"
#include "stable_display_slots.h"

namespace crossdesk {

class CursorFrameCompositor;

class ScreenCapturerDxgi : public ScreenCapturer {
 public:
  ScreenCapturerDxgi();
  ~ScreenCapturerDxgi();

 public:
  int Init(const int fps, cb_desktop_data cb) override;
  int Destroy() override;
  int Start(bool show_cursor) override;
  int Stop() override;
  void SetCursorCapture(bool enabled) {
    show_cursor_.store(enabled, std::memory_order_relaxed);
  }

  int Pause(int monitor_index) override;
  int Resume(int monitor_index) override;

  int SwitchTo(int monitor_index) override;
  int ResetToInitialMonitor() override;

  // Rebuild the selected output on the capture thread, never on the caller.
  void RequestTopologyRefresh() { topology_refresh_requested_.store(true); }

  std::vector<DisplayInfo> GetDisplayInfoList() override {
    std::lock_guard<std::mutex> lock(switch_mutex_);
    return display_info_list_;
  }
  int GetCurrentMonitorIndex() const override {
    return monitor_index_.load(std::memory_order_relaxed);
  }

 private:
  bool EnumerateDisplays();
  bool CreateDuplicationForMonitor(int monitor_index);
  void CaptureLoop();
  bool ConvertFrame(int frame_monitor, bool* cursor_embedded,
                    CursorFrameCompositor& compositor);
  void ReleaseDuplication();

 private:
  std::vector<DisplayInfo> display_info_list_;
  std::vector<Microsoft::WRL::ComPtr<IDXGIOutput>> outputs_;
  StableDisplaySlots display_slots_;
  std::atomic<bool> topology_refresh_requested_{false};

  Microsoft::WRL::ComPtr<ID3D11Device> d3d_device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context_;
  Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
  DXGI_MODE_ROTATION rotation_ = DXGI_MODE_ROTATION_IDENTITY;
  uint64_t duplication_generation_ = 0;

  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<int> monitor_index_{0};
  std::atomic<bool> show_cursor_{true};
  std::thread thread_;
  int fps_ = 60;
  cb_desktop_data callback_ = nullptr;
  std::mutex switch_mutex_;

  unsigned char* nv12_frame_ = nullptr;
  int nv12_width_ = 0;
  int nv12_height_ = 0;
  std::vector<uint8_t> rotated_frame_;
};
}  // namespace crossdesk

#endif
