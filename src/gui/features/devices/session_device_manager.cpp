#include "features/devices/session_device_manager.h"

#include <chrono>
#include <cstdlib>

#include "platform.h"
#include "rd_log.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {
namespace {

constexpr uint64_t kCaptureResumeKeyFrameGapMs = 500;

} // namespace

SessionDeviceManager::SessionDeviceManager(GuiRuntime &owner) : owner_(owner) {}

void SessionDeviceManager::Initialize() {
  InitializeAudioOutput();
  screen_capturer_factory_ = new ScreenCapturerFactory();
  speaker_capturer_factory_ = new SpeakerCapturerFactory();
  device_controller_factory_ = new DeviceControllerFactory();
  keyboard_capturer_ =
      static_cast<KeyboardCapturer *>(device_controller_factory_->Create(
          DeviceControllerFactory::Device::Keyboard));
}

int SessionDeviceManager::InitializeScreenCapturer() {
#ifdef __APPLE__
  if (!owner_.EnsureMacScreenRecordingPermission()) {
    return -1;
  }
#endif

  if (!screen_capturer_) {
    screen_capturer_ =
        static_cast<ScreenCapturer *>(screen_capturer_factory_->Create());
  }

  last_frame_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
  const int fps = owner_.config_center_->GetVideoFrameRate() ==
                          ConfigCenter::VIDEO_FRAME_RATE::FPS_30
                      ? 30
                      : 60;
  LOG_INFO("Init screen capturer with {} fps", fps);

  const int init_ret = screen_capturer_->Init(
      fps, [this, fps](unsigned char *data, int size, int width, int height,
                       const char *display_name) {
        const auto now_time = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        const auto duration = now_time - last_frame_time_;
        if (duration * fps < 1000) {
          return;
        }

        const std::string stream_id = display_name ? display_name : "";
        const bool resumed_after_gap =
            last_frame_time_ != 0 && duration >= kCaptureResumeKeyFrameGapMs;
        const bool stream_changed = !last_video_frame_stream_id_.empty() &&
                                    last_video_frame_stream_id_ != stream_id;
        if (resumed_after_gap || stream_changed) {
          if (RequestVideoKeyFrame(owner_.peer_, stream_id.c_str()) == 0) {
            LOG_INFO("Request video key frame before sending captured frame, "
                     "stream='{}', gap_ms={}, stream_changed={}",
                     stream_id, duration, stream_changed);
          }
        }

        XVideoFrame frame{};
        frame.data = reinterpret_cast<const char *>(data);
        frame.size = size;
        frame.width = width;
        frame.height = height;
        frame.captured_timestamp = GetSystemTimeMicros(owner_.peer_);
        SendVideoFrame(owner_.peer_, &frame, stream_id.c_str());
        last_video_frame_stream_id_ = stream_id;
        last_frame_time_ = now_time;
      });

  if (init_ret == 0) {
    LOG_INFO("Init screen capturer success");
    const auto latest_display_info = screen_capturer_->GetDisplayInfoList();
    if (!latest_display_info.empty()) {
      display_info_list_ = latest_display_info;
    }
    return 0;
  }

  LOG_ERROR("Init screen capturer failed");
  screen_capturer_->Destroy();
  delete screen_capturer_;
  screen_capturer_ = nullptr;
  return -1;
}

int SessionDeviceManager::StartScreenCapturer() {
#ifdef __APPLE__
  if (!owner_.EnsureMacScreenRecordingPermission()) {
    return -1;
  }
#endif

  if (!screen_capturer_) {
    LOG_INFO("Screen capturer instance missing, recreating before start");
    if (InitializeScreenCapturer() != 0) {
      LOG_ERROR("Recreate screen capturer failed");
      return -1;
    }
  }

  LOG_INFO("Start screen capturer, show cursor: {}", owner_.show_cursor_);
  const int ret = screen_capturer_->Start(owner_.show_cursor_);
  if (ret != 0) {
    LOG_ERROR("Start screen capturer failed: {}", ret);
  }
  return ret;
}

int SessionDeviceManager::StopScreenCapturer() {
  if (screen_capturer_) {
    LOG_INFO("Stop screen capturer");
    screen_capturer_->Stop();
  }
  return 0;
}

int SessionDeviceManager::StartSpeakerCapturer() {
  if (!speaker_capturer_) {
    speaker_capturer_ =
        static_cast<SpeakerCapturer *>(speaker_capturer_factory_->Create());
    const int init_ret = speaker_capturer_->Init(
        [this](unsigned char *data, size_t size, const char *audio_name) {
          SendAudioFrame(owner_.peer_, reinterpret_cast<const char *>(data),
                         size, owner_.audio_label_.c_str());
        });

    if (init_ret != 0) {
      speaker_capturer_->Destroy();
      delete speaker_capturer_;
      speaker_capturer_ = nullptr;
    }
  }

  if (!speaker_capturer_) {
    return -1;
  }

  const int ret = speaker_capturer_->Start();
  if (ret != 0) {
    LOG_ERROR("Start speaker capturer failed: {}", ret);
    return ret;
  }
  owner_.start_speaker_capturer_ = true;
  return 0;
}

int SessionDeviceManager::StopSpeakerCapturer() {
  if (speaker_capturer_) {
    speaker_capturer_->Stop();
    owner_.start_speaker_capturer_ = false;
  }
  return 0;
}

int SessionDeviceManager::StartMouseController() {
#ifdef __APPLE__
  if (!owner_.EnsureMacAccessibilityPermission()) {
    return -1;
  }
#endif

  if (!device_controller_factory_) {
    LOG_INFO("Device controller factory is nullptr");
    return -1;
  }

#if defined(__linux__) && !defined(__APPLE__)
  if (IsWaylandSession()) {
    if (!screen_capturer_) {
      return 1;
    }
    const auto latest_display_info = screen_capturer_->GetDisplayInfoList();
    if (latest_display_info.empty() ||
        latest_display_info[0].handle == nullptr) {
      return 1;
    }
  }

  if (screen_capturer_) {
    const auto latest_display_info = screen_capturer_->GetDisplayInfoList();
    if (!latest_display_info.empty()) {
      display_info_list_ = latest_display_info;
    }
  }
#endif

  mouse_controller_ =
      static_cast<MouseController *>(device_controller_factory_->Create(
          DeviceControllerFactory::Device::Mouse));
  if (!mouse_controller_) {
    LOG_ERROR("Create mouse controller failed");
    return -1;
  }

  const int init_ret = mouse_controller_->Init(display_info_list_);
  if (init_ret != 0) {
    LOG_INFO("Destroy mouse controller");
    mouse_controller_->Destroy();
    delete mouse_controller_;
    mouse_controller_ = nullptr;
  }
  return init_ret;
}

int SessionDeviceManager::StopMouseController() {
  if (mouse_controller_) {
    mouse_controller_->Destroy();
    delete mouse_controller_;
    mouse_controller_ = nullptr;
  }
  return 0;
}

int SessionDeviceManager::StartKeyboardCapturer() {
  owner_.keyboard_capturer_uses_sdl_events_ = false;

#ifdef __APPLE__
  if (!owner_.EnsureMacAccessibilityPermission()) {
    owner_.keyboard_capturer_uses_sdl_events_ = true;
    return 0;
  }
#endif

#if defined(__linux__) && !defined(__APPLE__)
  if (IsWaylandSession()) {
    owner_.keyboard_capturer_uses_sdl_events_ = true;
    LOG_INFO("Start keyboard capturer with SDL Wayland backend");
    return 0;
  }
#endif

  if (!keyboard_capturer_) {
    owner_.keyboard_capturer_uses_sdl_events_ = true;
    LOG_WARN(
        "keyboard capturer is nullptr, falling back to SDL keyboard events");
    return 0;
  }

  const int hook_ret = keyboard_capturer_->Hook(
      [](int key_code, bool is_down, uint32_t scan_code, bool extended,
         void *user_ptr) {
        if (user_ptr) {
          auto *runtime = static_cast<GuiRuntime *>(user_ptr);
          runtime->keyboard_.SendKeyCommand(key_code, is_down, scan_code,
                                           extended);
        }
      },
      &owner_);
  if (hook_ret != 0) {
    owner_.keyboard_capturer_uses_sdl_events_ = true;
    LOG_WARN(
        "Start keyboard capturer failed, falling back to SDL keyboard events");
  } else {
    LOG_INFO("Start keyboard capturer with native hook");
  }
  return 0;
}

int SessionDeviceManager::StopKeyboardCapturer() {
  if (owner_.keyboard_capturer_uses_sdl_events_) {
    owner_.keyboard_capturer_uses_sdl_events_ = false;
    LOG_INFO("Stop keyboard capturer with SDL keyboard backend");
    return 0;
  }

  if (keyboard_capturer_) {
    keyboard_capturer_->Unhook();
    LOG_INFO("Stop keyboard capturer");
  }
  return 0;
}

int SessionDeviceManager::InitializeAudioOutput() {
  SDL_AudioSpec desired_out{};
  desired_out.freq = 48000;
  desired_out.format = SDL_AUDIO_S16;
  desired_out.channels = 1;

  auto open_stream = [&]() {
    output_stream_ = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_out, nullptr, nullptr);
    return output_stream_ != nullptr;
  };

  if (!open_stream()) {
#if defined(__linux__) && !defined(__APPLE__)
    LOG_WARN("Failed to open output stream with driver [{}]: {}",
             getenv("SDL_AUDIODRIVER") ? getenv("SDL_AUDIODRIVER")
                                       : "(default)",
             SDL_GetError());

    setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
      LOG_ERROR("Failed to reinitialize SDL audio with dummy driver: {}",
                SDL_GetError());
      return -1;
    }
    if (!open_stream()) {
      LOG_ERROR("Failed to open output stream with dummy driver: {}",
                SDL_GetError());
      return -1;
    }
    LOG_WARN("Audio output disabled, using SDL dummy audio driver");
#else
    LOG_ERROR("Failed to open output stream: {}", SDL_GetError());
    return -1;
#endif
  }

  SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(output_stream_));
  return 0;
}

int SessionDeviceManager::DestroyAudioOutput() {
  if (output_stream_) {
    SDL_CloseAudioDevice(SDL_GetAudioStreamDevice(output_stream_));
    SDL_DestroyAudioStream(output_stream_);
    output_stream_ = nullptr;
  }
  return 0;
}

void SessionDeviceManager::PushAudio(const char *data, size_t size) {
  if (!output_stream_) {
    return;
  }
  const int pushed = SDL_PutAudioStreamData(
      output_stream_, reinterpret_cast<const Uint8 *>(data),
      static_cast<int>(size));
  if (pushed < 0) {
    LOG_ERROR("Failed to push audio data: {}", SDL_GetError());
  }
}

void SessionDeviceManager::UpdateInteractions() {
#if defined(__linux__) && !defined(__APPLE__)
  const bool is_wayland_session = IsWaylandSession();
  const bool stop_wayland_mouse_before_screen =
      is_wayland_session && !owner_.start_screen_capturer_ &&
      owner_.screen_capturer_is_started_ && !owner_.start_mouse_controller_ &&
      owner_.mouse_controller_is_started_;
  if (stop_wayland_mouse_before_screen) {
    LOG_INFO("Stopping Wayland mouse controller before screen capturer to "
             "cleanly release the shared portal session");
    StopMouseController();
    owner_.mouse_controller_is_started_ = false;
  }
#endif

  if (owner_.start_screen_capturer_ && !owner_.screen_capturer_is_started_) {
    if (StartScreenCapturer() == 0) {
      owner_.screen_capturer_is_started_ = true;
    }
  } else if (!owner_.start_screen_capturer_ &&
             owner_.screen_capturer_is_started_) {
    StopScreenCapturer();
    owner_.screen_capturer_is_started_ = false;
  }

  if (owner_.start_speaker_capturer_ && !owner_.speaker_capturer_is_started_) {
    if (StartSpeakerCapturer() == 0) {
      owner_.speaker_capturer_is_started_ = true;
    }
  } else if (!owner_.start_speaker_capturer_ &&
             owner_.speaker_capturer_is_started_) {
    StopSpeakerCapturer();
    owner_.speaker_capturer_is_started_ = false;
  }

  if (owner_.start_mouse_controller_ && !owner_.mouse_controller_is_started_) {
    if (StartMouseController() == 0) {
      owner_.mouse_controller_is_started_ = true;
    }
  } else if (!owner_.start_mouse_controller_ &&
             owner_.mouse_controller_is_started_) {
    StopMouseController();
    owner_.mouse_controller_is_started_ = false;
  }

#if defined(__linux__) && !defined(__APPLE__)
  if (owner_.screen_capturer_is_started_ && screen_capturer_ &&
      mouse_controller_) {
    const auto latest_display_info = screen_capturer_->GetDisplayInfoList();
    if (!latest_display_info.empty()) {
      display_info_list_ = latest_display_info;
      mouse_controller_->UpdateDisplayInfoList(display_info_list_);
    }
  }
#endif

  if (owner_.start_keyboard_capturer_ && owner_.focus_on_stream_window_) {
    if (!owner_.keyboard_capturer_is_started_ && StartKeyboardCapturer() == 0) {
      owner_.keyboard_capturer_is_started_ = true;
    }
    if (owner_.keyboard_capturer_is_started_) {
      owner_.keyboard_.SendHeartbeat(false);
    }
  } else if (owner_.keyboard_capturer_is_started_) {
    owner_.keyboard_.ForceReleasePressedKeys();
    StopKeyboardCapturer();
    owner_.keyboard_capturer_is_started_ = false;
  }

  owner_.keyboard_.CheckRemoteTimeouts();
}

bool SessionDeviceManager::SendKeyboardCommand(int key_code, bool is_down,
                                               uint32_t scan_code,
                                               bool extended) {
  return keyboard_capturer_ && keyboard_capturer_->SendKeyboardCommand(
                                   key_code, is_down, scan_code, extended) == 0;
}

void SessionDeviceManager::SendMouseCommand(const RemoteAction &action,
                                            int selected_display) {
  if (mouse_controller_) {
    mouse_controller_->SendMouseCommand(action, selected_display);
  }
}

int SessionDeviceManager::SwitchDisplay(int display_id) {
  return screen_capturer_ ? screen_capturer_->SwitchTo(display_id) : -1;
}

void SessionDeviceManager::ResetToInitialDisplay() {
  if (screen_capturer_) {
    screen_capturer_->ResetToInitialMonitor();
  }
}

const std::vector<DisplayInfo> &
SessionDeviceManager::display_info_list() const {
  return display_info_list_;
}

void SessionDeviceManager::DestroyDevices() {
  if (mouse_controller_) {
    mouse_controller_->Destroy();
    delete mouse_controller_;
    mouse_controller_ = nullptr;
  }
  if (screen_capturer_) {
    screen_capturer_->Destroy();
    delete screen_capturer_;
    screen_capturer_ = nullptr;
  }
  if (speaker_capturer_) {
    speaker_capturer_->Destroy();
    delete speaker_capturer_;
    speaker_capturer_ = nullptr;
  }
  if (keyboard_capturer_) {
    delete keyboard_capturer_;
    keyboard_capturer_ = nullptr;
  }
}

void SessionDeviceManager::DestroyFactories() {
  delete screen_capturer_factory_;
  screen_capturer_factory_ = nullptr;
  delete speaker_capturer_factory_;
  speaker_capturer_factory_ = nullptr;
  delete device_controller_factory_;
  device_controller_factory_ = nullptr;
}

} // namespace crossdesk
