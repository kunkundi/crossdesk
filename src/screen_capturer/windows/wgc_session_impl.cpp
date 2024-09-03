#include "wgc_session_impl.h"

#include <Windows.Graphics.Capture.Interop.h>

#include <atomic>
#include <functional>
#include <iostream>
#include <memory>

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
    ::IDXGIDevice *dxgiDevice, ::IInspectable **graphicsDevice);
}

WgcSessionImpl::WgcSessionImpl() {}

WgcSessionImpl::~WgcSessionImpl() {
  Stop();
  CleanUp();
}

void WgcSessionImpl::Release() { delete this; }

int WgcSessionImpl::Initialize(HWND hwnd) {
  std::lock_guard locker(lock_);

  target_.hwnd = hwnd;
  target_.is_window = true;
  return Initialize();
}

int WgcSessionImpl::Initialize(HMONITOR hmonitor) {
  std::lock_guard locker(lock_);

  target_.hmonitor = hmonitor;
  target_.is_window = false;
  return Initialize();
}

void WgcSessionImpl::RegisterObserver(wgc_session_observer *observer) {
  std::lock_guard locker(lock_);
  observer_ = observer;
}

int WgcSessionImpl::Start() {
  std::lock_guard locker(lock_);

  if (is_running_) return 0;

  int error = 1;

  CHECK_INIT;
  try {
    if (!capture_session_) {
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
    }

    if (!capture_framepool_) throw std::exception();

    is_running_ = true;

    // we do not need to crate a thread to enter a message loop coz we use
    // CreateFreeThreaded instead of Create to create a capture frame pool,
    // we need to test the performance later
    // loop_ = std::thread(std::bind(&WgcSessionImpl::message_func, this));

    capture_session_.StartCapture();

    capture_session_.IsCursorCaptureEnabled(false);

    error = 0;
  } catch (winrt::hresult_error) {
    std::cout << "AE_WGC_CREATE_CAPTURER_FAILED" << std::endl;
    return 86;
  } catch (...) {
    return 86;
  }

  return error;
}

int WgcSessionImpl::Stop() {
  std::lock_guard locker(lock_);

  CHECK_INIT;

  is_running_ = false;

  // if (loop_.joinable()) loop_.join();

  if (capture_framepool_trigger_) capture_framepool_trigger_.revoke();

  if (capture_session_) {
    capture_session_.Close();
    capture_session_ = nullptr;
  }

  return 0;
}

int WgcSessionImpl::Pause() {
  std::lock_guard locker(lock_);

  CHECK_INIT;
  return 0;
}

int WgcSessionImpl::Resume() {
  std::lock_guard locker(lock_);

  CHECK_INIT;
  return 0;
}

auto WgcSessionImpl::CreateD3D11Device() {
  winrt::com_ptr<ID3D11Device> d3d_device;
  HRESULT hr;

  WINRT_ASSERT(!d3d_device);

  D3D_DRIVER_TYPE type = D3D_DRIVER_TYPE_HARDWARE;
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  hr = D3D11CreateDevice(nullptr, type, nullptr, flags, nullptr, 0,
                         D3D11_SDK_VERSION, d3d_device.put(), nullptr, nullptr);

  if (DXGI_ERROR_UNSUPPORTED == hr) {
    // change D3D_DRIVER_TYPE
    D3D_DRIVER_TYPE type = D3D_DRIVER_TYPE_WARP;
    hr = D3D11CreateDevice(nullptr, type, nullptr, flags, nullptr, 0,
                           D3D11_SDK_VERSION, d3d_device.put(), nullptr,
                           nullptr);
  }

  winrt::check_hresult(hr);

  winrt::com_ptr<::IInspectable> d3d11_device;
  winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(
      d3d_device.as<IDXGIDevice>().get(), d3d11_device.put()));
  return d3d11_device
      .as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

auto WgcSessionImpl::CreateCaptureItemForWindow(HWND hwnd) {
  auto activation_factory = winrt::get_activation_factory<
      winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
  auto interop_factory = activation_factory.as<IGraphicsCaptureItemInterop>();
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = {nullptr};
  interop_factory->CreateForWindow(
      hwnd,
      winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
      reinterpret_cast<void **>(winrt::put_abi(item)));
  return item;
}

auto WgcSessionImpl::CreateCaptureItemForMonitor(HMONITOR hmonitor) {
  auto activation_factory = winrt::get_activation_factory<
      winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
  auto interop_factory = activation_factory.as<IGraphicsCaptureItemInterop>();
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = {nullptr};
  interop_factory->CreateForMonitor(
      hmonitor,
      winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
      reinterpret_cast<void **>(winrt::put_abi(item)));
  return item;
}

HRESULT WgcSessionImpl::CreateMappedTexture(
    winrt::com_ptr<ID3D11Texture2D> src_texture, unsigned int width,
    unsigned int height) {
  D3D11_TEXTURE2D_DESC src_desc;
  src_texture->GetDesc(&src_desc);
  D3D11_TEXTURE2D_DESC map_desc;
  map_desc.Width = width == 0 ? src_desc.Width : width;
  map_desc.Height = height == 0 ? src_desc.Height : height;
  map_desc.MipLevels = src_desc.MipLevels;
  map_desc.ArraySize = src_desc.ArraySize;
  map_desc.Format = src_desc.Format;
  map_desc.SampleDesc = src_desc.SampleDesc;
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
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const &sender,
    winrt::Windows::Foundation::IInspectable const &args) {
  std::lock_guard locker(lock_);

  auto is_new_size = false;

  {
    auto frame = sender.TryGetNextFrame();
    auto frame_size = frame.ContentSize();

    if (frame_size.Width != capture_frame_size_.Width ||
        frame_size.Height != capture_frame_size_.Height) {
      // The thing we have been capturing has changed size.
      // We need to resize our swap chain first, then blit the pixels.
      // After we do that, retire the frame and then recreate our frame pool.
      is_new_size = true;
      capture_frame_size_ = frame_size;
    }

    // copy to mapped texture
    {
      auto frame_captured =
          GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());

      if (!d3d11_texture_mapped_ || is_new_size)
        CreateMappedTexture(frame_captured);

      d3d11_device_context_->CopyResource(d3d11_texture_mapped_.get(),
                                          frame_captured.get());

      D3D11_MAPPED_SUBRESOURCE map_result;
      HRESULT hr = d3d11_device_context_->Map(
          d3d11_texture_mapped_.get(), 0, D3D11_MAP_READ,
          0 /*coz we use CreateFreeThreaded, so we cant use flags
               D3D11_MAP_FLAG_DO_NOT_WAIT*/
          ,
          &map_result);
      if (FAILED(hr)) {
        OutputDebugStringW(
            (L"map resource failed: " + std::to_wstring(hr)).c_str());
      }

      // copy data from map_result.pData
      if (map_result.pData && observer_) {
        observer_->OnFrame(wgc_session_frame{
            static_cast<unsigned int>(frame_size.Width),
            static_cast<unsigned int>(frame_size.Height), map_result.RowPitch,
            const_cast<const unsigned char *>(
                (unsigned char *)map_result.pData)});
      }

      d3d11_device_context_->Unmap(d3d11_texture_mapped_.get(), 0);
    }
  }

  if (is_new_size) {
    capture_framepool_.Recreate(d3d11_direct_device_,
                                winrt::Windows::Graphics::DirectX::
                                    DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                2, capture_frame_size_);
  }
}

void WgcSessionImpl::OnClosed(
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem const &,
    winrt::Windows::Foundation::IInspectable const &) {
  OutputDebugStringW(L"WgcSessionImpl::OnClosed");
}

int WgcSessionImpl::Initialize() {
  if (is_initialized_) return 0;

  if (!(d3d11_direct_device_ = CreateD3D11Device())) {
    std::cout << "AE_D3D_CREATE_DEVICE_FAILED" << std::endl;
    return 1;
  }

  try {
    if (target_.is_window)
      capture_item_ = CreateCaptureItemForWindow(target_.hwnd);
    else
      capture_item_ = CreateCaptureItemForMonitor(target_.hmonitor);

    // Set up
    auto d3d11_device =
        GetDXGIInterfaceFromObject<ID3D11Device>(d3d11_direct_device_);
    d3d11_device->GetImmediateContext(d3d11_device_context_.put());

  } catch (winrt::hresult_error) {
    std::cout << "AE_WGC_CREATE_CAPTURER_FAILED" << std::endl;
    return 86;
  } catch (...) {
    return 86;
  }

  is_initialized_ = true;

  return 0;
}

void WgcSessionImpl::CleanUp() {
  std::lock_guard locker(lock_);

  auto expected = false;
  if (cleaned_.compare_exchange_strong(expected, true)) {
    capture_close_trigger_.revoke();
    capture_framepool_trigger_.revoke();

    if (capture_framepool_) capture_framepool_.Close();

    if (capture_session_) capture_session_.Close();

    capture_framepool_ = nullptr;
    capture_session_ = nullptr;
    capture_item_ = nullptr;

    is_initialized_ = false;
  }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param,
                            LPARAM l_param) {
  return DefWindowProc(window, message, w_param, l_param);
}

// void WgcSessionImpl::message_func() {
//   const std::wstring kClassName = L"am_fake_window";

//   WNDCLASS wc = {};

//   wc.style = CS_HREDRAW | CS_VREDRAW;
//   wc.lpfnWndProc = DefWindowProc;
//   wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
//   wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW);
//   wc.lpszClassName = kClassName.c_str();

//   if (!::RegisterClassW(&wc)) return;

//   hwnd_ = ::CreateWindowW(kClassName.c_str(), nullptr, WS_OVERLAPPEDWINDOW,
//   0,
//                           0, 0, 0, nullptr, nullptr, nullptr, nullptr);
//   MSG msg;
//   while (is_running_) {
//     while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
//       if (!is_running_) break;
//       TranslateMessage(&msg);
//       DispatchMessage(&msg);
//     }
//     Sleep(10);
//   }

//   ::CloseWindow(hwnd_);
//   ::DestroyWindow(hwnd_);
// }