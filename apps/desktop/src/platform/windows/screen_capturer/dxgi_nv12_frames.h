/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-25
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DXGI_NV12_FRAMES_H_
#define _DXGI_NV12_FRAMES_H_

#include <d3d11.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>

#include "minirtc.h"
#include "d3d11_capture_feedback.h"

namespace crossdesk {

struct DxgiNv12Scaler;

class IntelCaptureDemand final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, minirtc::D3D11CaptureFeedback> {
 public:
  void STDMETHODCALLTYPE EncoderStarted() override { ++encoders_; retry_after_ = 0; }
  void STDMETHODCALLTYPE EncoderStopped() override { --encoders_; }
  void STDMETHODCALLTYPE RequestCpuFrames() override { retry_after_ = GetTickCount64() + 2000; }
  bool PrefersCpu() const { return !encoders_.load() && retry_after_.load() != 0; }
  bool TryNative();

 private:
  std::atomic<unsigned> encoders_{0};
  std::atomic<ULONGLONG> retry_after_{0};
};

class DxgiNv12Frame {
 public:
  ULONG AddRef() { return ++refs_; }
  ULONG Release();
  const MiniRtcNativeVideoFrame* Descriptor() const { return &frame_; }

 private:
  friend class DxgiNv12Frames;
  bool Init(ID3D11Device* device, ID3D11VideoDevice* video,
            ID3D11VideoProcessorEnumerator* enumerator, UINT width,
            UINT height);
  static const MiniRtcNativeVideoFrame* Scale(void* owner, uint32_t width,
                                              uint32_t height);
  static int Readback(void* owner, uint8_t* destination, size_t size);

  std::atomic<ULONG> refs_{1};
  MiniRtcNativeVideoFrame frame_{};
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_, staging_;
  Microsoft::WRL::ComPtr<IDXGIKeyedMutex> key_;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> output_;
  std::mutex readback_mutex_;
  Microsoft::WRL::ComPtr<minirtc::D3D11CaptureFeedback> feedback_;
  std::shared_ptr<DxgiNv12Scaler> scaler_;
};

class DxgiNv12Frames {
 public:
  explicit DxgiNv12Frames(ID3D11Device* device) : device_(device) {}
  Microsoft::WRL::ComPtr<DxgiNv12Frame> Convert(ID3D11Texture2D* desktop);
  ID3D11Texture2D* CleanDesktop() const { return source_.Get(); }

 private:
  friend class DxgiNv12Frame;
  friend struct DxgiNv12Scaler;
  Microsoft::WRL::ComPtr<DxgiNv12Frame> Convert(ID3D11Texture2D* texture,
                                                UINT width, UINT height);
  bool Init(const D3D11_TEXTURE2D_DESC& desc, UINT width, UINT height);
  std::shared_ptr<DxgiNv12Scaler> scaler_;
  UINT source_width_ = 0, source_height_ = 0;
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_;
  Microsoft::WRL::ComPtr<ID3D11VideoContext> video_context_;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> source_;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> input_;
  std::array<Microsoft::WRL::ComPtr<DxgiNv12Frame>, 3> slots_;
  bool disabled_ = false;
};

}  // namespace crossdesk
#endif
