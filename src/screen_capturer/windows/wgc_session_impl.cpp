#include "wgc_session_impl.h"

#include <Windows.Graphics.Capture.Interop.h>

#include <atomic>
#include <functional>
#include <iostream>
#include <memory>

#include "rd_log.h"

#define CHECK_INIT                            \
  if (!is_initialized_) {                     \
    std::cout << "AE_NEED_INIT" << std::endl; \
    return 4;                                 \
  }

#define CHECK_CLOSED                         \
  if (cleaned_.load() == true) {             \
    throw winrt::hresult_error(RO_E_CLOSED); \
  }

extern "C" {
HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(
    ::IDXGIDevice* dxgiDevice, ::IInspectable** graphicsDevice);
}

WgcSessionImpl::WgcSessionImpl(int id) : id_(id) {}

WgcSessionImpl::~WgcSessionImpl() {
  try {
    Stop();
    CleanUp();
  } catch (...) {
  }
}

void WgcSessionImpl::Release() { delete this; }

int WgcSessionImpl::Initialize(HWND hwnd) {
  std::scoped_lock locker(lock_);
  target_.hwnd = hwnd;
  target_.is_window = true;
  return Initialize();
}

int WgcSessionImpl::Initialize(HMONITOR hmonitor) {
  std::scoped_lock locker(lock_);
  target_.hmonitor = hmonitor;
  target_.is_window = false;
  return Initialize();
}

void WgcSessionImpl::RegisterObserver(wgc_session_observer* observer) {
  std::scoped_lock locker(lock_);
  observer_ = observer;
}

int WgcSessionImpl::Start() {
  std::scoped_lock locker(lock_);
  CHECK_INIT;

  if (is_running_) return 0;

  try {
    if (!capture_item_) {
      std::cout << "AE_NO_CAPTURE_ITEM" << std::endl;
      LOG_ERROR("No capture item");
      return 2;
    }

    auto current_size = capture_item_.Size();
    capture_framepool_ =
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::
            CreateFreeThreaded(d3d11_direct_device_,
                               winrt::Windows::Graphics::DirectX::
                                   DirectXPixelFormat::B8G8R8A8UIntNormalized,
                               2, current_size);

    capture_session_ = capture_framepool_.CreateCaptureSession(capture_item_);
    capture_frame_size_ = current_size;

    capture_framepool_trigger_ = capture_framepool_.FrameArrived(
        winrt::auto_revoke, {this, &WgcSessionImpl::OnFrame});
    capture_close_trigger_ = capture_item_.Closed(
        winrt::auto_revoke, {this, &WgcSessionImpl::OnClosed});

    capture_session_.IsCursorCaptureEnabled(false);
    capture_session_.StartCapture();

    is_running_ = true;
    is_paused_ = false;
    return 0;
  } catch (winrt::hresult_error const& e) {
    LOG_ERROR("Create WGC Capture Failed: {}", winrt::to_string(e.message()));
    return 86;
  } catch (...) {
    return 86;
  }
}

int WgcSessionImpl::Stop() {
  std::scoped_lock locker(lock_);
  CHECK_INIT;

  if (!is_running_) return 0;
  is_running_ = false;

  try {
    if (capture_framepool_trigger_) capture_framepool_trigger_.revoke();
    if (capture_close_trigger_) capture_close_trigger_.revoke();
    if (capture_session_) {
      capture_session_.Close();
      capture_session_ = nullptr;
    }
    if (capture_framepool_) {
      capture_framepool_.Close();
      capture_framepool_ = nullptr;
    }
  } catch (...) {
  }

  return 0;
}

int WgcSessionImpl::Pause() {
  std::scoped_lock locker(lock_);
  CHECK_INIT;
  is_paused_ = true;
  return 0;
}

int WgcSessionImpl::Resume() {
  std::scoped_lock locker(lock_);
  CHECK_INIT;
  is_paused_ = false;
  return 0;
}

auto WgcSessionImpl::CreateD3D11Device() {
  winrt::com_ptr<ID3D11Device> d3d_device;
  HRESULT hr;

  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                         nullptr, 0, D3D11_SDK_VERSION, d3d_device.put(),
                         nullptr, nullptr);

  if (DXGI_ERROR_UNSUPPORTED == hr) {
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                           nullptr, 0, D3D11_SDK_VERSION, d3d_device.put(),
                           nullptr, nullptr);
  }

  winrt::check_hresult(hr);

  winrt::com_ptr<::IInspectable> d3d11_device;
  winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(
      d3d_device.as<IDXGIDevice>().get(), d3d11_device.put()));
  return d3d11_device
      .as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

auto WgcSessionImpl::CreateCaptureItemForWindow(HWND hwnd) {
  auto interop_factory =
      winrt::get_activation_factory<
          winrt::Windows::Graphics::Capture::GraphicsCaptureItem>()
          .as<IGraphicsCaptureItemInterop>();
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
  interop_factory->CreateForWindow(
      hwnd,
      winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
      reinterpret_cast<void**>(winrt::put_abi(item)));
  return item;
}

auto WgcSessionImpl::CreateCaptureItemForMonitor(HMONITOR hmonitor) {
  auto interop_factory =
      winrt::get_activation_factory<
          winrt::Windows::Graphics::Capture::GraphicsCaptureItem>()
          .as<IGraphicsCaptureItemInterop>();
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
  interop_factory->CreateForMonitor(
      hmonitor,
      winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
      reinterpret_cast<void**>(winrt::put_abi(item)));
  return item;
}

HRESULT WgcSessionImpl::CreateMappedTexture(
    winrt::com_ptr<ID3D11Texture2D> src_texture, unsigned int width,
    unsigned int height) {
  D3D11_TEXTURE2D_DESC src_desc;
  src_texture->GetDesc(&src_desc);

  D3D11_TEXTURE2D_DESC map_desc = src_desc;
  map_desc.Width = width ? width : src_desc.Width;
  map_desc.Height = height ? height : src_desc.Height;
  map_desc.Usage = D3D11_USAGE_STAGING;
  map_desc.BindFlags = 0;
  map_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  map_desc.MiscFlags = 0;

  auto d3dDevice =
      GetDXGIInterfaceFromObject<ID3D11Device>(d3d11_direct_device_);

  return d3dDevice->CreateTexture2D(&map_desc, nullptr,
                                    d3d11_texture_mapped_.put());
}

void WgcSessionImpl::OnFrame(
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
    [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args) {
  if (!is_running_ || is_paused_) return;
  std::scoped_lock locker(lock_);

  if (!observer_) return;

  auto frame = sender.TryGetNextFrame();
  if (!frame) return;

  auto frame_size = frame.ContentSize();
  bool size_changed = (frame_size.Width != capture_frame_size_.Width ||
                       frame_size.Height != capture_frame_size_.Height);
  if (size_changed) {
    capture_frame_size_ = frame_size;
    capture_framepool_.Recreate(d3d11_direct_device_,
                                winrt::Windows::Graphics::DirectX::
                                    DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                2, capture_frame_size_);
  }

  auto frame_surface =
      GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());

  if (!d3d11_texture_mapped_ || size_changed)
    CreateMappedTexture(frame_surface, frame_size.Width, frame_size.Height);

  d3d11_device_context_->CopyResource(d3d11_texture_mapped_.get(),
                                      frame_surface.get());

  D3D11_MAPPED_SUBRESOURCE map_result;
  HRESULT hr = d3d11_device_context_->Map(d3d11_texture_mapped_.get(), 0,
                                          D3D11_MAP_READ, 0, &map_result);
  if (SUCCEEDED(hr)) {
    wgc_session_frame frame_info{
        static_cast<unsigned int>(frame_size.Width),
        static_cast<unsigned int>(frame_size.Height), map_result.RowPitch,
        reinterpret_cast<const unsigned char*>(map_result.pData)};
    observer_->OnFrame(frame_info, id_);
    d3d11_device_context_->Unmap(d3d11_texture_mapped_.get(), 0);
  }
}

void WgcSessionImpl::OnClosed(
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem const&,
    winrt::Windows::Foundation::IInspectable const&) {
  OutputDebugStringW(L"WgcSessionImpl::OnClosed");
  Stop();
}

int WgcSessionImpl::Initialize() {
  if (is_initialized_) return 0;

  d3d11_direct_device_ = CreateD3D11Device();
  if (!d3d11_direct_device_) {
    std::cout << "AE_D3D_CREATE_DEVICE_FAILED" << std::endl;
    return 1;
  }

  try {
    capture_item_ = target_.is_window
                        ? CreateCaptureItemForWindow(target_.hwnd)
                        : CreateCaptureItemForMonitor(target_.hmonitor);

    auto d3d_device =
        GetDXGIInterfaceFromObject<ID3D11Device>(d3d11_direct_device_);
    d3d_device->GetImmediateContext(d3d11_device_context_.put());
  } catch (...) {
    LOG_ERROR("AE_WGC_CREATE_CAPTURER_FAILED");
    return 86;
  }

  is_initialized_ = true;
  return 0;
}

void WgcSessionImpl::CleanUp() {
  std::scoped_lock locker(lock_);
  if (cleaned_.exchange(true)) return;

  try {
    if (capture_framepool_trigger_) capture_framepool_trigger_.revoke();
    if (capture_close_trigger_) capture_close_trigger_.revoke();
    if (capture_framepool_) capture_framepool_.Close();
    if (capture_session_) capture_session_.Close();
  } catch (...) {
  }

  capture_framepool_ = nullptr;
  capture_session_ = nullptr;
  capture_item_ = nullptr;
  d3d11_texture_mapped_ = nullptr;
  d3d11_device_context_ = nullptr;
  d3d11_direct_device_ = nullptr;

  is_initialized_ = false;
  is_running_ = false;
}
