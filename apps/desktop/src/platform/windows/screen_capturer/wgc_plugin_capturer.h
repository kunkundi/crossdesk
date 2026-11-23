/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WGC_PLUGIN_CAPTURER_H_
#define _WGC_PLUGIN_CAPTURER_H_

#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <memory>
#include <utility>
#include "captured_cursor_state.h"
#include "wgc_plugin_api.h"

namespace crossdesk {
class WgcPluginCapturer final : public ScreenCapturer {
 public:
  using CreateFn = ScreenCapturer* (*)();
  using DestroyFn = void (*)(ScreenCapturer*);
  using SetCursorFn = decltype(&CrossDeskSetWgcCursorCapture);
  using FrameCursorFn = decltype(&CrossDeskWgcFrameCapturesCursor);

  static std::unique_ptr<ScreenCapturer> Create() {
    wchar_t module_path[MAX_PATH] = {0};
    const DWORD len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
      return nullptr;
    }
    const auto plugin_path =
        std::filesystem::path(module_path).parent_path() / L"wgc_plugin.dll";

    HMODULE module = LoadLibraryW(plugin_path.c_str());
    if (!module) {
      return nullptr;
    }

    auto create_fn = reinterpret_cast<CreateFn>(
        GetProcAddress(module, "CrossDeskCreateWgcCapturer"));
    auto destroy_fn = reinterpret_cast<DestroyFn>(
        GetProcAddress(module, "CrossDeskDestroyWgcCapturer"));
    if (!create_fn || !destroy_fn) {
      FreeLibrary(module);
      return nullptr;
    }

    ScreenCapturer* impl = create_fn();
    if (!impl) {
      FreeLibrary(module);
      return nullptr;
    }

    return std::unique_ptr<ScreenCapturer>(
        new WgcPluginCapturer(module, impl, destroy_fn));
  }

  ~WgcPluginCapturer() override {
    destroy_fn_(impl_);
    FreeLibrary(module_);
  }

  int Init(const int fps, cb_desktop_data cb) override {
    return impl_->Init(fps, [this, cb = std::move(cb)](
        unsigned char* data, int size, int width, int height,
        const char* stream_id, const MiniRtcNativeVideoFrame* native_frame) {
      bool embedded = false;
      if (frame_cursor_fn_) {
        embedded = frame_cursor_fn_();
      } else if (legacy_capture_cursor_.load(std::memory_order_relaxed)) {
        CURSORINFO info{};
        info.cbSize = sizeof(info);
        embedded = GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING) != 0;
      }
      // The plugin has its own globals. Bridge only this callback's metadata
      // into the executable before the wrapper accepts/publishes the frame.
      CapturedCursorFrameScope cursor_scope(embedded);
      cb(data, size, width, height, stream_id, native_frame);
    });
  }
  int Destroy() override { return impl_->Destroy(); }
  int Start(bool show_cursor) override {
    if (!frame_cursor_fn_) {
      legacy_capture_cursor_.store(show_cursor, std::memory_order_relaxed);
    }
    return impl_->Start(show_cursor);
  }
  int SetCursorCapture(bool enabled) {
    if (set_cursor_fn_) return set_cursor_fn_(impl_, enabled);
    // Older plugins only apply the cursor setting at startup.
    const int ret = impl_->Stop();
    return ret == 0 ? Start(enabled) : ret;
  }
  int Stop() override { return impl_->Stop(); }
  int Pause(int monitor_index) override {
    return impl_->Pause(monitor_index);
  }
  int Resume(int monitor_index) override {
    return impl_->Resume(monitor_index);
  }
  std::vector<DisplayInfo> GetDisplayInfoList() override {
    return impl_->GetDisplayInfoList();
  }
  int SwitchTo(int monitor_index) override {
    return impl_->SwitchTo(monitor_index);
  }
  int ResetToInitialMonitor() override {
    return impl_->ResetToInitialMonitor();
  }

 private:
  WgcPluginCapturer(HMODULE module, ScreenCapturer* impl, DestroyFn destroy_fn)
      : module_(module), impl_(impl), destroy_fn_(destroy_fn) {
    set_cursor_fn_ = reinterpret_cast<SetCursorFn>(
        GetProcAddress(module, "CrossDeskSetWgcCursorCapture"));
    frame_cursor_fn_ = reinterpret_cast<FrameCursorFn>(
        GetProcAddress(module, "CrossDeskWgcFrameCapturesCursor"));
    // Use the legacy restart path unless both parts of the extension exist.
    if (!frame_cursor_fn_) set_cursor_fn_ = nullptr;
  }

  // Create() validates these before constructing the wrapper.
  HMODULE module_;
  ScreenCapturer* impl_;
  DestroyFn destroy_fn_;
  SetCursorFn set_cursor_fn_ = nullptr;
  FrameCursorFn frame_cursor_fn_ = nullptr;
  std::atomic<bool> legacy_capture_cursor_{false};
};

}  // namespace crossdesk

#endif
