#ifndef CROSSDESK_GUI_SESSION_DEVICE_MANAGER_H_
#define CROSSDESK_GUI_SESSION_DEVICE_MANAGER_H_

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "device_controller.h"
#include "device_controller_factory.h"
#include "display_info.h"
#include "screen_capturer_factory.h"
#include "speaker_capturer_factory.h"

namespace crossdesk {

class GuiRuntime;

// Owns media capture/playback and remote input devices for the active GUI
// session. GuiRuntime supplies application intent; this class handles device
// creation, updates and teardown.
class SessionDeviceManager {
public:
  explicit SessionDeviceManager(GuiRuntime &owner);

  void Initialize();
  void UpdateInteractions();
  void DestroyDevices();
  void DestroyFactories();

  int InitializeScreenCapturer();
  int StartScreenCapturer();
  int StopScreenCapturer();
  int StartSpeakerCapturer();
  int StopSpeakerCapturer();
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

  GuiRuntime &owner_;
  SDL_AudioStream *output_stream_ = nullptr;
  ScreenCapturerFactory *screen_capturer_factory_ = nullptr;
  ScreenCapturer *screen_capturer_ = nullptr;
  SpeakerCapturerFactory *speaker_capturer_factory_ = nullptr;
  SpeakerCapturer *speaker_capturer_ = nullptr;
  DeviceControllerFactory *device_controller_factory_ = nullptr;
  MouseController *mouse_controller_ = nullptr;
  KeyboardCapturer *keyboard_capturer_ = nullptr;
  std::vector<DisplayInfo> display_info_list_;
  size_t registered_display_stream_count_ = 0;
  std::deque<CapturedKeyboardInput> captured_keyboard_inputs_;
  std::mutex captured_keyboard_inputs_mutex_;
  uint64_t last_frame_time_ = 0;
  std::string last_video_frame_stream_id_;
  bool invalid_video_stream_id_logged_ = false;
};

} // namespace crossdesk

#endif // CROSSDESK_GUI_SESSION_DEVICE_MANAGER_H_
