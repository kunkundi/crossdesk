/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-23
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _HDR_VIDEO_SURFACE_H_
#define _HDR_VIDEO_SURFACE_H_

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <chrono>
#include <vector>

#include "hdr_video_frame.h"

namespace crossdesk {

// Present PQ video and Slint's premultiplied SDR controls as separate surfaces.
// DWM composites the controls in the monitor's output color space.
class HdrVideoSurface {
 public:
  bool Prepare(HWND window, const MiniRtcNativeVideoFrame& frame, uint64_t sequence,
               int target_width, int target_height, int top);
  void PresentControls();
  void Hide();
  bool IsVisible() const { return window_ != nullptr; }
  const RECT& Bounds() const { return bounds_; }

 private:
  template <class T>
  using Ptr = Microsoft::WRL::ComPtr<T>;
  bool Create(HWND window);
  bool UpdateChain(Ptr<IDXGISwapChain3>& chain, IDCompositionVisual* visual, UINT w, UINT h,
                   bool hdr);
  bool Upload(IDXGISwapChain3* chain, const void* pixels, UINT pitch);

  HWND window_ = nullptr;
  HMONITOR monitor_ = nullptr;
  bool hdr_enabled_ = false;
  std::chrono::steady_clock::time_point next_check_{};
  Ptr<ID3D11Device> device_;
  Ptr<ID3D11DeviceContext> context_;
  Ptr<IDXGIFactory2> factory_;
  Ptr<IDCompositionDevice> composition_;
  Ptr<IDCompositionTarget> target_;
  Ptr<IDCompositionVisual> root_, video_, controls_;
  Ptr<IDXGISwapChain3> image_chain_, controls_chain_;
  std::vector<uint32_t> image_, overlay_;
  uint64_t sequence_ = 0;
  RECT bounds_{};
  int target_height_ = 0;
};

}  // namespace crossdesk

#endif
