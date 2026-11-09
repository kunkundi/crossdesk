/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SESSION_DEVICE_MANAGER_H_
#define _SESSION_DEVICE_MANAGER_H_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <remote_action.h>

#include "device_controller.h"
#include "display_info.h"
#include "features/input/captured_keyboard_queue.h"
#include "features/input/remote_mouse_buttons.h"
#include "screen_sharing_power.h"
#include "speaker_capture_controller.h"

struct SDL_AudioStream;

namespace crossdesk {

class GuiRuntime;
class DeviceControllerFactory;
class ScreenCapturerFactory;
class ScreenCapturer;

// Owns media capture/playback and remote input devices for the active GUI
// session. GuiRuntime supplies application intent; this class handles device
// creation, updates and teardown.
class SessionDeviceManager {
public:
  explicit SessionDeviceManager(GuiRuntime &owner);
  ~SessionDeviceManager();

  void Initialize();
  void UpdateInteractions();
  // Call after changing the controller map. UpdateInteractions consumes this
  // notification on the UI thread before reading the map under its mutex.
  void OnControllerConnectionsChanged() {
    cursor_capture_dirty_.store(true, std::memory_order_relaxed);
  }
  void DestroyDevices();
  void DestroyFactories();

  int InitializeScreenCapturer();
  int EnsureCaptureReady();
  int StartScreenCapturer();
  int StopScreenCapturer();
  bool ReleaseRemoteMouseButtons();
  bool ReleaseRemoteMouseButtons(const std::string& remote_id,
                                 unsigned mask = 7);
  template <typename Send>
  InputOutcome ApplyRemoteMouse(const std::string& remote_id, MouseFlag flag,
                                 Send send) {
    return remote_mouse_buttons_.Apply(remote_id, flag, std::move(send));
  }
#ifdef _WIN32
  bool OwnsNativeCursor() const;
  void SetNativeCursorPolicy(bool default_enabled, bool current_enabled);
  std::string SendServiceMouseCommand(int x, int y, int wheel, int flag);
#endif
  // Requests are asynchronous; Stop drains/revokes audio delivery before
  // returning so the connection peer can be destroyed safely.
  void StartSpeakerCapturer();
  void StopSpeakerCapturer();
  int StartMouseController();
  // Returns 1 while background releases are pending. force is only used
  // during final destruction after the input dispatcher has been stopped.
  int StopMouseController(bool force = false);
  int StartKeyboardCapturer();
  int StopKeyboardCapturer();

  int DestroyAudioOutput();
  // Serialized by GuiRuntime::audio_delivery_mutex_. Each peer has its own
  // stream so SDL mixes simultaneous playback and muting clears only that peer.
  // Open the output on the first frame and close it after the last stream ends.
  void ClearAudio(const std::string& remote_id);
  void PushAudio(const std::string& remote_id, const char *data, size_t size);

  bool SupportsTextInput() const;
  bool SendTextCommand(const std::string& text);
  bool SendKeyboardCommand(int key_code, bool is_down, uint32_t scan_code,
                           bool extended);
  bool SendMouseCommand(const RemoteAction &action, int selected_display);
  int SwitchDisplay(int display_id);
  InputOutcome ResetToInitialDisplay();

  // Snapshot of the capture displays. The UI thread refreshes the list every
  // tick while transport threads build host information and map mouse input
  // from it, so callers get a copy taken under the lock.
  std::vector<DisplayInfo> display_info_list() const;
  // The same snapshot in the wire's host information form.
  std::vector<HostDisplay> host_display_list() const;

private:
  int InitializeAudioOutput();
  unsigned ReleaseMouseButtonMask(unsigned mask);
  void DrainCapturedKeyboardInput();
  bool ShouldSendCapturedFrame(std::chrono::steady_clock::time_point now,
                               int fps);
  bool ShouldCaptureCursor() const;
  bool HasConnectedController() const;
  void UpdateScreenSharingPower();
  bool SyncDisplayInfo();
  void SetDisplayInfoList(std::vector<DisplayInfo> displays);
  void RecordCaptureCadence(std::chrono::steady_clock::time_point now, int fps,
                            bool from_secure_desktop);

  GuiRuntime &owner_;
  uint32_t output_device_ = 0;
  std::unordered_map<std::string, SDL_AudioStream*> output_streams_;
  std::chrono::steady_clock::time_point next_audio_output_retry_{};
  ScreenCapturerFactory *screen_capturer_factory_ = nullptr;
  ScreenCapturer *screen_capturer_ = nullptr;
  ScreenSharingPower screen_sharing_power_;
  std::atomic<bool> cursor_capture_dirty_{true};
  SpeakerCaptureController speaker_capture_;
  DeviceControllerFactory *device_controller_factory_ = nullptr;
  MouseController *mouse_controller_ = nullptr;
  RemoteMouseButtons remote_mouse_buttons_;
  // Input is executed on a worker while the UI starts/stops this controller.
  std::mutex mouse_controller_mutex_;
  KeyboardCapturer *keyboard_capturer_ = nullptr;
  // Guards display_info_list_ (UI thread writes, transport threads read).
  mutable std::mutex display_info_mutex_;
  std::vector<DisplayInfo> display_info_list_;
  size_t registered_display_stream_count_ = 0;
  CapturedKeyboardQueue captured_keyboard_inputs_;
#if defined(_WIN32) || defined(__OHOS__)
  std::chrono::steady_clock::time_point next_keyboard_capture_retry_{};
#endif
#ifdef _WIN32
  std::mutex service_mouse_mutex_;
  unsigned service_mouse_buttons_ = 0;
  int service_mouse_x_ = 0, service_mouse_y_ = 0;
#endif
  std::chrono::steady_clock::time_point last_frame_time_{};
  std::chrono::steady_clock::time_point next_frame_deadline_{};
  std::chrono::steady_clock::time_point next_capture_start_retry_{};
  std::mutex capture_metrics_mutex_;
  std::chrono::steady_clock::time_point capture_metrics_started_{};
  std::chrono::steady_clock::time_point last_capture_callback_{};
  uint64_t capture_callbacks_ = 0, capture_rate_drops_ = 0;
  uint64_t capture_secure_callbacks_ = 0;
  uint64_t capture_forwarded_ = 0, capture_send_errors_ = 0;
  int64_t capture_max_gap_us_ = 0;
  int64_t capture_send_us_ = 0, capture_max_send_us_ = 0;
  std::string last_video_frame_stream_id_;
  bool invalid_video_stream_id_logged_ = false;
};

} // namespace crossdesk

#endif
