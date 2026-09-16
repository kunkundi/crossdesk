/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SESSION_DEVICE_MANAGER_H_
#define _SESSION_DEVICE_MANAGER_H_


#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <remote_action.h>

#include "device_controller.h"
#include "device_controller_factory.h"
#include "display_info.h"
#include "screen_capturer_factory.h"
#include "speaker_capture_controller.h"

namespace crossdesk {

class GuiRuntime;

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
  int StartScreenCapturer();
  int StopScreenCapturer();
#ifdef _WIN32
  void ReleaseRemoteMouseButtons();
#endif
  // Requests are asynchronous; Stop drains/revokes audio delivery before
  // returning so the connection peer can be destroyed safely.
  void StartSpeakerCapturer();
  void StopSpeakerCapturer();
  int StartMouseController();
  int StopMouseController();
  int StartKeyboardCapturer();
  int StopKeyboardCapturer();

  int InitializeAudioOutput();
  int DestroyAudioOutput();
  void PushAudio(const char *data, size_t size);

  bool SendKeyboardCommand(int key_code, bool is_down, uint32_t scan_code,
                           bool extended);
  void SendMouseCommand(const RemoteAction &action, int selected_display);
  int SwitchDisplay(int display_id);
  void ResetToInitialDisplay();

  const std::vector<DisplayInfo> &display_info_list() const;

private:
  struct CapturedKeyboardInput {
    int key_code = 0;
    bool is_down = false;
    uint32_t scan_code = 0;
    bool extended = false;
  };

  void QueueCapturedKeyboardInput(int key_code, bool is_down,
                                  uint32_t scan_code, bool extended);
  void DrainCapturedKeyboardInput();
  void ClearCapturedKeyboardInput();
  bool ShouldSendCapturedFrame(std::chrono::steady_clock::time_point now,
                               int fps);
  bool ShouldCaptureCursor() const;
  void RecordCaptureCadence(std::chrono::steady_clock::time_point now, int fps,
                            bool from_secure_desktop);

  GuiRuntime &owner_;
  SDL_AudioStream *output_stream_ = nullptr;
  ScreenCapturerFactory *screen_capturer_factory_ = nullptr;
  ScreenCapturer *screen_capturer_ = nullptr;
  std::atomic<bool> cursor_capture_dirty_{true};
  SpeakerCaptureController speaker_capture_;
  DeviceControllerFactory *device_controller_factory_ = nullptr;
  MouseController *mouse_controller_ = nullptr;
  KeyboardCapturer *keyboard_capturer_ = nullptr;
  std::vector<DisplayInfo> display_info_list_;
  size_t registered_display_stream_count_ = 0;
  std::deque<CapturedKeyboardInput> captured_keyboard_inputs_;
  std::mutex captured_keyboard_inputs_mutex_;
  std::chrono::steady_clock::time_point last_frame_time_{};
  std::chrono::steady_clock::time_point next_frame_deadline_{};
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
