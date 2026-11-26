#include "wgc_session_impl.h"

#include <Windows.Graphics.Capture.Interop.h>

#include <string>

#include "capture_resolution.h"
#include "rd_log.h"

namespace crossdesk {

extern "C" {
HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(
    ::IDXGIDevice* dxgiDevice, ::IInspectable** graphicsDevice);
}

WgcSessionImpl::WgcSessionImpl(int id) : id_(id) {}

WgcSessionImpl::~WgcSessionImpl() {
  Stop();
  CleanUp();
}

void WgcSessionImpl::Release() { delete this; }

int WgcSessionImpl::Initialize(HWND hwnd) {
  std::lock_guard locker(lock_);

  target_.hwnd = hwnd;
  target_.is_window = true;
  return InitializeLocked();
}

int WgcSessionImpl::Initialize(HMONITOR hmonitor) {
  std::lock_guard locker(lock_);

  target_.hmonitor = hmonitor;
  target_.is_window = false;
  return InitializeLocked();
}

void WgcSessionImpl::RegisterObserver(wgc_session_observer* observer) {
  std::lock_guard locker(lock_);
  observer_ = observer;
}

int WgcSessionImpl::Start(bool show_cursor) {
  std::lock_guard locker(lock_);

  return StartLocked(show_cursor);
}

int WgcSessionImpl::Stop() {
  std::lock_guard locker(lock_);

  CleanUpLocked();
  return 0;
}

int WgcSessionImpl::Pause() {
  std::lock_guard locker(lock_);

  is_paused_ = true;

  if (!is_initialized_) {
    LOG_ERROR("AE_NEED_INIT");
    return 4;
  }
  return 0;
}

int WgcSessionImpl::Resume() {
  std::lock_guard locker(lock_);

  is_paused_ = false;

  if (!is_initialized_) {
    LOG_ERROR("AE_NEED_INIT");
    return 4;
  }
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
    type = D3D_DRIVER_TYPE_WARP;
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
  winrt::check_hresult(interop_factory->CreateForWindow(
      hwnd,
      winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
      reinterpret_cast<void**>(winrt::put_abi(item))));
  return item;
}

auto WgcSessionImpl::CreateCaptureItemForMonitor(HMONITOR hmonitor) {
  auto activation_factory = winrt::get_activation_factory<
      winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
  auto interop_factory = activation_factory.as<IGraphicsCaptureItemInterop>();
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = {nullptr};
  winrt::check_hresult(interop_factory->CreateForMonitor(
      hmonitor,
      winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
      reinterpret_cast<void**>(winrt::put_abi(item))));
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

  d3d11_texture_mapped_ = nullptr;
  const HRESULT hr = d3dDevice->CreateTexture2D(&map_desc, nullptr,
                                                d3d11_texture_mapped_.put());
  if (SUCCEEDED(hr)) {
    mapped_width_ = map_desc.Width;
    mapped_height_ = map_desc.Height;
  } else {
    mapped_width_ = 0;
    mapped_height_ = 0;
  }
  return hr;
}

void WgcSessionImpl::ResetVideoScalerLocked() {
  video_output_view_ = nullptr;
  video_input_view_ = nullptr;
  video_output_tex_ = nullptr;
  video_input_tex_ = nullptr;
  video_processor_ = nullptr;
  video_enumerator_ = nullptr;
  video_context_ = nullptr;
  video_device_ = nullptr;
  video_src_width_ = 0;
  video_src_height_ = 0;
  video_dst_width_ = 0;
  video_dst_height_ = 0;
}

winrt::com_ptr<ID3D11Texture2D> WgcSessionImpl::ScaleOversizedTexture(
    const winrt::com_ptr<ID3D11Texture2D>& src_texture, UINT target_width,
    UINT target_height) {
  if (gpu_scale_disabled_ || !src_texture || target_width < 2 ||
      target_height < 2) {
    return nullptr;
  }
  D3D11_TEXTURE2D_DESC src_desc{};
  src_texture->GetDesc(&src_desc);
  if (!video_processor_ || video_src_width_ != src_desc.Width ||
      video_src_height_ != src_desc.Height ||
      video_dst_width_ != target_width || video_dst_height_ != target_height) {
    ResetVideoScalerLocked();
    auto d3d_device =
        GetDXGIInterfaceFromObject<ID3D11Device>(d3d11_direct_device_);
    video_device_ = d3d_device.try_as<ID3D11VideoDevice>();
    video_context_ = d3d11_device_context_.try_as<ID3D11VideoContext>();
    if (!video_device_ || !video_context_) {
      ResetVideoScalerLocked();
      gpu_scale_disabled_ = true;
      return nullptr;
    }
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputWidth = src_desc.Width;
    content.InputHeight = src_desc.Height;
    content.OutputWidth = target_width;
    content.OutputHeight = target_height;
    content.InputFrameRate = content.OutputFrameRate = {60, 1};
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    UINT input_support = 0;
    UINT output_support = 0;
    if (FAILED(video_device_->CreateVideoProcessorEnumerator(
            &content, video_enumerator_.put())) ||
        FAILED(video_enumerator_->CheckVideoProcessorFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, &input_support)) ||
        !(input_support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) ||
        FAILED(video_enumerator_->CheckVideoProcessorFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, &output_support)) ||
        !(output_support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) ||
        FAILED(video_device_->CreateVideoProcessor(
            video_enumerator_.get(), 0, video_processor_.put()))) {
      ResetVideoScalerLocked();
      gpu_scale_disabled_ = true;
      return nullptr;
    }

    D3D11_TEXTURE2D_DESC input_desc = src_desc;
    input_desc.Usage = D3D11_USAGE_DEFAULT;
    input_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    input_desc.CPUAccessFlags = 0;
    input_desc.MiscFlags = 0;
    D3D11_TEXTURE2D_DESC output_desc = input_desc;
    output_desc.Width = target_width;
    output_desc.Height = target_height;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_view_desc{};
    input_view_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_view_desc{};
    output_view_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    if (FAILED(d3d_device->CreateTexture2D(&input_desc, nullptr,
                                           video_input_tex_.put())) ||
        FAILED(d3d_device->CreateTexture2D(&output_desc, nullptr,
                                           video_output_tex_.put())) ||
        FAILED(video_device_->CreateVideoProcessorInputView(
            video_input_tex_.get(), video_enumerator_.get(), &input_view_desc,
            video_input_view_.put())) ||
        FAILED(video_device_->CreateVideoProcessorOutputView(
            video_output_tex_.get(), video_enumerator_.get(), &output_view_desc,
            video_output_view_.put()))) {
      ResetVideoScalerLocked();
      gpu_scale_disabled_ = true;
      return nullptr;
    }

    D3D11_VIDEO_PROCESSOR_COLOR_SPACE rgb{};
    rgb.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
    video_context_->VideoProcessorSetStreamColorSpace(video_processor_.get(), 0,
                                                      &rgb);
    video_context_->VideoProcessorSetOutputColorSpace(video_processor_.get(),
                                                      &rgb);
    video_context_->VideoProcessorSetStreamFrameFormat(
        video_processor_.get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    const RECT src_rect{0, 0, static_cast<LONG>(src_desc.Width),
                        static_cast<LONG>(src_desc.Height)};
    const RECT dst_rect{0, 0, static_cast<LONG>(target_width),
                        static_cast<LONG>(target_height)};
    video_context_->VideoProcessorSetStreamSourceRect(video_processor_.get(), 0,
                                                      TRUE, &src_rect);
    video_context_->VideoProcessorSetStreamDestRect(video_processor_.get(), 0,
                                                    TRUE, &dst_rect);
    video_context_->VideoProcessorSetOutputTargetRect(video_processor_.get(),
                                                      TRUE, &dst_rect);
    video_context_->VideoProcessorSetStreamAutoProcessingMode(
        video_processor_.get(), 0, FALSE);
    video_src_width_ = src_desc.Width;
    video_src_height_ = src_desc.Height;
    video_dst_width_ = target_width;
    video_dst_height_ = target_height;
  }

  d3d11_device_context_->CopyResource(video_input_tex_.get(),
                                      src_texture.get());
  D3D11_VIDEO_PROCESSOR_STREAM stream{};
  stream.Enable = TRUE;
  stream.pInputSurface = video_input_view_.get();
  if (FAILED(video_context_->VideoProcessorBlt(
          video_processor_.get(), video_output_view_.get(), 0, 1, &stream))) {
    ResetVideoScalerLocked();
    gpu_scale_disabled_ = true;
    return nullptr;
  }
  return video_output_tex_;
}

int WgcSessionImpl::StartCaptureLocked(bool show_cursor) {
  if (!is_initialized_) {
    LOG_ERROR("AE_NEED_INIT");
    return 4;
  }
  if (!capture_item_) {
    LOG_ERROR("WGC: capture item is null");
    return 4;
  }

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

    if (!capture_framepool_ || !capture_session_) {
      throw std::exception();
    }

    capture_session_.IsCursorCaptureEnabled(show_cursor);
    LOG_INFO("WGC: session {} cursor capture enabled={}", id_,
             capture_session_.IsCursorCaptureEnabled());
    capture_session_.StartCapture();
    is_running_ = true;
    return 0;
  } catch (const winrt::hresult_error& error) {
    LOG_ERROR("WGC: start capture failed, hr=0x{:08X}, thread={}",
              static_cast<uint32_t>(error.code().value), GetCurrentThreadId());
    return 86;
  } catch (...) {
    LOG_ERROR("AE_WGC_CREATE_CAPTURER_FAILED");
    return 86;
  }
}

int WgcSessionImpl::StartLocked(bool show_cursor) {
  const bool was_paused = is_paused_;
  if (is_running_) {
    if (last_show_cursor_ == show_cursor) return 0;
    // Replace the pool along with the setting, so buffered frames from the
    // previous cursor mode cannot be labeled with the new mode.
    StopLocked();
  }

  last_show_cursor_ = show_cursor;
  if (!is_initialized_) {
    const int init_ret = InitializeLocked();
    if (init_ret != 0) {
      return init_ret;
    }
  }

  int ret = StartCaptureLocked(show_cursor);
  if (ret == 0) {
    return 0;
  }

  LOG_WARN("WGC: start capture failed, rebuilding capture item");
  CleanUpLocked();
  is_paused_ = was_paused;
  ret = InitializeLocked();
  if (ret != 0) {
    return ret;
  }
  return StartCaptureLocked(show_cursor);
}

void WgcSessionImpl::StopLocked() {
  is_running_ = false;

  // if (loop_.joinable()) loop_.join();

  if (capture_framepool_trigger_) capture_framepool_trigger_.revoke();
  if (capture_close_trigger_) capture_close_trigger_.revoke();

  if (capture_session_) {
    try {
      capture_session_.Close();
    } catch (...) {
      LOG_WARN("WGC: capture session close failed");
    }
    capture_session_ = nullptr;
  }

  if (capture_framepool_) {
    try {
      capture_framepool_.Close();
    } catch (...) {
      LOG_WARN("WGC: frame pool close failed");
    }
    capture_framepool_ = nullptr;
  }

  d3d11_texture_mapped_ = nullptr;
  mapped_width_ = 0;
  mapped_height_ = 0;
  ResetVideoScalerLocked();
  gpu_scale_disabled_ = false;
}

void WgcSessionImpl::OnFrame(
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
    [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args) {
  std::lock_guard locker(lock_);

  if (!is_running_ || sender != capture_framepool_) return;

  try {
    auto frame = sender.TryGetNextFrame();
    if (!frame || is_paused_) return;
    const auto frame_size = frame.ContentSize();
    if (frame_size.Width <= 0 || frame_size.Height <= 0) return;

    if (frame_size.Width != capture_frame_size_.Width ||
        frame_size.Height != capture_frame_size_.Height) {
      // ContentSize already describes the new content, while this surface
      // still belongs to the old pool. Retire it before mapping any pixels.
      frame.Close();
      d3d11_texture_mapped_ = nullptr;
      mapped_width_ = 0;
      mapped_height_ = 0;
      ResetVideoScalerLocked();
      gpu_scale_disabled_ = false;
      capture_framepool_.Recreate(
          d3d11_direct_device_,
          winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
          2, frame_size);
      capture_frame_size_ = frame_size;
      return;
    }

    auto frame_captured =
        GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
    D3D11_TEXTURE2D_DESC desc{};
    frame_captured->GetDesc(&desc);
    if (desc.Width != static_cast<UINT>(frame_size.Width) ||
        desc.Height != static_cast<UINT>(frame_size.Height) ||
        desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
      return;
    }

    winrt::com_ptr<ID3D11Texture2D> readback_source = frame_captured;
    unsigned int readback_width = static_cast<unsigned int>(frame_size.Width);
    unsigned int readback_height = static_cast<unsigned int>(frame_size.Height);
    if (IsOversizedCaptureResolution(frame_size.Width, frame_size.Height)) {
      const auto target =
          ClampCaptureResolution(frame_size.Width, frame_size.Height);
      if (target.valid()) {
        if (auto scaled = ScaleOversizedTexture(
                frame_captured, static_cast<UINT>(target.width),
                static_cast<UINT>(target.height))) {
          readback_source = scaled;
          readback_width = static_cast<unsigned int>(target.width);
          readback_height = static_cast<unsigned int>(target.height);
        }
      }
    }

    if (!d3d11_texture_mapped_ || mapped_width_ != readback_width ||
        mapped_height_ != readback_height) {
      HRESULT tex_hr =
          CreateMappedTexture(readback_source, readback_width, readback_height);
      if (FAILED(tex_hr)) {
        ReportFrameFailure(tex_hr);
        return;
      }
    }

    d3d11_device_context_->CopyResource(d3d11_texture_mapped_.get(),
                                        readback_source.get());

    D3D11_MAPPED_SUBRESOURCE map_result;
    HRESULT hr = d3d11_device_context_->Map(
        d3d11_texture_mapped_.get(), 0, D3D11_MAP_READ,
        0 /*coz we use CreateFreeThreaded, so we cant use flags
             D3D11_MAP_FLAG_DO_NOT_WAIT*/
        ,
        &map_result);
    if (FAILED(hr)) {
      ReportFrameFailure(hr);
      return;
    }

    {
      struct UnmapOnExit {
        ID3D11DeviceContext* context;
        ID3D11Texture2D* texture;
        ~UnmapOnExit() { context->Unmap(texture, 0); }
      } unmap{d3d11_device_context_.get(), d3d11_texture_mapped_.get()};
      // copy data from map_result.pData
      if (map_result.pData && observer_) {
        CURSORINFO cursor{};
        cursor.cbSize = sizeof(cursor);
        const bool cursor_captured =
            last_show_cursor_ && GetCursorInfo(&cursor) && cursor.hCursor &&
            (cursor.flags & CURSOR_SHOWING) != 0 &&
            (target_.is_window ||
             MonitorFromPoint(cursor.ptScreenPos, MONITOR_DEFAULTTONULL) ==
                 target_.hmonitor);
        observer_->OnFrame(
            wgc_session_frame{readback_width, readback_height,
                              map_result.RowPitch,
                              const_cast<const unsigned char*>(
                                  (unsigned char*)map_result.pData),
                              cursor_captured},
            id_);
        frame_failure_reported_ = false;
      }
    }
  } catch (const winrt::hresult_error& error) {
    ReportFrameFailure(error.code());
  } catch (...) {
    ReportFrameFailure(E_FAIL);
  }
}

void WgcSessionImpl::ReportFrameFailure(HRESULT error) {
  if (frame_failure_reported_) return;
  frame_failure_reported_ = true;
  LOG_WARN("WGC: frame processing failed, hr=0x{:08X}", static_cast<uint32_t>(error));
  // Let both normal and secure capture advance to GDI rather than replay a
  // stale image indefinitely after a readback/device failure.
  if (observer_) observer_->OnFrame({}, id_);
}

void WgcSessionImpl::OnClosed(
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem const&,
    winrt::Windows::Foundation::IInspectable const&) {
  std::lock_guard locker(lock_);
  if (observer_) observer_->OnFrame({}, id_);
  const bool was_running = is_running_;
  const bool was_paused = is_paused_;
  try {
    CleanUpLocked();
    is_paused_ = was_paused;
    if (InitializeLocked() == 0) {
      int ret = was_running ? StartCaptureLocked(last_show_cursor_) : 0;
      if (ret == 0) {
        OutputDebugStringW(L"WgcSessionImpl::OnClosed: auto recovered");
      } else {
        OutputDebugStringW(L"WgcSessionImpl::OnClosed: recover Start failed");
      }
    } else {
      OutputDebugStringW(
          L"WgcSessionImpl::OnClosed: recover Initialize failed");
    }
  } catch (...) {
    OutputDebugStringW(L"WgcSessionImpl::OnClosed: exception during recover");
  }
}

int WgcSessionImpl::InitializeLocked() {
  if (is_initialized_) return 0;

  d3d11_texture_mapped_ = nullptr;
  d3d11_device_context_ = nullptr;
  d3d11_direct_device_ = nullptr;
  capture_frame_size_ = {};

  if (!(d3d11_direct_device_ = CreateD3D11Device())) {
    LOG_ERROR("AE_D3D_CREATE_DEVICE_FAILED");
    return 1;
  }

  try {
    if (target_.is_window)
      capture_item_ = CreateCaptureItemForWindow(target_.hwnd);
    else
      capture_item_ = CreateCaptureItemForMonitor(target_.hmonitor);
    if (!capture_item_) {
      LOG_ERROR("WGC: create capture item returned null");
      return 86;
    }

    // Set up
    auto d3d11_device =
        GetDXGIInterfaceFromObject<ID3D11Device>(d3d11_direct_device_);
    d3d11_device->GetImmediateContext(d3d11_device_context_.put());

  } catch (const winrt::hresult_error& error) {
    LOG_ERROR("WGC: create capture item failed, hr=0x{:08X}, thread={}",
              static_cast<uint32_t>(error.code().value), GetCurrentThreadId());
    return 86;
  } catch (...) {
    return 86;
  }

  is_initialized_ = true;

  return 0;
}

void WgcSessionImpl::CleanUp() {
  std::lock_guard locker(lock_);

  CleanUpLocked();
}

void WgcSessionImpl::CleanUpLocked() {
  StopLocked();

  capture_item_ = nullptr;
  d3d11_device_context_ = nullptr;
  d3d11_direct_device_ = nullptr;
  capture_frame_size_ = {};
  is_initialized_ = false;
  is_paused_ = false;
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
}  // namespace crossdesk
