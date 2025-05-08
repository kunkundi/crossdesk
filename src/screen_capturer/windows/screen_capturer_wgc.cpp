#include "screen_capturer_wgc.h"

#include <Windows.h>
#include <d3d11_4.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>

#include <iostream>

#include "libyuv.h"
#include "rd_log.h"

static std::vector<ScreenCapturer::DisplayInfo> gs_display_list;

std::string WideToUtf8(const wchar_t *wideStr) {
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0,
                                        nullptr, nullptr);
  std::string result(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, &result[0], size_needed, nullptr,
                      nullptr);
  result.pop_back();
  return result;
}

BOOL WINAPI EnumMonitorProc(HMONITOR hmonitor, [[maybe_unused]] HDC hdc,
                            [[maybe_unused]] LPRECT lprc, LPARAM data) {
  MONITORINFOEX monitor_info_;
  monitor_info_.cbSize = sizeof(MONITORINFOEX);

  if (GetMonitorInfo(hmonitor, &monitor_info_)) {
    gs_display_list.push_back(
        {(void *)hmonitor, WideToUtf8(monitor_info_.szDevice),
         (monitor_info_.dwFlags & MONITORINFOF_PRIMARY) ? true : false,
         monitor_info_.rcMonitor.left, monitor_info_.rcMonitor.top,
         monitor_info_.rcMonitor.right, monitor_info_.rcMonitor.bottom});
  }

  if (monitor_info_.dwFlags == DISPLAY_DEVICE_MIRRORING_DRIVER) return true;

  if (monitor_info_.dwFlags & MONITORINFOF_PRIMARY) {
    *(HMONITOR *)data = hmonitor;
  }

  return true;
}

HMONITOR GetPrimaryMonitor() {
  HMONITOR hmonitor = nullptr;

  gs_display_list.clear();
  ::EnumDisplayMonitors(NULL, NULL, EnumMonitorProc, (LPARAM)&hmonitor);

  return hmonitor;
}

ScreenCapturerWgc::ScreenCapturerWgc() : monitor_(nullptr) {}

ScreenCapturerWgc::~ScreenCapturerWgc() {
  Stop();
  CleanUp();

  if (nv12_frame_) {
    delete nv12_frame_;
    nv12_frame_ = nullptr;
  }

  if (nv12_frame_scaled_) {
    delete nv12_frame_scaled_;
    nv12_frame_scaled_ = nullptr;
  }
}

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

int ScreenCapturerWgc::Init(const int fps, cb_desktop_data cb) {
  int error = 0;
  if (_inited == true) return error;

  // nv12_frame_ = new unsigned char[rect.right * rect.bottom * 3 / 2];
  // nv12_frame_scaled_ = new unsigned char[1280 * 720 * 3 / 2];

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

    monitor_ = GetPrimaryMonitor();

    display_list_ = gs_display_list;

    for (const auto &display : display_list_) {
      LOG_INFO("Display Name: {}, Is Primary: {}, Bounds: ({}, {}) - ({}, {})",
               display.name, (display.is_primary ? "Yes" : "No"), display.left,
               display.top, display.right, display.bottom);
    }

    error = session_->Initialize(monitor_);

    _inited = true;
  } while (0);

  if (error != 0) {
  }

  return error;
}

int ScreenCapturerWgc::Destroy() { return 0; }

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

int ScreenCapturerWgc::SwitchTo(int monitor_index) {
  if (!_inited) return -1;
  if (monitor_index >= display_list_.size()) {
    LOG_ERROR("Invalid monitor index: {}", monitor_index);
    return -3;
  }

  LOG_INFO("Switching to monitor {}:{}", monitor_index,
           display_list_[monitor_index].name);

  Stop();

  if (session_) {
    session_->Release();
    delete session_;
    session_ = nullptr;
  }

  session_ = new WgcSessionImpl();
  if (!session_) {
    LOG_ERROR("Failed to create new WgcSessionImpl.");
    return -4;
  }

  session_->RegisterObserver(this);

  int err = session_->Initialize((HMONITOR)display_list_[monitor_index].handle);
  if (err != 0) {
    LOG_ERROR("Failed to re-initialize session on new monitor.");
    return -5;
  }

  monitor_ = (HMONITOR)display_list_[monitor_index].handle;
  _inited = true;

  return Start();
}

void ConvertABGRtoBGRA(const uint8_t *abgr_data, uint8_t *bgra_data, int width,
                       int height, int abgr_stride, int bgra_stride) {
  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      int abgr_index = (i * abgr_stride + j) * 4;
      int bgra_index = (i * bgra_stride + j) * 4;

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
      int bgra_index = (i * bgra_stride + j) * 4;
      int abgr_index = (i * abgr_stride + j) * 4;

      abgr_data[abgr_index + 0] = bgra_data[bgra_index + 3];  // Alpha
      abgr_data[abgr_index + 1] = bgra_data[bgra_index + 0];  // Blue
      abgr_data[abgr_index + 2] = bgra_data[bgra_index + 1];  // Green
      abgr_data[abgr_index + 3] = bgra_data[bgra_index + 2];  // Red
    }
  }
}

void ScreenCapturerWgc::OnFrame(const WgcSession::wgc_session_frame &frame) {
  if (_on_data) {
    // int width = 1280;
    // int height = 720;

    if (!nv12_frame_) {
      nv12_frame_ = new unsigned char[frame.width * frame.height * 3 / 2];
    }

    libyuv::ARGBToNV12((const uint8_t *)frame.data, frame.width * 4,
                       (uint8_t *)nv12_frame_, frame.width,
                       (uint8_t *)(nv12_frame_ + frame.width * frame.height),
                       frame.width, frame.width, frame.height);

    _on_data(nv12_frame_, frame.width * frame.height * 3 / 2, frame.width,
             frame.height);
  }
}

void ScreenCapturerWgc::CleanUp() {
  _inited = false;

  if (session_) session_->Release();

  session_ = nullptr;
}
