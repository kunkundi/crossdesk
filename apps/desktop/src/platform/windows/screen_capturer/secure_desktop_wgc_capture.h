/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SECURE_DESKTOP_WGC_CAPTURE_H_
#define _SECURE_DESKTOP_WGC_CAPTURE_H_

#include <display_stream_id.h>
#include <dxgi.h>
#include <cstring>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "wgc_plugin_capturer.h"
#include "../winrt_apartment.h"
#include "../windows_thread_dpi.h"

namespace crossdesk {

// The helper's producer consumes the latest cropped NV12 image. The plugin
// remains responsible for WGC/WinRT and cursor exclusion, as in normal capture.
class SecureDesktopWgcCapture {
 public:
  ~SecureDesktopWgcCapture() {
    if (stop_) SetEvent(stop_);
    if (worker_.joinable()) worker_.join();
    // Close from the caller after the worker's windows have been destroyed.
    if (desktop_) CloseDesktop(desktop_);
    if (stop_) CloseHandle(stop_);
  }

  int Initialize(int left, int top, int width, int height, int fps, bool cursor) {
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        ((width | height) & 1) || worker_.joinable()) return -1;
    DWORD bytes = 0;
    const auto current = GetThreadDesktop(GetCurrentThreadId());
    GetUserObjectInformationW(current, UOI_NAME, nullptr, 0, &bytes);
    if (!bytes) return -1;
    std::vector<wchar_t> name(bytes / sizeof(wchar_t) + 1);
    if (!GetUserObjectInformationW(current, UOI_NAME, name.data(), bytes, &bytes)) return -1;
    desktop_ = OpenDesktopW(name.data(), 0, FALSE,
        DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_CREATEWINDOW);
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!desktop_ || !stop_) return -1;
    std::promise<int> ready;
    auto initialized = ready.get_future();
    // WinRT may create thread-owned windows. Keep those off the producer,
    // which must remain able to bind another input desktop after lock/unlock.
    worker_ = std::thread([this, left, top, width, height, fps, cursor,
                           ready = std::move(ready)]() mutable {
      if (!SetThreadDesktop(desktop_)) { ready.set_value(-1); return; }
      ScopedWindowsPhysicalCoordinates coordinates;
      ScopedWinRtApartment apartment;
      int result = -1;
      try {
        if (apartment.ready()) result = InitializeWorker(left, top, width, height, fps, cursor);
      } catch (...) { result = -1; }
      ready.set_value(result);
      if (result == 0) {
        while (MsgWaitForMultipleObjects(1, &stop_, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1) {
          MSG message{};
          while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
          }
        }
      }
      // Drain callbacks before either the apartment or frame storage goes away.
      if (capture_) { capture_->Stop(); capture_->Destroy(); capture_.reset(); }
    });
    return initialized.get();
  }

 private:
  int InitializeWorker(int left, int top, int width, int height, int fps, bool cursor) {
    capture_ = WgcPluginCapturer::Create();
    if (!capture_) return -1;
    width_ = width;
    height_ = height;
    const int initialized = capture_->Init(fps, [this](unsigned char* data,
        int size, int width, int height, const char* stream, const MiniRtcNativeVideoFrame*) {
      if (size == ScreenCapturer::kBackendReset || size == ScreenCapturer::kDisplayTopologyChanged) {
        std::lock_guard lock(mutex_);
        invalidated_ = true;
        return;
      }
      if (!data || size <= 0 || !stream || stream_ != stream) return;
      std::lock_guard lock(mutex_);
      if (x_ < 0 || y_ < 0 || width <= 0 || height <= 0 ||
          x_ + width_ > width || y_ + height_ > height ||
          static_cast<size_t>(size) < static_cast<size_t>(width) * height * 3 / 2) {
        invalidated_ = true;
        return;
      }
      const size_t luma = static_cast<size_t>(width_) * height_;
      pending_.resize(luma * 3 / 2);
      for (int row = 0; row < height_; ++row)
        std::memcpy(pending_.data() + static_cast<size_t>(row) * width_,
            data + static_cast<size_t>(y_ + row) * width + x_, width_);
      const auto* uv = data + static_cast<size_t>(width) * height;
      for (int row = 0; row < height_ / 2; ++row)
        std::memcpy(pending_.data() + luma + static_cast<size_t>(row) * width_,
            uv + static_cast<size_t>(y_ / 2 + row) * width + x_, width_);
      pending_cursor_ = current_frame_has_cursor;
      invalidated_ = false;
      ++sequence_;
    });
    if (initialized != 0) return initialized;
    const auto displays = capture_->GetDisplayInfoList();
    for (size_t index = 0; index < displays.size(); ++index) {
      const auto& display = displays[index];
      if (left < display.left || top < display.top ||
          static_cast<int64_t>(left) + width > display.right ||
          static_cast<int64_t>(top) + height > display.bottom) continue;
      x_ = left - display.left;
      y_ = top - display.top;
      // The IPC regions are normally whole monitors. A region with an odd
      // chroma offset, or spanning outputs, falls through to GDI.
      if ((x_ | y_ | width | height) & 1) return -1;
      stream_ = MakeDisplayStreamId(index);
      const int selected = capture_->SwitchTo(static_cast<int>(index));
      return selected == 0 ? capture_->Start(cursor) : selected;
    }
    return -1;
  }

 public:
  HRESULT Capture() {
    std::lock_guard lock(mutex_);
    updated_ = false;
    if (invalidated_) return DXGI_ERROR_ACCESS_LOST;
    if (!sequence_) return S_FALSE;
    // A static desktop need not produce another WGC event. Retain the valid
    // image until a new frame or an explicit reset, just like DXGI timeouts.
    if (consumed_ != sequence_) {
      pixels_.swap(pending_);
      cursor_ = pending_cursor_;
      consumed_ = sequence_;
      updated_ = true;
    }
    return S_OK;
  }
  const std::vector<uint8_t>& pixels() const { return pixels_; }
  bool updated() const { return updated_; }
  bool CursorEmbedded() const { return cursor_; }

 private:
  std::unique_ptr<ScreenCapturer> capture_;
  HDESK desktop_ = nullptr;
  HANDLE stop_ = nullptr;
  std::thread worker_;
  std::mutex mutex_;
  int width_ = 0, height_ = 0, x_ = 0, y_ = 0;
  std::string stream_;
  std::vector<uint8_t> pending_, pixels_;
  uint64_t sequence_ = 0, consumed_ = 0;
  bool invalidated_ = false;
  bool pending_cursor_ = false, cursor_ = false, updated_ = false;
};

}  // namespace crossdesk

#endif