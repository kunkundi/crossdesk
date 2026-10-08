/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-08
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SECURE_DESKTOP_DXGI_CAPTURE_H_
#define _SECURE_DESKTOP_DXGI_CAPTURE_H_

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <libyuv.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace crossdesk {

// Owned by the SYSTEM helper's capture thread, after binding its input desktop.
// Destroy before switching desktops. The uncomposited pixels stay valid on a
// timeout, so a static lock screen and a moving separate cursor keep their
// requested cadence without another GPU readback or a GDI screenshot.
class SecureDesktopDxgiCapture {
 public:
  HRESULT Initialize(int left, int top, int width, int height) {
    if (width <= 0 || height <= 0) return E_INVALIDARG;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) return hr;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; SUCCEEDED(factory->EnumAdapters1(
                         a, adapter.ReleaseAndGetAddressOf())); ++a) {
      Microsoft::WRL::ComPtr<IDXGIOutput> output;
      for (UINT o = 0; SUCCEEDED(adapter->EnumOutputs(
                           o, output.ReleaseAndGetAddressOf())); ++o) {
        DXGI_OUTPUT_DESC desc{};
        if (FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop) continue;
        const RECT& bounds = desc.DesktopCoordinates;
        if (left < bounds.left || top < bounds.top ||
            static_cast<int64_t>(left) + width > bounds.right ||
            static_cast<int64_t>(top) + height > bounds.bottom) continue;

        hr = D3D11CreateDevice(
            adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            device_.GetAddressOf(), nullptr, context_.GetAddressOf());
        if (FAILED(hr)) return hr;
        Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
        hr = output.As(&output1);
        if (FAILED(hr)) return hr;
        hr = output1->DuplicateOutput(device_.Get(), duplication_.GetAddressOf());
        if (FAILED(hr)) return hr;

        output_desc_ = desc;
        offset_x_ = left - bounds.left;
        offset_y_ = top - bounds.top;
        DXGI_OUTDUPL_DESC duplication_desc{};
        duplication_->GetDesc(&duplication_desc);
        switch (duplication_desc.Rotation) {
          case DXGI_MODE_ROTATION_ROTATE90: rotation_ = libyuv::kRotate90; break;
          case DXGI_MODE_ROTATION_ROTATE180: rotation_ = libyuv::kRotate180; break;
          case DXGI_MODE_ROTATION_ROTATE270: rotation_ = libyuv::kRotate270; break;
          default: rotation_ = libyuv::kRotate0; break;
        }
        return S_OK;
      }
    }
    // Includes headless desktops and regions spanning more than one output.
    return DXGI_ERROR_NOT_FOUND;
  }

  // S_FALSE means no first frame yet; the caller can use GDI until one arrives.
  // On any failure the caller must discard this instance, including its cache.
  HRESULT Capture() {
    updated_ = false;
    if (!duplication_) return E_UNEXPECTED;
    Microsoft::WRL::ComPtr<IDXGIResource> resource;
    DXGI_OUTDUPL_FRAME_INFO info{};
    const HRESULT hr = duplication_->AcquireNextFrame(0, &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return pixels_.empty() ? S_FALSE : S_OK;
    if (FAILED(hr)) return hr;
    struct ReleaseFrame {
      IDXGIOutputDuplication* duplication;
      ~ReleaseFrame() { duplication->ReleaseFrame(); }
    } release{duplication_.Get()};
    if (info.LastMouseUpdateTime.QuadPart != 0) {
      cursor_embedded_ = !info.PointerPosition.Visible;
    }
    // Pointer-only updates leave the desktop image unchanged.
    if (info.LastPresentTime.QuadPart == 0 && !pixels_.empty()) return S_OK;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    HRESULT result = resource.As(&texture);
    if (FAILED(result)) return result;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    const bool swap_axes = rotation_ == libyuv::kRotate90 ||
                           rotation_ == libyuv::kRotate270;
    const UINT logical_width = swap_axes ? desc.Height : desc.Width;
    const UINT logical_height = swap_axes ? desc.Width : desc.Height;
    const RECT& bounds = output_desc_.DesktopCoordinates;
    if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
        logical_width != static_cast<UINT>(bounds.right - bounds.left) ||
        logical_height != static_cast<UINT>(bounds.bottom - bounds.top)) {
      return DXGI_ERROR_ACCESS_LOST;
    }
    if (!staging_) {
      desc.Usage = D3D11_USAGE_STAGING;
      desc.BindFlags = 0;
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      desc.MiscFlags = 0;
      result = device_->CreateTexture2D(&desc, nullptr, staging_.GetAddressOf());
      if (FAILED(result)) return result;
    }
    context_->CopyResource(staging_.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) return result;
    stride_ = static_cast<int>(logical_width) * 4;
    pixels_.resize(static_cast<size_t>(stride_) * logical_height);
    const int converted = libyuv::ARGBRotate(
        static_cast<const uint8_t*>(mapped.pData), static_cast<int>(mapped.RowPitch),
        pixels_.data(), stride_, static_cast<int>(desc.Width),
        static_cast<int>(desc.Height), rotation_);
    context_->Unmap(staging_.Get(), 0);
    updated_ = converted == 0;
    return converted == 0 ? S_OK : E_FAIL;
  }

  const uint8_t* pixels() const {
    return pixels_.data() + static_cast<size_t>(offset_y_) * stride_ + offset_x_ * 4;
  }
  int stride() const { return stride_; }
  bool updated() const { return updated_; }
  const DXGI_OUTPUT_DESC& output() const { return output_desc_; }
  void SetSoftwareCursor(bool software) { cursor_embedded_ = software; }
  bool CursorEmbedded(const CURSORINFO& cursor) const {
    return cursor_embedded_ && (cursor.flags & CURSOR_SHOWING) != 0 &&
           MonitorFromPoint(cursor.ptScreenPos, MONITOR_DEFAULTTONULL) ==
               output_desc_.Monitor;
  }

 private:
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
  DXGI_OUTPUT_DESC output_desc_{};
  libyuv::RotationMode rotation_ = libyuv::kRotate0;
  int offset_x_ = 0, offset_y_ = 0, stride_ = 0;
  bool cursor_embedded_ = false;
  bool updated_ = false;
  std::vector<uint8_t> pixels_;
};

}  // namespace crossdesk

#endif  // _SECURE_DESKTOP_DXGI_CAPTURE_H_
