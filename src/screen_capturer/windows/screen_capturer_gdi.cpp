#include "screen_capturer_gdi.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "libyuv.h"
#include "rd_log.h"

namespace crossdesk {

namespace {
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
}  // namespace

ScreenCapturerGdi::ScreenCapturerGdi() {}
ScreenCapturerGdi::~ScreenCapturerGdi() {
  Stop();
  Destroy();
}

BOOL CALLBACK ScreenCapturerGdi::EnumMonitorProc(HMONITOR hMonitor, HDC, LPRECT,
                                                 LPARAM data) {
  auto displays = reinterpret_cast<std::vector<DisplayInfo>*>(data);
  MONITORINFOEX mi{};
  mi.cbSize = sizeof(MONITORINFOEX);
  if (GetMonitorInfo(hMonitor, &mi)) {
    std::string name = CleanDisplayName(mi.szDevice);
    bool is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) ? true : false;
    DisplayInfo info((void*)hMonitor, name, is_primary, mi.rcMonitor.left,
                     mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom);
    if (is_primary)
      displays->insert(displays->begin(), info);
    else
      displays->push_back(info);
  }
  return TRUE;
}

void ScreenCapturerGdi::EnumerateDisplays() {
  display_info_list_.clear();
  EnumDisplayMonitors(nullptr, nullptr, EnumMonitorProc,
                      (LPARAM)&display_info_list_);
}

int ScreenCapturerGdi::Init(const int fps, cb_desktop_data cb) {
  fps_ = fps;
  callback_ = cb;
  if (!callback_) {
    LOG_ERROR("GDI: callback is null");
    return -1;
  }
  EnumerateDisplays();
  if (display_info_list_.empty()) {
    LOG_ERROR("GDI: no displays found");
    return -2;
  }
  monitor_index_ = 0;
  initial_monitor_index_ = monitor_index_;
  return 0;
}

int ScreenCapturerGdi::Destroy() {
  Stop();
  if (nv12_frame_) {
    delete[] nv12_frame_;
    nv12_frame_ = nullptr;
    nv12_width_ = 0;
    nv12_height_ = 0;
  }
  return 0;
}

int ScreenCapturerGdi::Start(bool show_cursor) {
  if (running_) return 0;
  show_cursor_ = show_cursor;
  paused_ = false;
  running_ = true;
  thread_ = std::thread([this]() { CaptureLoop(); });
  return 0;
}

int ScreenCapturerGdi::Stop() {
  if (!running_) return 0;
  running_ = false;
  if (thread_.joinable()) thread_.join();
  return 0;
}

int ScreenCapturerGdi::Pause(int monitor_index) {
  paused_ = true;
  return 0;
}

int ScreenCapturerGdi::Resume(int monitor_index) {
  paused_ = false;
  return 0;
}

int ScreenCapturerGdi::SwitchTo(int monitor_index) {
  if (monitor_index < 0 || monitor_index >= (int)display_info_list_.size()) {
    LOG_ERROR("GDI: invalid monitor index {}", monitor_index);
    return -1;
  }
  monitor_index_ = monitor_index;
  LOG_INFO("GDI: switched to monitor {}:{}", monitor_index_.load(),
           display_info_list_[monitor_index_].name);
  return 0;
}

int ScreenCapturerGdi::ResetToInitialMonitor() {
  if (display_info_list_.empty()) return -1;
  int target = initial_monitor_index_;
  if (target < 0 || target >= (int)display_info_list_.size()) return -1;
  monitor_index_ = target;
  LOG_INFO("GDI: reset to initial monitor {}:{}", monitor_index_.load(),
           display_info_list_[monitor_index_].name);
  return 0;
}

void ScreenCapturerGdi::CaptureLoop() {
  int interval_ms = fps_ > 0 ? (1000 / fps_) : 16;
  HDC screen_dc = GetDC(nullptr);
  while (running_) {
    if (paused_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    if (!screen_dc) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    const auto& di = display_info_list_[monitor_index_];
    int left = di.left;
    int top = di.top;
    int width = di.width & ~1;
    int height = di.height & ~1;
    if (width <= 0 || height <= 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
      continue;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP dib =
        CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem_dc, dib);

    BitBlt(mem_dc, 0, 0, width, height, screen_dc, left, top,
           SRCCOPY | CAPTUREBLT);

    if (show_cursor_) {
      CURSORINFO ci{};
      ci.cbSize = sizeof(CURSORINFO);
      if (GetCursorInfo(&ci) && ci.flags == CURSOR_SHOWING && ci.hCursor) {
        POINT pt = ci.ptScreenPos;
        int cx = pt.x - left;
        int cy = pt.y - top;
        if (cx >= -64 && cy >= -64 && cx < width + 64 && cy < height + 64) {
          DrawIconEx(mem_dc, cx, cy, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
        }
      }
    }

    int stride_argb = width * 4;
    int nv12_size = width * height * 3 / 2;
    if (!nv12_frame_ || nv12_width_ != width || nv12_height_ != height) {
      delete[] nv12_frame_;
      nv12_frame_ = new unsigned char[nv12_size];
      nv12_width_ = width;
      nv12_height_ = height;
    }

    libyuv::ARGBToNV12(static_cast<const uint8_t*>(bits), stride_argb,
                       nv12_frame_, width, nv12_frame_ + width * height, width,
                       width, height);

    if (callback_) {
      callback_(nv12_frame_, nv12_size, width, height, di.name.c_str());
    }

    SelectObject(mem_dc, old);
    DeleteObject(dib);
    DeleteDC(mem_dc);

    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
  }
  ReleaseDC(nullptr, screen_dc);
}

}  // namespace crossdesk