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
#include "../service/shared_capture_configuration.h"
#include "headless_recovery.h"

namespace crossdesk {

class CapturedNv12FramePool;
class PrivacyCursorGuard;

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
  bool OwnsNativeCursor() const { return native_cursor_owned_.load(); }
  void SetNativeCursorPolicy(bool default_enabled, bool current_enabled) {
    native_cursor_default_.store(default_enabled);
    native_cursor_enabled_.store(current_enabled);
  }
  void SetPrivacyController(PrivacyController* privacy) override { privacy_ = privacy; }
  // Set before Init(), while capture is stopped.
  void SetUnattendedHost(bool enabled) { unattended_host_ = enabled; }
  // Query synchronously inside the frame callback, on the delivering thread.
  // This describes the actual frame, independently of service status polling.
  static bool CurrentFrameIsFromSecureDesktop();

 private:
  std::unique_ptr<ScreenCapturer> impl_;
  // Serializes backend replacement (secure-desktop recovery runs on the
  // management thread) against the public entry points that dereference it.
  // Lock order: impl_mutex_ before alias_mutex_; never the reverse.
  std::mutex impl_mutex_;
  bool unattended_host_ = false;
  bool PrepareUnattendedDisplay();
  void UpdateNativeCursor(ScreenCapturer* backend);
  void ReleaseNativeCursor();
  std::unique_ptr<PrivacyCursorGuard> native_cursor_guard_;
  std::atomic<bool> native_cursor_owned_{false};
  std::atomic<bool> native_cursor_transition_{false};
  std::atomic<bool> native_cursor_default_{true}, native_cursor_enabled_{true};
  ULONGLONG next_native_cursor_retry_ = 0;
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
  std::atomic<bool> backend_frame_received_{false}, backend_reset_pending_{false};
  std::atomic<bool> display_probe_requested_{false};
  ULONGLONG last_display_probe_tick_ = 0;
  ULONGLONG last_capture_retry_tick_ = 0;
  HeadlessRecovery headless_recovery_;
  std::atomic<bool> paused_{false};
  std::atomic<bool> show_cursor_{true};
  bool applied_show_cursor_ = true;
  // GUI sessions provision on a worker and release after Stop(). Machine
  // hosts prepare before signaling, revalidate before capture, and retain
  // the monitor until a physical screen returns or Destroy() runs.
  // The management thread adopts the output once virtual_display_ready_ is
  // set, trying the same ordered backends on the replacement target.
  VirtualDisplayProvisioner virtual_display_;
  std::thread virtual_display_thread_;
  std::atomic<bool> virtual_display_ready_{false};
  std::atomic<bool> virtual_display_busy_{false};
  ULONGLONG last_virtual_adoption_tick_ = 0;
  ULONGLONG physical_display_since_ = 0;
  // Protected by alias_mutex_. The virtual screen and then the returning
  // physical screen replace the same active wire slot explicitly.
  std::string replacement_display_name_;
  int replacement_display_slot_ = -1;
  std::atomic<int> monitor_index_{0};
  std::atomic<bool> secure_desktop_capture_active_{false};
  std::atomic<bool> post_secure_desktop_waiting_for_frame_{false};
  std::atomic<ULONGLONG> post_secure_desktop_started_tick_{0};
  std::thread secure_capture_thread_;
  HANDLE secure_frame_mapping_ = nullptr;
  HANDLE secure_frame_ready_event_ = nullptr;
  uint8_t* secure_frame_view_ = nullptr;
  size_t secure_frame_view_size_ = 0;
  DWORD secure_shared_session_id_ = 0xFFFFFFFF;
  SharedCaptureConfiguration secure_shared_configuration_;
  bool secure_shared_capture_started_ = false;
  bool secure_shared_start_pending_ = false;
  ULONGLONG secure_shared_last_keepalive_ = 0;
  std::shared_ptr<CapturedNv12FramePool> native_frame_pool_;

  void BuildCanonicalFromImpl();
  void RebuildAliasesFromImpl(bool preserve_backend_slots = false);
  void RestoreMonitor(int monitor_index);
  bool TryStartBackend(std::unique_ptr<ScreenCapturer> candidate,
                       int monitor_index);
  bool StartPreferredBackend(int monitor_index,
      ScreenCaptureMethod after = ScreenCaptureMethod::Auto);
  void ResetPostSecureDesktopState(bool secure = false);
  void EmitCapturedFrame(unsigned char* data, int size, int width, int height,
                         const char* stream_id,
                         const MiniRtcNativeVideoFrame* native_frame = nullptr,
                         bool from_secure_desktop = false);
  void StopSecureCaptureThread();
  bool RestartCaptureBackendAfterSecureDesktop();
  void CheckCaptureProgress(ULONGLONG now);
  void CheckDisplayPresence(ULONGLONG now);
  void RestorePhysicalDisplay();
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
