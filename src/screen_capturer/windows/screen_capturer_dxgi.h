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

namespace crossdesk {

class ScreenCapturerDxgi : public ScreenCapturer {
 public:
  ScreenCapturerDxgi();
  ~ScreenCapturerDxgi();

 public:
  int Init(const int fps, cb_desktop_data cb) override;
  int Destroy() override;
  int Start(bool show_cursor) override;
  int Stop() override;

  int Pause(int monitor_index) override;
  int Resume(int monitor_index) override;

  int SwitchTo(int monitor_index) override;
  int ResetToInitialMonitor() override;

  std::vector<DisplayInfo> GetDisplayInfoList() override {
    return display_info_list_;
  }

 private:
  bool InitializeDxgi();
  void EnumerateDisplays();
  bool CreateDuplicationForMonitor(int monitor_index);
  void CaptureLoop();
  void ReleaseDuplication();

 private:
  std::vector<DisplayInfo> display_info_list_;
  std::vector<Microsoft::WRL::ComPtr<IDXGIOutput>> outputs_;

  Microsoft::WRL::ComPtr<IDXGIFactory1> dxgi_factory_;
  Microsoft::WRL::ComPtr<ID3D11Device> d3d_device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context_;
  Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;

  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<int> monitor_index_{0};
  int initial_monitor_index_ = 0;
  std::atomic<bool> show_cursor_{true};
  std::thread thread_;
  int fps_ = 60;
  cb_desktop_data callback_ = nullptr;

  unsigned char* nv12_frame_ = nullptr;
  int nv12_width_ = 0;
  int nv12_height_ = 0;
};
}  // namespace crossdesk

#endif