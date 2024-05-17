#include "screen_capturer_wgc.h"

#include <Windows.h>
#include <d3d11_4.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>

#include <iostream>

#include "libyuv.h"

BOOL WINAPI EnumMonitorProc(HMONITOR hmonitor, HDC hdc, LPRECT lprc,
                            LPARAM data) {
  MONITORINFOEX info_ex;
  info_ex.cbSize = sizeof(MONITORINFOEX);

  GetMonitorInfo(hmonitor, &info_ex);

  if (info_ex.dwFlags == DISPLAY_DEVICE_MIRRORING_DRIVER) return true;

  if (info_ex.dwFlags & MONITORINFOF_PRIMARY) {
    *(HMONITOR *)data = hmonitor;
  }

  return true;
}

HMONITOR GetPrimaryMonitor() {
  HMONITOR hmonitor = nullptr;

  ::EnumDisplayMonitors(NULL, NULL, EnumMonitorProc, (LPARAM)&hmonitor);

  return hmonitor;
}

ScreenCapturerWgc::ScreenCapturerWgc() {}

ScreenCapturerWgc::~ScreenCapturerWgc() {}

bool ScreenCapturerWgc::IsWgcSupported() {
  try {
    /* no contract for IGraphicsCaptureItemInterop, verify 10.0.18362.0 */
    return winrt::Windows::Foundation::Metadata::ApiInformation::
        IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 8);
  } catch (const winrt::hresult_error &) {
    return false;
  } catch (...) {
    return false;
  }
}

int ScreenCapturerWgc::Init(const RECORD_DESKTOP_RECT &rect, const int fps,
                            cb_desktop_data cb) {
  int error = 0;
  if (_inited == true) return error;

  int r = rect.right;
  int b = rect.bottom;

  nv12_frame_ = new unsigned char[rect.right * rect.bottom * 3 / 2];
  nv12_frame_scaled_ = new unsigned char[1280 * 720 * 3 / 2];

  _fps = fps;

  _on_data = cb;

  do {
    if (!IsWgcSupported()) {
      std::cout << "AE_UNSUPPORT" << std::endl;
      error = 2;
      break;
    }

    session_ = new WgcSessionImpl();
    if (!session_) {
      error = -1;
      std::cout << "AE_WGC_CREATE_CAPTURER_FAILED" << std::endl;
      break;
    }

    session_->RegisterObserver(this);

    error = session_->Initialize(GetPrimaryMonitor());

    _inited = true;
  } while (0);

  if (error != 0) {
  }

  return error;
}

int ScreenCapturerWgc::Destroy() {
  if (nv12_frame_) {
    delete nv12_frame_;
    nv12_frame_ = nullptr;
  }

  if (nv12_frame_scaled_) {
    delete nv12_frame_scaled_;
    nv12_frame_scaled_ = nullptr;
  }

  Stop();
  CleanUp();

  return 0;
}

int ScreenCapturerWgc::Start() {
  if (_running == true) {
    std::cout << "record desktop duplication is already running" << std::endl;
    return 0;
  }

  if (_inited == false) {
    std::cout << "AE_NEED_INIT" << std::endl;
    return 4;
  }

  _running = true;
  session_->Start();

  return 0;
}

int ScreenCapturerWgc::Pause() {
  _paused = true;
  if (session_) session_->Pause();
  return 0;
}

int ScreenCapturerWgc::Resume() {
  _paused = false;
  if (session_) session_->Resume();
  return 0;
}

int ScreenCapturerWgc::Stop() {
  _running = false;

  if (session_) session_->Stop();

  return 0;
}

void ConvertABGRtoBGRA(const uint8_t *abgr_data, uint8_t *bgra_data, int width,
                       int height, int abgr_stride, int bgra_stride) {
  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      // ABGR到BGRA的索引映射
      int abgr_index = (i * abgr_stride + j) * 4;
      int bgra_index = (i * bgra_stride + j) * 4;

      // 直接交换蓝色和红色分量，同时保持Alpha通道不变
      bgra_data[bgra_index + 0] = abgr_data[abgr_index + 2];  // 蓝色
      bgra_data[bgra_index + 1] = abgr_data[abgr_index + 1];  // 绿色
      bgra_data[bgra_index + 2] = abgr_data[abgr_index + 0];  // 红色
      bgra_data[bgra_index + 3] = abgr_data[abgr_index + 3];  // Alpha
    }
  }
}

void ConvertBGRAtoABGR(const uint8_t *bgra_data, uint8_t *abgr_data, int width,
                       int height, int bgra_stride, int abgr_stride) {
  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      // BGRA到ABGR的索引映射
      int bgra_index = (i * bgra_stride + j) * 4;
      int abgr_index = (i * abgr_stride + j) * 4;

      // 交换红色和蓝色分量，同时保持Alpha通道在最前面
      abgr_data[abgr_index + 0] = bgra_data[bgra_index + 3];  // Alpha
      abgr_data[abgr_index + 1] = bgra_data[bgra_index + 0];  // Blue
      abgr_data[abgr_index + 2] = bgra_data[bgra_index + 1];  // Green
      abgr_data[abgr_index + 3] = bgra_data[bgra_index + 2];  // Red
    }
  }
}

void ScreenCapturerWgc::OnFrame(const WgcSession::wgc_session_frame &frame) {
  if (_on_data) {
    int width = 1280;
    int height = 720;

    // libyuv::ARGBToI420(frame.data, frame.width * 4, yuv420_frame_,
    // frame.width,
    //                    yuv420_frame_ + frame.width * frame.height,
    //                    frame.width / 2,
    //                    yuv420_frame_ + frame.width * frame.height * 5 / 4,
    //                    frame.width / 2, frame.width, frame.height);

    // libyuv::I420Scale(
    //     (const uint8_t *)yuv420_frame_, frame.width,
    //     (const uint8_t *)(yuv420_frame_ + frame.width * frame.height),
    //     frame.width / 2,
    //     (const uint8_t *)(yuv420_frame_ + frame.width * frame.height * 5 /
    //     4), frame.width / 2, frame.width, frame.height, (uint8_t
    //     *)yuv420_frame_, width, (uint8_t *)(yuv420_frame_ + width * height),
    //     width / 2, (uint8_t *)(yuv420_frame_ + width * height * 5 / 4), width
    //     / 2, width, height, libyuv::FilterMode::kFilterLinear);

    // libyuv::I420ToNV12(
    //     (const uint8_t *)yuv420_frame_, width,
    //     (const uint8_t *)(yuv420_frame_ + width * height), width / 2,
    //     (const uint8_t *)(yuv420_frame_ + width * height * 5 / 4), width / 2,
    //     nv12_frame_, width, nv12_frame_ + width * height, width, width,
    //     height);

    libyuv::ARGBToNV12(frame.data, frame.width * 4, nv12_frame_, frame.width,
                       nv12_frame_ + frame.width * frame.height,
                       frame.width / 2, frame.width, frame.height);

    libyuv::NV12Scale(
        (const uint8_t *)nv12_frame_, frame.width,
        (const uint8_t *)(nv12_frame_ + frame.width * frame.height),
        frame.width / 2, frame.width, frame.height,
        (uint8_t *)nv12_frame_scaled_, width,
        (uint8_t *)(nv12_frame_scaled_ + width * height), width / 2, width,
        height, libyuv::FilterMode::kFilterLinear);

    _on_data(nv12_frame_scaled_, width * height * 3 / 2, width, height);
  }
}

void ScreenCapturerWgc::CleanUp() {
  _inited = false;

  if (session_) session_->Release();

  session_ = nullptr;
}
