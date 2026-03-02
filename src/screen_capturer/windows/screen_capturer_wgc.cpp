#include "screen_capturer_wgc.h"

#include <Windows.h>
#include <d3d11_4.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>

#include <iostream>

#include "libyuv.h"
#include "rd_log.h"

namespace crossdesk {

static std::vector<DisplayInfo> gs_display_list;

std::string WideToUtf8(const std::wstring& wstr) {
  if (wstr.empty()) return {};
  int size_needed = WideCharToMultiByte(
      CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
  std::string result(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), result.data(),
                      size_needed, nullptr, nullptr);
  return result;
}

std::string CleanDisplayName(const std::wstring& wide_name) {
  std::string name = WideToUtf8(wide_name);
  name.erase(std::remove_if(name.begin(), name.end(),
                            [](unsigned char c) { return !std::isalnum(c); }),
             name.end());
  return name;
}

BOOL WINAPI EnumMonitorProc(HMONITOR hmonitor, [[maybe_unused]] HDC hdc,
                            [[maybe_unused]] LPRECT lprc, LPARAM data) {
  MONITORINFOEX monitor_info_;
  monitor_info_.cbSize = sizeof(MONITORINFOEX);

  if (GetMonitorInfo(hmonitor, &monitor_info_)) {
    std::string display_name = CleanDisplayName(monitor_info_.szDevice);
    if (monitor_info_.dwFlags & MONITORINFOF_PRIMARY) {
      gs_display_list.insert(
          gs_display_list.begin(),
          {(void*)hmonitor, display_name,
           (monitor_info_.dwFlags & MONITORINFOF_PRIMARY) ? true : false,
           monitor_info_.rcMonitor.left, monitor_info_.rcMonitor.top,
           monitor_info_.rcMonitor.right, monitor_info_.rcMonitor.bottom});
      *(HMONITOR*)data = hmonitor;
    } else {
      gs_display_list.push_back(DisplayInfo(
          (void*)hmonitor, display_name,
          (monitor_info_.dwFlags & MONITORINFOF_PRIMARY) ? true : false,
          monitor_info_.rcMonitor.left, monitor_info_.rcMonitor.top,
          monitor_info_.rcMonitor.right, monitor_info_.rcMonitor.bottom));
    }
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
    delete[] nv12_frame_;
    nv12_frame_ = nullptr;
    nv12_width_ = 0;
    nv12_height_ = 0;
  }

  if (nv12_frame_scaled_) {
    delete[] nv12_frame_scaled_;
    nv12_frame_scaled_ = nullptr;
  }
}

bool ScreenCapturerWgc::IsWgcSupported() {
  try {
    /* no contract for IGraphicsCaptureItemInterop, verify 10.0.18362.0 */
    return winrt::Windows::Foundation::Metadata::ApiInformation::
        IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 8);
  } catch (const winrt::hresult_error&) {
    return false;
  } catch (...) {
    return false;
  }
}

int ScreenCapturerWgc::Init(const int fps, cb_desktop_data cb) {
  int error = 0;
  if (inited_ == true) return error;

  // nv12_frame_ = new unsigned char[rect.right * rect.bottom * 3 / 2];
  // nv12_frame_scaled_ = new unsigned char[1280 * 720 * 3 / 2];

  fps_ = fps;

  on_data_ = cb;

  if (!IsWgcSupported()) {
    LOG_ERROR("WGC not supported");
    error = 2;
    return error;
  }

  monitor_ = GetPrimaryMonitor();

  display_info_list_ = gs_display_list;

  if (display_info_list_.empty()) {
    LOG_ERROR("No display found");
    return -1;
  }

  for (int i = 0; i < display_info_list_.size(); i++) {
    const auto& display = display_info_list_[i];
    LOG_INFO(
        "index: {}, display name: {}, is primary: {}, bounds: ({}, {}) - "
        "({}, {})",
        i, display.name, (display.is_primary ? "yes" : "no"), display.left,
        display.top, display.right, display.bottom);

    sessions_.push_back(
        {std::make_unique<WgcSessionImpl>(i), false, false, false});
    sessions_.back().session_->RegisterObserver(this);
    error = sessions_.back().session_->Initialize((HMONITOR)display.handle);
    if (error != 0) {
      return error;
    }
    sessions_[i].inited_ = true;
    inited_ = true;
  }

  LOG_INFO("Default on monitor {}:{}", monitor_index_,
           display_info_list_[monitor_index_].name);

  initial_monitor_index_ = monitor_index_;
  return 0;
}

int ScreenCapturerWgc::Destroy() { return 0; }

int ScreenCapturerWgc::Start(bool show_cursor) {
  if (running_ == true) {
    LOG_ERROR("Screen capturer already running");
    return 0;
  }

  if (inited_ == false) {
    LOG_ERROR("Screen capturer not inited");
    return 4;
  }

  bool any_started = false;
  int last_error = 0;
  for (int i = 0; i < sessions_.size(); i++) {
    if (sessions_[i].inited_ == false) {
      LOG_ERROR("Session {} not inited", i);
      continue;
    }

    if (sessions_[i].running_) {
      LOG_ERROR("Session {} is already running", i);
    } else {
      int ret = sessions_[i].session_->Start(show_cursor);
      if (ret != 0) {
        LOG_ERROR("Session {} start failed, ret={}", i, ret);
        last_error = ret;
        continue;
      }

      if (i != 0) {
        sessions_[i].session_->Pause();
        sessions_[i].paused_ = true;
      }
      sessions_[i].running_ = true;
      any_started = true;
    }
    running_ = running_ || any_started;
  }

  if (!any_started) {
    LOG_ERROR("WGC: no session started successfully");
    return last_error != 0 ? last_error : -1;
  }
  return 0;
}

int ScreenCapturerWgc::Pause(int monitor_index) {
  if (monitor_index >= sessions_.size() || monitor_index < 0) {
    LOG_ERROR("Invalid session index: {}", monitor_index);
    return -1;
  }

  if (!sessions_[monitor_index].paused_) {
    sessions_[monitor_index].session_->Pause();
    sessions_[monitor_index].paused_ = true;
    LOG_INFO("Pausing session {}", monitor_index);
  }
  return 0;
}

int ScreenCapturerWgc::Resume(int monitor_index) {
  if (monitor_index >= sessions_.size() || monitor_index < 0) {
    LOG_ERROR("Invalid session index: {}", monitor_index);
    return -1;
  }

  if (sessions_[monitor_index].paused_) {
    sessions_[monitor_index].session_->Resume();
    sessions_[monitor_index].paused_ = false;
    LOG_INFO("Resuming session {}", monitor_index);
  }
  return 0;
}

int ScreenCapturerWgc::Stop() {
  running_ = false;

  for (int i = 0; i < sessions_.size(); i++) {
    if (sessions_[i].running_) {
      sessions_[i].session_->Stop();
      sessions_[i].running_ = false;
    }
  }

  return 0;
}

int ScreenCapturerWgc::SwitchTo(int monitor_index) {
  if (monitor_index_ == monitor_index) {
    LOG_INFO("Already on monitor {}:{}", monitor_index_ + 1,
             display_info_list_[monitor_index_].name);
    return 0;
  }

  if (monitor_index >= display_info_list_.size()) {
    LOG_ERROR("Invalid monitor index: {}", monitor_index);
    return -1;
  }

  if (!sessions_[monitor_index].inited_) {
    LOG_ERROR("Monitor {} not inited", monitor_index);
    return -1;
  }

  Pause(monitor_index_);

  monitor_index_ = monitor_index;
  LOG_INFO("Switching to monitor {}:{}", monitor_index_,
           display_info_list_[monitor_index_].name);

  Resume(monitor_index);

  return 0;
}

int ScreenCapturerWgc::ResetToInitialMonitor() {
  if (display_info_list_.empty()) return -1;
  if (initial_monitor_index_ < 0 ||
      initial_monitor_index_ >= static_cast<int>(display_info_list_.size())) {
    return -1;
  }
  if (monitor_index_ == initial_monitor_index_) {
    return 0;
  }
  if (running_) {
    Pause(monitor_index_);
  }
  monitor_index_ = initial_monitor_index_;
  LOG_INFO("Reset to initial monitor {}:{}", monitor_index_,
           display_info_list_[monitor_index_].name);
  if (running_) {
    Resume(monitor_index_);
  }
  return 0;
}
void ScreenCapturerWgc::OnFrame(const WgcSession::wgc_session_frame& frame,
                                int id) {
  if (!running_ || !on_data_) {
    return;
  }

  std::lock_guard<std::mutex> lock(frame_mutex_);

  if (on_data_) {
    if (id < 0 || id >= static_cast<int>(display_info_list_.size())) {
      LOG_ERROR("WGC OnFrame invalid display index: {}", id);
      return;
    }

    if (!frame.data || frame.row_pitch == 0) {
      LOG_ERROR("WGC OnFrame received invalid frame: data={}, row_pitch={}",
                (void*)frame.data, frame.row_pitch);
      return;
    }

    // calculate the maximum width that can be contained in one row according to
    // row_pitch (BGRA: 4 bytes per pixel), and take the minimum with logical
    // width to avoid out-of-bounds access.
    unsigned int max_width_by_pitch = frame.row_pitch / 4u;
    int logical_width = static_cast<int>(
        frame.width < max_width_by_pitch ? frame.width : max_width_by_pitch);

    // libyuv::ARGBToNV12 requires even width/height
    int even_width = logical_width & ~1;
    int even_height = static_cast<int>(frame.height) & ~1;

    if (even_width <= 0 || even_height <= 0) {
      LOG_ERROR(
          "WGC OnFrame invalid frame size after adjust: width={} "
          "(frame.width={}, max_by_pitch={}), height={}",
          logical_width, frame.width, max_width_by_pitch, frame.height);
      return;
    }

    int nv12_size = even_width * even_height * 3 / 2;

    if (!nv12_frame_ || nv12_width_ != even_width ||
        nv12_height_ != even_height) {
      delete[] nv12_frame_;
      nv12_frame_ = new unsigned char[nv12_size];
      nv12_width_ = even_width;
      nv12_height_ = even_height;
    }

    libyuv::ARGBToNV12((const uint8_t*)frame.data,
                       static_cast<int>(frame.row_pitch), (uint8_t*)nv12_frame_,
                       even_width,
                       (uint8_t*)(nv12_frame_ + even_width * even_height),
                       even_width, even_width, even_height);

    on_data_(nv12_frame_, nv12_size, even_width, even_height,
             display_info_list_[id].name.c_str());
  }
}

void ScreenCapturerWgc::CleanUp() {
  if (inited_) {
    for (auto& session : sessions_) {
      if (session.session_) {
        session.session_->Stop();
      }
    }
    sessions_.clear();
  }
}
}  // namespace crossdesk