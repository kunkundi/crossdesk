#include "dxgi_nv12_frames.h"

#include <d3d10.h>
#include <libyuv.h>

#include "rd_log.h"

namespace crossdesk {
using Microsoft::WRL::ComPtr;

bool IntelCaptureDemand::TryNative() {
  if (encoders_.load()) return true;
  auto retry = retry_after_.load();
  if (!retry) return true;
  const auto now = GetTickCount64();
  // Allow a new viewer/codec to discover native input while CPU consumers
  // continue using the original BGRA readback path between probes.
  return now >= retry && retry_after_.compare_exchange_strong(retry, now + 2000);
}

// Shared by the capture slots, but not by the derived textures. Bound the
// retained viewport sizes when independent viewers resize their windows.
struct DxgiNv12Scaler {
  struct Size {
    UINT width = 0, height = 0;
    std::unique_ptr<DxgiNv12Frames> outputs;
  };
  std::mutex mutex;
  std::array<Size, 2> sizes;
  size_t next = 0;
};

const MiniRtcNativeVideoFrame* DxgiNv12Frame::Scale(void* owner, uint32_t width,
                                                    uint32_t height) {
  auto& slot = *static_cast<DxgiNv12Frame*>(owner);
  if (!width || !height || ((width | height) & 1) ||
      width > slot.frame_.width || height > slot.frame_.height || !slot.scaler_)
    return nullptr;
  auto& scaler = *slot.scaler_;
  std::lock_guard lock(scaler.mutex);
  DxgiNv12Frames* outputs = nullptr;
  for (auto& size : scaler.sizes) {
    if (size.width == width && size.height == height)
      outputs = size.outputs.get();
  }
  if (!outputs) {
    ComPtr<ID3D11Device> device;
    slot.texture_->GetDevice(&device);
    auto& size = scaler.sizes[scaler.next];
    scaler.next = (scaler.next + 1) % scaler.sizes.size();
    size = {width, height, std::make_unique<DxgiNv12Frames>(device.Get())};
    outputs = size.outputs.get();
  }
  if (slot.key_->AcquireSync(0, 100) != S_OK) return nullptr;
  auto scaled = outputs->Convert(slot.texture_.Get(), width, height);
  if (FAILED(slot.key_->ReleaseSync(0)) || !scaled) return nullptr;
  return scaled.Detach()->Descriptor();
}

ULONG DxgiNv12Frame::Release() {
  const ULONG refs = --refs_;
  if (!refs) delete this;
  return refs;
}

bool DxgiNv12Frame::Init(ID3D11Device* device, ID3D11VideoDevice* video,
                         ID3D11VideoProcessorEnumerator* enumerator, UINT width,
                         UINT height) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width;
  desc.Height = height;
  desc.Format = DXGI_FORMAT_NV12;
  desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
  ComPtr<IDXGIResource> dxgi;
  HANDLE handle = nullptr;
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC view{};
  view.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture_)) ||
      FAILED(texture_.As(&key_)) || FAILED(texture_.As(&dxgi)) ||
      FAILED(dxgi->GetSharedHandle(&handle)) || !handle ||
      FAILED(video->CreateVideoProcessorOutputView(texture_.Get(), enumerator,
                                                   &view, &output_)))
    return false;
  frame_.struct_size = sizeof(frame_);
  frame_.type = MiniRtcNativeVideoFrameD3D11Nv12;
  frame_.width = width;
  frame_.height = height;
  frame_.payload.d3d11_nv12 = {texture_.Get(), handle};
  frame_.owner = this;
  frame_.retain = [](void* p) { static_cast<DxgiNv12Frame*>(p)->AddRef(); };
  frame_.release = [](void* p) { static_cast<DxgiNv12Frame*>(p)->Release(); };
  frame_.copy_to_nv12 = &Readback;
  feedback_ = minirtc::D3D11CaptureFeedback::Find(device);
  return true;
}

int DxgiNv12Frame::Readback(void* owner, uint8_t* destination, size_t size) {
  auto& slot = *static_cast<DxgiNv12Frame*>(owner);
  const UINT w = slot.frame_.width, h = slot.frame_.height;
  const size_t pixels = static_cast<size_t>(w) * h;
  if (!destination || size < pixels + pixels / 2) return -1;
  if (slot.feedback_) slot.feedback_->RequestCpuFrames();
  std::lock_guard lock(slot.readback_mutex_);
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  slot.texture_->GetDevice(&device);
  device->GetImmediateContext(&context);
  if (!slot.staging_) {
    D3D11_TEXTURE2D_DESC desc{};
    slot.texture_->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &slot.staging_)))
      return -1;
  }
  if (slot.key_->AcquireSync(0, 100) != S_OK) return -1;
  context->CopyResource(slot.staging_.Get(), slot.texture_.Get());
  if (FAILED(slot.key_->ReleaseSync(0))) return -1;
  D3D11_MAPPED_SUBRESOURCE map{};
  if (FAILED(context->Map(slot.staging_.Get(), 0, D3D11_MAP_READ, 0, &map)))
    return -1;
  const auto* y = static_cast<const uint8_t*>(map.pData);
  const int result = libyuv::NV12Copy(
      y, map.RowPitch, y + size_t(map.RowPitch) * h, map.RowPitch, destination,
      w, destination + pixels, w, w, h);
  context->Unmap(slot.staging_.Get(), 0);
  return result;
}

bool DxgiNv12Frames::Init(const D3D11_TEXTURE2D_DESC& desc, UINT width,
                          UINT height) {
  device_->GetImmediateContext(&context_);
  ComPtr<ID3D10Multithread> multithread;
  if (FAILED(context_.As(&multithread)) || FAILED(device_.As(&video_)) ||
      FAILED(context_.As(&video_context_)))
    return false;
  // Software/scaled consumers can materialize a retained frame on another
  // thread.
  multithread->SetMultithreadProtected(TRUE);
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
  content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  content.InputWidth = desc.Width;
  content.InputHeight = desc.Height;
  content.OutputWidth = width;
  content.OutputHeight = height;
  content.InputFrameRate = content.OutputFrameRate = {60, 1};
  content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  UINT input = 0, output = 0;
  if (FAILED(video_->CreateVideoProcessorEnumerator(&content, &enumerator_)) ||
      FAILED(enumerator_->CheckVideoProcessorFormat(desc.Format, &input)) ||
      !(input & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) ||
      FAILED(
          enumerator_->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &output)) ||
      !(output & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) ||
      FAILED(video_->CreateVideoProcessor(enumerator_.Get(), 0, &processor_)))
    return false;

  source_width_ = desc.Width;
  source_height_ = desc.Height;
  if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
    auto source_desc = desc;
    source_desc.Usage = D3D11_USAGE_DEFAULT;
    source_desc.CPUAccessFlags = source_desc.MiscFlags = 0;
    source_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC view{};
    view.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    if (FAILED(device_->CreateTexture2D(&source_desc, nullptr, &source_)) ||
        FAILED(video_->CreateVideoProcessorInputView(
            source_.Get(), enumerator_.Get(), &view, &input_)))
      return false;
    scaler_ = std::make_shared<DxgiNv12Scaler>();
  }
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE rgb{}, yuv{};
  rgb.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
  yuv.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
  yuv.YCbCr_Matrix = 0;  // BT.601, matching libyuv::ARGBToNV12.
  video_context_->VideoProcessorSetStreamColorSpace(
      processor_.Get(), 0, desc.Format == DXGI_FORMAT_NV12 ? &yuv : &rgb);
  video_context_->VideoProcessorSetOutputColorSpace(processor_.Get(), &yuv);
  video_context_->VideoProcessorSetStreamFrameFormat(
      processor_.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
  const RECT src{0, 0, static_cast<LONG>(desc.Width),
                 static_cast<LONG>(desc.Height)};
  const RECT dst{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
  video_context_->VideoProcessorSetStreamSourceRect(processor_.Get(), 0, TRUE,
                                                    &src);
  video_context_->VideoProcessorSetStreamDestRect(processor_.Get(), 0, TRUE,
                                                  &dst);
  video_context_->VideoProcessorSetOutputTargetRect(processor_.Get(), TRUE,
                                                    &dst);
  video_context_->VideoProcessorSetStreamAutoProcessingMode(processor_.Get(), 0,
                                                            FALSE);
  return true;
}

ComPtr<DxgiNv12Frame> DxgiNv12Frames::Convert(ID3D11Texture2D* desktop) {
  D3D11_TEXTURE2D_DESC desc{};
  desktop->GetDesc(&desc);
  if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
      ((desc.Width | desc.Height) & 1))
    return {};
  return Convert(desktop, desc.Width, desc.Height);
}

ComPtr<DxgiNv12Frame> DxgiNv12Frames::Convert(ID3D11Texture2D* texture,
                                              UINT width, UINT height) {
  if (disabled_) return {};
  D3D11_TEXTURE2D_DESC desc{};
  texture->GetDesc(&desc);
  if (processor_ &&
      (source_width_ != desc.Width || source_height_ != desc.Height))
    *this = DxgiNv12Frames(device_.Get());
  if (!processor_ && !Init(desc, width, height)) {
    disabled_ = true;
    LOG_WARN(
        "DXGI: BGRA to NV12 video processing unavailable; using CPU "
        "conversion");
    return {};
  }
  for (auto& slot : slots_) {
    if (!slot) {
      slot.Attach(new DxgiNv12Frame);
      if (!slot->Init(device_.Get(), video_.Get(), enumerator_.Get(), width,
                      height)) {
        disabled_ = true;
        LOG_WARN("DXGI: shared NV12 output unavailable; using CPU conversion");
        return {};
      }
      slot->scaler_ = scaler_;
      if (scaler_)
        slot->frame_.payload.d3d11_nv12.scale = &DxgiNv12Frame::Scale;
    }
    // Only the pool's reference remains when all asynchronous consumers are
    // done.
    if (slot->refs_.load() != 1 || slot->key_->AcquireSync(0, 0) != S_OK)
      continue;
    ComPtr<ID3D11VideoProcessorInputView> view = input_;
    if (source_) {
      context_->CopyResource(source_.Get(), texture);
    } else {
      D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{};
      input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
      if (FAILED(video_->CreateVideoProcessorInputView(
              texture, enumerator_.Get(), &input_desc, &view))) {
        slot->key_->ReleaseSync(0);
        disabled_ = true;
        return {};
      }
    }
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = view.Get();
    const HRESULT blt = video_context_->VideoProcessorBlt(
        processor_.Get(), slot->output_.Get(), 0, 1, &stream);
    const HRESULT released = slot->key_->ReleaseSync(0);
    if (FAILED(blt) || FAILED(released)) {
      disabled_ = true;
      LOG_WARN(
          "DXGI: NV12 video processing failed ({:#x}, {:#x}); using CPU "
          "conversion",
          static_cast<unsigned>(blt), static_cast<unsigned>(released));
      return {};
    }
    return slot;
  }
  return {};
}

}  // namespace crossdesk
