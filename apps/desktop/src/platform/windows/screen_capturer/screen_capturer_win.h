/*
 * @Author: DI JUNKUN
 * @Date: 2026-02-27
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_WIN_H_
#define _SCREEN_CAPTURER_WIN_H_

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "screen_capturer.h"
#include "screen_capture_method.h"
#include "privacy_controller.h"
// Relative: platform include directories stay private to the capture target.
#include "../virtual_display/virtual_display_provisioner.h"

namespace crossdesk {

class CapturedNv12FramePool;

class ScreenCapturerWin : public ScreenCapturer {
 public:
  ScreenCapturerWin();
  ~ScreenCapturerWin();

 public:
  int Init(const int fps, cb_desktop_data cb) override;
  int Destroy() override;
  int Start(bool show_cursor) override;
  int Stop() override;
  // Applied on the capture-management thread, including secure-desktop capture.
  void SetCursorCapture(bool enabled) {
    show_cursor_.store(enabled, std::memory_order_relaxed);
  }

  int Pause(int monitor_index) override;
  int Resume(int monitor_index) override;

  int SwitchTo(int monitor_index) override;
  int ResetToInitialMonitor() override;

  std::vector<DisplayInfo> GetDisplayInfoList() override;
  int GetCurrentMonitorIndex() const override;
  // True while this session captures a usbmmidd monitor it plugged itself.
  bool IsVirtualDisplayActive() const { return virtual_display_.active(); }
  void SetPrivacyController(PrivacyController* privacy) override { privacy_ = privacy; }
  // Set before Init(), while capture is stopped.
  void SetCaptureMethod(ScreenCaptureMethod method) { capture_method_ = method; }
  // Query synchronously inside the frame callback, on the delivering thread.
  // This describes the actual frame, independently of service status polling.
  static bool CurrentFrameIsFromSecureDesktop();

 private:
  using BackendFactory = std::function<std::unique_ptr<ScreenCapturer>()>;
  std::unique_ptr<ScreenCapturer> impl_;
  // Serializes backend replacement (secure-desktop recovery runs on the
  // management thread) against the public entry points that dereference it.
  // Lock order: impl_mutex_ before alias_mutex_; never the reverse.
  std::mutex impl_mutex_;
  ScreenCaptureMethod capture_method_ = ScreenCaptureMethod::Auto;
  bool IsBackendEnabled(ScreenCaptureMethod method) const {
    return capture_method_ == ScreenCaptureMethod::Auto ||
           capture_method_ == method;
  }
  PrivacyController* privacy_ = nullptr;
  void NotifyPrivacyCapture(bool running);
  int fps_ = 60;
  cb_desktop_data cb_;
  cb_desktop_data cb_orig_;

  std::unordered_map<void*, size_t> handle_to_canonical_index_;
  std::unordered_map<std::string, std::string> stream_id_alias_;
  std::mutex alias_mutex_;
  std::vector<DisplayInfo> canonical_displays_;
  std::vector<int> backend_to_canonical_;
  std::atomic<bool> invalid_stream_id_logged_{false};
  std::atomic<bool> native_output_logged_{false};
  std::atomic<bool> native_output_error_logged_{false};
  std::atomic<bool> running_{false};
  std::atomic<ULONGLONG> last_capture_progress_tick_{0};
  std::atomic<bool> paused_{false};
  std::atomic<bool> show_cursor_{true};
  bool applied_show_cursor_ = true;
  std::atomic<bool> headless_compat_{false};
  // Session-scoped: plugged on a headless desktop after Start() returns and
  // released after Stop(), so an attached physical monitor is never shadowed
  // between sessions. Both run on virtual_display_thread_ because the helper
  // may need up to two minutes to install the driver; Start() itself begins
  // in GDI compatibility mode and the management thread adopts the virtual
  // display (switching to a GPU backend) once virtual_display_ready_ is set.
  VirtualDisplayProvisioner virtual_display_;
  std::thread virtual_display_thread_;
  std::atomic<bool> virtual_display_ready_{false};
  std::atomic<int> monitor_index_{0};
  std::atomic<bool> secure_desktop_capture_active_{false};
  std::atomic<bool> post_secure_desktop_waiting_for_frame_{false};
  std::atomic<bool> post_secure_desktop_drop_logged_{false};
  std::atomic<ULONGLONG> post_secure_desktop_started_tick_{0};
  std::thread secure_capture_thread_;
  HANDLE secure_frame_mapping_ = nullptr;
  HANDLE secure_frame_ready_event_ = nullptr;
  uint8_t* secure_frame_view_ = nullptr;
  size_t secure_frame_view_size_ = 0;
  DWORD secure_shared_session_id_ = 0xFFFFFFFF;
  int secure_shared_left_ = 0;
  int secure_shared_top_ = 0;
  int secure_shared_width_ = 0;
  int secure_shared_height_ = 0;
  int secure_shared_fps_ = 0;
  bool secure_shared_show_cursor_ = true;
  std::string secure_shared_stage_;
  std::string secure_shared_desktop_;
  bool secure_shared_capture_started_ = false;
  std::shared_ptr<CapturedNv12FramePool> native_frame_pool_;

  void BuildCanonicalFromImpl();
  void RebuildAliasesFromImpl(bool preserve_backend_slots = false);
  void RestoreMonitor(int monitor_index);
  // Backends in preference order for the current desktop; each factory is
  // invoked lazily so an unused WGC plugin is never loaded.
  std::vector<BackendFactory> PreferredBackends(bool headless) const;
  bool TryStartBackend(std::unique_ptr<ScreenCapturer> candidate,
                       int monitor_index);
  bool StartPreferredBackend(bool headless, int monitor_index);
  void ResetPostSecureDesktopState();
  // Common tail of every successful Start(): publish running state and
  // launch the management thread.
  int MarkStarted();
  void EmitCapturedFrame(unsigned char* data, int size, int width, int height,
                         const char* stream_id,
                         const MiniRtcNativeVideoFrame* native_frame = nullptr,
                         bool from_secure_desktop = false);
  void StopSecureCaptureThread();
  bool RestartCaptureBackendAfterSecureDesktop();
  void CheckCaptureProgress(ULONGLONG now);
  // Virtual display provisioning runs off the caller's thread.
  void JoinVirtualDisplayThread();
  void BeginVirtualDisplayProvisioning();
  void BeginVirtualDisplayRelease();
  // Management thread: once the worker plugged the virtual display, replace
  // the GDI compatibility backend with a GPU backend on that display.
  void MaybeAdoptVirtualDisplay();
  void ApplyCursorCaptureSetting();
  void SecureDesktopCaptureLoop();
  bool GetCurrentCaptureRegion(int* left, int* top, int* width, int* height,
                               std::string* display_name);
  bool StartSecureDesktopSharedCapture(DWORD session_id, int left, int top,
                                       int width, int height,
                                       const std::string& stage,
                                       const std::string& desktop,
                                       bool show_cursor, int fps,
                                       std::string* error_out);
  void StopSecureDesktopSharedCapture(DWORD session_id);
  bool OpenSecureDesktopSharedFrame(DWORD session_id, size_t min_size,
                                    std::string* error_out);
  bool ReadSecureDesktopSharedFrame(DWORD wait_ms,
                                    std::vector<uint8_t>* nv12_frame_out,
                                    int* width_out, int* height_out,
                                    std::string* error_out);
  void CloseSecureDesktopSharedFrame();
};
}  // namespace crossdesk
#endif
