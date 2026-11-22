#include "hdr_video_surface.h"

#include <GL/gl.h>
#include <algorithm>
#include "../screen_capturer/hdr_display.h"

namespace crossdesk {

bool HdrVideoSurface::Create(HWND window) {
  Ptr<IDXGIAdapter1> adapter;
  if (!IsHdrMonitor(monitor_, adapter.GetAddressOf())) return false;
  if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                               &device_, nullptr, &context_))) {
    return false;
  }
  Ptr<IDXGIDevice> dxgi;
  if (FAILED(device_.As(&dxgi)) ||
      FAILED(adapter->GetParent(IID_PPV_ARGS(&factory_))) ||
      FAILED(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&composition_))) ||
      (FAILED(composition_->CreateTargetForHwnd(window, TRUE, &target_)) &&
       FAILED(composition_->CreateTargetForHwnd(window, FALSE, &target_))) ||
      FAILED(composition_->CreateVisual(&root_)) || FAILED(composition_->CreateVisual(&video_)) ||
      FAILED(composition_->CreateVisual(&controls_)) ||
      FAILED(root_->AddVisual(video_.Get(), FALSE, nullptr)) ||
      FAILED(root_->AddVisual(controls_.Get(), TRUE, video_.Get())) ||
      FAILED(target_->SetRoot(root_.Get()))) {
    return false;
  }
  window_ = window;
  return true;
}

bool HdrVideoSurface::UpdateChain(Ptr<IDXGISwapChain3>& chain, IDCompositionVisual* visual, UINT w,
                                  UINT h, bool hdr) {
  DXGI_SWAP_CHAIN_DESC1 desc{};
  if (chain && SUCCEEDED(chain->GetDesc1(&desc)) && desc.Width == w && desc.Height == h) {
    return true;
  }
  desc.Width = w;
  desc.Height = h;
  desc.Format = hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.Stereo = FALSE;
  desc.SampleDesc = {1, 0};
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.AlphaMode = hdr ? DXGI_ALPHA_MODE_IGNORE : DXGI_ALPHA_MODE_PREMULTIPLIED;
  Ptr<IDXGISwapChain1> candidate;
  if (FAILED(factory_->CreateSwapChainForComposition(device_.Get(), &desc, nullptr, &candidate)) ||
      FAILED(candidate.As(&chain))) {
    return false;
  }
  const auto space =
      hdr ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
  UINT support = 0;
  return SUCCEEDED(chain->CheckColorSpaceSupport(space, &support)) &&
         (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) &&
         SUCCEEDED(chain->SetColorSpace1(space)) && SUCCEEDED(visual->SetContent(chain.Get()));
}

bool HdrVideoSurface::Upload(IDXGISwapChain3* chain, const void* pixels, UINT pitch) {
  Ptr<ID3D11Texture2D> back;
  if (FAILED(chain->GetBuffer(0, IID_PPV_ARGS(&back)))) {
    return false;
  }
  context_->UpdateSubresource(back.Get(), 0, nullptr, pixels, pitch, 0);
  return SUCCEEDED(chain->Present(0, 0));
}

void HdrVideoSurface::Hide() {
  if (target_) {
    target_->SetRoot(nullptr);
    composition_->Commit();
  }
  target_.Reset();
  root_.Reset();
  controls_.Reset();
  video_.Reset();
  image_chain_.Reset();
  controls_chain_.Reset();
  composition_.Reset();
  factory_.Reset();
  context_.Reset();
  device_.Reset();
  sequence_ = 0;
  window_ = nullptr;
}

bool HdrVideoSurface::Prepare(HWND window, const MiniRtcNativeVideoFrame& frame, uint64_t sequence,
                              int target_width, int target_height, int top) {
  if (!window || target_width <= 0 || top >= target_height) {
    return false;
  }
  const auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
  const auto now = std::chrono::steady_clock::now();
  if (monitor != monitor_ && window_) Hide();
  if (monitor != monitor_ || now >= next_check_) {
    monitor_ = monitor;
    hdr_enabled_ = IsHdrMonitor(monitor);
    next_check_ = now + std::chrono::seconds(1);
  }
  if (!hdr_enabled_) {
    return false;
  }
  if (window != window_) {
    Hide();
    if (!Create(window)) {
      hdr_enabled_ = false;
      return false;
    }
  }
  const int w = frame.width, h = frame.height;
  float scale = std::min(float(target_width) / w, float(target_height - top) / h);
  int dw = std::max(1, int(w * scale + .5f)), dh = std::max(1, int(h * scale + .5f));
  int x = (target_width - dw) / 2, y = top + (target_height - top - dh) / 2;
  bounds_ = {x, y, x + dw, y + dh};
  target_height_ = target_height;
  if (!UpdateChain(image_chain_, video_.Get(), w, h, true) ||
      !UpdateChain(controls_chain_, controls_.Get(), dw, dh, false)) {
    hdr_enabled_ = false;
    return false;
  }
  D2D_MATRIX_3X2_F transform = {};
  transform._11 = float(dw) / w;
  transform._22 = float(dh) / h;
  if (FAILED(video_->SetTransform(transform)) || FAILED(root_->SetOffsetX(float(x))) ||
      FAILED(root_->SetOffsetY(float(y)))) {
    return false;
  }
  if (sequence != sequence_) {
    image_.resize(size_t(w) * h);
    auto quantize = [](float v) { return uint32_t(std::clamp(v, 0.f, 1.f) * 1023.f + .5f); };
    for (int row = 0; row < h; ++row) {
      for (int col = 0; col < w; ++col) {
        auto c = minirtc::HdrVideoFrame::ReadPq(frame.payload.hdr10.i010, w, h, col, row);
        image_[size_t(row) * w + col] =
            quantize(c.r) | (quantize(c.g) << 10) | (quantize(c.b) << 20) | 0xc0000000u;
      }
    }
    if (!Upload(image_chain_.Get(), image_.data(), w * 4)) {
      return false;
    }
    sequence_ = sequence;
  }
  return true;
}

void HdrVideoSurface::PresentControls() {
  if (!window_) {
    return;
  }
  const int w = bounds_.right - bounds_.left, h = bounds_.bottom - bounds_.top;
  overlay_.resize(size_t(w) * h);
  glReadPixels(bounds_.left, target_height_ - bounds_.bottom, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
               overlay_.data());
  // Readback is bottom-up RGBA. Use a visual transform to flip the rows.
  D2D_MATRIX_3X2_F flip = {};
  flip._11 = 1;
  flip._22 = -1;
  flip._32 = float(h);
  if (FAILED(controls_->SetTransform(flip)) ||
      !Upload(controls_chain_.Get(), overlay_.data(), w * 4) || FAILED(composition_->Commit())) {
    Hide();
    hdr_enabled_ = false;
  }
}

}  // namespace crossdesk
