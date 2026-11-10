#include "features/devices/session_device_manager.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <display_stream_id.h>
#include <remote_action.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <utility>

#include "device_controller_factory.h"
#include "screen_capturer_factory.h"
#include "platform.h"
#include "rd_log.h"
#include "runtime/gui_runtime.h"
#include "speaker_capturer_factory.h"
#ifdef _WIN32
#include <nlohmann/json.hpp>
#include "platform/windows/screen_capturer/screen_capturer_win.h"
#include "service_host.h"
#endif

namespace crossdesk {
namespace {

constexpr uint64_t kCaptureResumeKeyFrameGapMs = 500;
constexpr auto kFrameDeadlineTolerance = std::chrono::milliseconds(1);
#if defined(_WIN32) || defined(__OHOS__)
constexpr auto kKeyboardCaptureRetryDelay = std::chrono::seconds(1);
#endif

} // namespace

SessionDeviceManager::SessionDeviceManager(GuiRuntime &owner)
    : owner_(owner),
      speaker_capture_(
          [] {
            return std::unique_ptr<SpeakerCapturer>(
                SpeakerCapturerFactory().Create());
          },
          [this](unsigned char *data, size_t size, const char *) {
            MiniRtcAudioFrame frame{};
            frame.data = reinterpret_cast<const char *>(data);
            frame.size = size;
            for (const auto& id : owner_.ConnectedControllers())
              owner_.WithController(
                  id, [&](MiniRtcPeerPtr* peer, const std::string& wire) {
                    frame.captured_timestamp = MiniRtcGetSystemTimeMicros(peer);
                    return PeerSendAudioFrameToPeer(peer, &frame,
                                                owner_.audio_label_.c_str(),
                                                wire.data(), wire.size());
                  });
          }) {}

SessionDeviceManager::~SessionDeviceManager() { speaker_capture_.Shutdown(); }

#ifdef _WIN32
bool SessionDeviceManager::OwnsNativeCursor() const {
  const auto* windows = dynamic_cast<ScreenCapturerWin*>(screen_capturer_);
  return windows && windows->OwnsNativeCursor();
}
void SessionDeviceManager::SetNativeCursorPolicy(bool default_enabled, bool current_enabled) {
  owner_.host_cursor_default_ = default_enabled;
  owner_.host_cursor_preference_ = current_enabled;
  if (auto* windows = dynamic_cast<ScreenCapturerWin*>(screen_capturer_))
    windows->SetNativeCursorPolicy(default_enabled, current_enabled);
}
#endif

bool SessionDeviceManager::ShouldSendCapturedFrame(
    std::chrono::steady_clock::time_point now, int fps) {
  const auto interval =
      std::chrono::nanoseconds(std::chrono::seconds(1)) / (fps > 0 ? fps : 1);
  if (next_frame_deadline_ == std::chrono::steady_clock::time_point{}) {
    next_frame_deadline_ = now;
  }
  if (now + kFrameDeadlineTolerance < next_frame_deadline_) {
    return false;
  }
  next_frame_deadline_ += interval;
  if (next_frame_deadline_ <= now) {
    next_frame_deadline_ = now + interval;
  }
  return true;
}

void SessionDeviceManager::RecordCaptureCadence(
    std::chrono::steady_clock::time_point now, int fps,
    bool from_secure_desktop) {
  std::lock_guard<std::mutex> lock(capture_metrics_mutex_);
  if (capture_metrics_started_ == std::chrono::steady_clock::time_point{})
    capture_metrics_started_ = now;
  const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              now - capture_metrics_started_)
                              .count();
  if (elapsed_us >= 2000000) {
    if (capture_forwarded_) {
      const char* source = capture_secure_callbacks_ == 0 ? "normal_desktop"
                           : capture_secure_callbacks_ == capture_callbacks_
                               ? "secure_desktop"
                               : "mixed";
      LOG_DEBUG(
          "Capture delivery: source={} normal_callbacks={} secure_callbacks={} "
          "target_fps={} input_fps={:.2f} "
          "forwarded_fps={:.2f} callbacks={} rate_dropped={} "
          "forwarded={} send_errors={} max_callback_gap_us={} "
          "send_avg_us={} send_max_us={} window_ms={}",
          source, capture_callbacks_ - capture_secure_callbacks_,
          capture_secure_callbacks_, fps,
          capture_callbacks_ * 1000000.0 / elapsed_us,
          capture_forwarded_ * 1000000.0 / elapsed_us, capture_callbacks_,
          capture_rate_drops_, capture_forwarded_, capture_send_errors_,
          capture_max_gap_us_, capture_send_us_ / capture_forwarded_,
          capture_max_send_us_, elapsed_us / 1000);
    }
    capture_metrics_started_ = now;
    capture_callbacks_ = capture_rate_drops_ = capture_forwarded_ = 0;
    capture_secure_callbacks_ = 0;
    capture_send_errors_ = 0;
    capture_max_gap_us_ = capture_send_us_ = capture_max_send_us_ = 0;
  }
  if (last_capture_callback_ != std::chrono::steady_clock::time_point{}) {
    capture_max_gap_us_ =
        std::max<int64_t>(capture_max_gap_us_,
                          std::chrono::duration_cast<std::chrono::microseconds>(
                              now - last_capture_callback_)
                              .count());
  }
  last_capture_callback_ = std::max(last_capture_callback_, now);
  ++capture_callbacks_;
  if (from_secure_desktop) ++capture_secure_callbacks_;
}

void SessionDeviceManager::Initialize() {
  screen_capturer_factory_ = new ScreenCapturerFactory();
  device_controller_factory_ = new DeviceControllerFactory();
  keyboard_capturer_ =
      static_cast<KeyboardCapturer *>(device_controller_factory_->Create(
          DeviceControllerFactory::Device::Keyboard));
}

int SessionDeviceManager::EnsureCaptureReady() {
  return screen_capturer_ ? 0 : InitializeScreenCapturer();
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
  screen_capturer_->SetPrivacyController(&owner_.privacy_);
#ifdef _WIN32
  if (auto windows = dynamic_cast<ScreenCapturerWin*>(screen_capturer_)) {
    windows->SetUnattendedHost(owner_.unattended_host_);
    windows->SetNativeCursorPolicy(owner_.host_cursor_default_, owner_.host_cursor_preference_);
  }
#endif

  last_frame_time_ = {};
  next_frame_deadline_ = {};
  capture_metrics_started_ = last_capture_callback_ = {};
  capture_callbacks_ = capture_rate_drops_ = capture_forwarded_ = 0;
  capture_secure_callbacks_ = 0;
  capture_send_errors_ = 0;
  capture_max_gap_us_ = capture_send_us_ = capture_max_send_us_ = 0;
  // Keep the shared source ready for either live rate. MiniRTC admits 30/60
  // frames independently per viewer, so switching never restarts native
  // capture.
  const int fps = 60;
  LOG_INFO("Init screen capturer with {} fps", fps);
  SetDisplayInfoList({});
  registered_display_stream_count_ = 0;
  last_video_frame_stream_id_.clear();
  invalid_video_stream_id_logged_ = false;

  const int init_ret = screen_capturer_->Init(
      fps, [this, fps](unsigned char *data, int size, int width, int height,
                       const char *display_name,
                       const MiniRtcNativeVideoFrame *native_frame) {
        const auto now_time = std::chrono::steady_clock::now();
        bool from_secure_desktop = false;
#ifdef _WIN32
        owner_.host_capture_tick_ = SDL_GetTicks();
        from_secure_desktop =
            ScreenCapturerWin::CurrentFrameIsFromSecureDesktop();
#endif
        RecordCaptureCadence(now_time, fps, from_secure_desktop);
        if (!ShouldSendCapturedFrame(now_time, fps)) {
          std::lock_guard<std::mutex> lock(capture_metrics_mutex_);
          ++capture_rate_drops_;
          return;
        }

        const bool has_previous_frame =
            last_frame_time_.time_since_epoch().count() != 0;
        const auto duration_ms =
            has_previous_frame
                ? std::chrono::duration_cast<std::chrono::milliseconds>(
                      now_time - last_frame_time_)
                      .count()
                : 0;

        const auto connected_remote_ids = owner_.ConnectedControllers();

        // Capture can still deliver frames while ICE is gathering or after
        // the final controller disconnects. Do not broadcast those frames to
        // MiniRTC: a broadcast also reaches newly joining peers whose ICE
        // transport is not ready yet.
        if (connected_remote_ids.empty()) {
          last_frame_time_ = now_time;
          return;
        }

        const std::string stream_id = ResolveDisplayStreamId(
            display_name, registered_display_stream_count_, -1,
            last_video_frame_stream_id_);
        if (stream_id.empty()) {
          if (!invalid_video_stream_id_logged_) {
            LOG_ERROR(
                "Drop captured frames with an empty or unregistered video "
                "stream id, reported='{}', registered_streams={}",
                display_name ? display_name : "",
                registered_display_stream_count_);
            invalid_video_stream_id_logged_ = true;
          }
          return;
        }
        invalid_video_stream_id_logged_ = false;
        const bool resumed_after_gap =
            has_previous_frame && duration_ms >= kCaptureResumeKeyFrameGapMs;
        const bool stream_changed = !last_video_frame_stream_id_.empty() &&
                                    last_video_frame_stream_id_ != stream_id;

        MiniRtcVideoFrame frame{};
        frame.data = reinterpret_cast<const char *>(data);
        frame.size = size;
        frame.width = width;
        frame.height = height;

        frame.native_frame = native_frame;
        const auto send_started = std::chrono::steady_clock::now();
        uint64_t send_errors = 0;
        for (const std::string &remote_id : connected_remote_ids) {
          if (owner_.managed_host_frontend_ && remote_id.rfind("lan/", 0) != 0) continue;
          if (!owner_.preview_subscriptions_.ShouldSend(remote_id, now_time,
                                                        stream_id)) continue;
          if (owner_.WithController(
                  remote_id, [&](MiniRtcPeerPtr* peer, const std::string& wire) {
                    if (resumed_after_gap || stream_changed)
                      MiniRtcRequestVideoKeyFrame(peer, stream_id.c_str());
                    frame.captured_timestamp = MiniRtcGetSystemTimeMicros(peer);
                    return PeerSendVideoFrameToPeer(peer, &frame, stream_id.c_str(),
                                                wire.data(), wire.size());
                  }) != 0)
            ++send_errors;
        }
        const auto send_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - send_started)
                .count();
        {
          std::lock_guard<std::mutex> lock(capture_metrics_mutex_);
          ++capture_forwarded_;
          capture_send_errors_ += send_errors;
          capture_send_us_ += send_us;
          capture_max_send_us_ =
              std::max<int64_t>(capture_max_send_us_, send_us);
        }
        last_video_frame_stream_id_ = stream_id;
        last_frame_time_ = now_time;
      });

  if (init_ret == 0) {
    LOG_INFO("Init screen capturer success");
    auto latest_display_info = screen_capturer_->GetDisplayInfoList();
    registered_display_stream_count_ = latest_display_info.size();
    if (!latest_display_info.empty()) {
      SetDisplayInfoList(std::move(latest_display_info));
    }
    return 0;
  }

  LOG_ERROR("Init screen capturer failed");
  screen_capturer_->Destroy();
  delete screen_capturer_;
  screen_capturer_ = nullptr;
  return -1;
}

bool SessionDeviceManager::ShouldCaptureCursor() const {
  std::shared_lock lock(owner_.connection_status_mutex_);
  return std::any_of(owner_.connection_status_.begin(),
                     owner_.connection_status_.end(), [](const auto& entry) {
    return entry.second == MiniRtcConnectionStatus::MiniRtcConnectionConnected &&
           entry.first.find("web") != std::string::npos;
  });
}

bool SessionDeviceManager::HasConnectedController() const {
  std::shared_lock lock(owner_.connection_status_mutex_);
  return std::any_of(owner_.connection_status_.begin(),
                     owner_.connection_status_.end(), [](const auto& entry) {
    return entry.second == MiniRtcConnectionStatus::MiniRtcConnectionConnected;
  });
}

void SessionDeviceManager::UpdateScreenSharingPower() {
  // Wayland keeps capture warm after the last disconnect. Only live viewers
  // justify keeping the host awake; an outgoing viewing session does not.
  screen_sharing_power_.SetSharing(owner_.screen_capturer_is_started_ &&
                                   HasConnectedController());
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

  // Clear before sampling, so a change arriving after the sample remains pending.
  cursor_capture_dirty_.store(false, std::memory_order_relaxed);
  const bool show_cursor = ShouldCaptureCursor();
  LOG_INFO("Start screen capturer, show cursor: {}", show_cursor);
  screen_sharing_power_.SetSharing(HasConnectedController());
  const int ret = screen_capturer_->Start(show_cursor);
  if (ret != 0) {
    screen_sharing_power_.SetSharing(false);
    LOG_ERROR("Start screen capturer failed: {}", ret);
  }
  return ret;
}

int SessionDeviceManager::StopScreenCapturer() {
  if (screen_capturer_) {
    LOG_INFO("Stop screen capturer");
    screen_capturer_->Stop();
  }
  owner_.screen_capturer_is_started_ = false;
  screen_sharing_power_.SetSharing(false);
  return 0;
}

bool SessionDeviceManager::ReleaseRemoteMouseButtons() {
  return remote_mouse_buttons_.ReleaseAll(
      [this](unsigned mask) { return ReleaseMouseButtonMask(mask); });
}

bool SessionDeviceManager::ReleaseRemoteMouseButtons(const std::string& remote_id,
                                                     unsigned mask) {
  return remote_mouse_buttons_.ReleaseController(
      remote_id, [this](unsigned buttons) { return ReleaseMouseButtonMask(buttons); }, mask);
}

unsigned SessionDeviceManager::ReleaseMouseButtonMask(unsigned mask) {
  unsigned released = mask;
  {
    std::lock_guard lock(mouse_controller_mutex_);
    if (mouse_controller_) released &= mouse_controller_->ReleasePressedButtons(mask);
  }
#ifdef _WIN32
  std::lock_guard lock(service_mouse_mutex_);
  for (int button = 0; button < 3; ++button) {
    const unsigned bit = 1u << button;
    if (!(service_mouse_buttons_ & mask & bit)) continue;
    const auto response = SendCrossDeskSecureDesktopMouseInput(
        service_mouse_x_, service_mouse_y_, 0, (button + 1) * 2, 1000);
    const auto json = nlohmann::json::parse(response, nullptr, false);
    if (json.is_object() && json.value("ok", false)) {
      service_mouse_buttons_ &= ~bit;
    } else {
      released &= ~bit;
      LOG_WARN("Service mouse button release failed: button={}, response={}",
               button, response);
    }
  }
#endif
  return released;
}

#ifdef _WIN32
std::string SessionDeviceManager::SendServiceMouseCommand(int x, int y,
                                                         int wheel, int flag) {
  std::lock_guard lock(service_mouse_mutex_);
  service_mouse_x_ = x;
  service_mouse_y_ = y;
  const unsigned bit = flag >= 1 && flag <= 6 ? 1u << ((flag - 1) / 2) : 0;
  // A timeout can follow successful injection. Keep downs for disconnect
  // cleanup even if the reply was lost; only clear a confirmed release.
  if (flag % 2 == 1) service_mouse_buttons_ |= bit;
  const auto response = SendCrossDeskSecureDesktopMouseInput(x, y, wheel, flag,
                                                           1000);
  const auto json = nlohmann::json::parse(response, nullptr, false);
  if (flag % 2 == 0 && json.is_object() && json.value("ok", false))
    service_mouse_buttons_ &= ~bit;
  return response;
}
#endif

void SessionDeviceManager::StartSpeakerCapturer() {
  if (owner_.unattended_host_) return;
  speaker_capture_.SetEnabled(true);
}

void SessionDeviceManager::StopSpeakerCapturer() {
  speaker_capture_.SetEnabled(false);
}

bool SessionDeviceManager::SyncDisplayInfo() {
  if (!screen_capturer_) return false;
  auto displays = screen_capturer_->GetDisplayInfoList();
  if (displays.empty()) return false;
  // Called every UI tick; replace the shared list only when geometry actually
  // changed so readers on other threads rarely contend for the lock.
  bool changed = false;
  {
    std::lock_guard<std::mutex> lock(display_info_mutex_);
    changed = !std::equal(
        displays.begin(), displays.end(), display_info_list_.begin(),
        display_info_list_.end(), [](const auto &a, const auto &b) {
          return a.handle == b.handle && a.name == b.name &&
                 a.is_primary == b.is_primary && a.left == b.left &&
                 a.top == b.top && a.width == b.width && a.height == b.height;
        });
    if (changed) display_info_list_ = displays;
  }
  if (changed) {
    std::lock_guard lock(mouse_controller_mutex_);
    if (mouse_controller_) mouse_controller_->UpdateDisplayInfoList(displays);
  }
  return true;
}

void SessionDeviceManager::SetDisplayInfoList(std::vector<DisplayInfo> displays) {
  std::lock_guard<std::mutex> lock(display_info_mutex_);
  display_info_list_ = std::move(displays);
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

#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
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

#endif

  SyncDisplayInfo();
  std::lock_guard lock(mouse_controller_mutex_);
  mouse_controller_ =
      static_cast<MouseController *>(device_controller_factory_->Create(
          DeviceControllerFactory::Device::Mouse));
  if (!mouse_controller_) {
    LOG_ERROR("Create mouse controller failed");
    return -1;
  }

  const int init_ret = mouse_controller_->Init(display_info_list());
  if (init_ret != 0) {
    LOG_INFO("Destroy mouse controller");
    mouse_controller_->Destroy();
    delete mouse_controller_;
    mouse_controller_ = nullptr;
  }
  return init_ret;
}

int SessionDeviceManager::StopMouseController(bool force) {
  // Retain the native button state across transient cleanup failures. The UI
  // can keep pumping while the input worker retries, including with no peers.
  if (!force && owner_.remote_input_.HasPendingReleases()) return 1;
  // A disconnect has revoked queued input. Finish its running injection and
  // release callback before destroying the native controller.
  owner_.remote_input_.WaitIdle();
  if (!force && owner_.remote_input_.HasPendingReleases()) return 1;
  ReleaseRemoteMouseButtons();
  std::lock_guard lock(mouse_controller_mutex_);
  if (mouse_controller_) {
    mouse_controller_->Destroy();
    delete mouse_controller_;
    mouse_controller_ = nullptr;
  }
  return 0;
}

int SessionDeviceManager::StartKeyboardCapturer() {
  captured_keyboard_inputs_.Clear();
  owner_.keyboard_capturer_uses_window_events_ = false;
#if defined(_WIN32) || defined(__OHOS__)
  next_keyboard_capture_retry_ =
      std::chrono::steady_clock::now() + kKeyboardCaptureRetryDelay;
#endif

#ifdef __APPLE__
  if (!owner_.EnsureMacAccessibilityPermission()) {
    owner_.keyboard_capturer_uses_window_events_ = true;
    return 0;
  }
#endif

#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
  if (IsWaylandSession()) {
    owner_.keyboard_capturer_uses_window_events_ = true;
    LOG_INFO("Start keyboard capturer with Slint Wayland backend");
    return 0;
  }
#endif

  if (!keyboard_capturer_) {
    owner_.keyboard_capturer_uses_window_events_ = true;
    LOG_WARN(
        "keyboard capturer is nullptr, falling back to Slint keyboard events");
    return 0;
  }

  const int hook_ret = keyboard_capturer_->Hook(
      [](int key_code, bool is_down, uint32_t scan_code, bool extended,
         void *user_ptr) {
        if (user_ptr) {
          auto *devices = static_cast<SessionDeviceManager *>(user_ptr);
          // Native hooks only enqueue; forwarding and logging stay off the
          // hook thread so they cannot trigger LowLevelHooksTimeout.
          devices->captured_keyboard_inputs_.Push(
              {key_code, is_down, scan_code, extended});
        }
      },
      this);
  if (hook_ret != 0) {
    keyboard_capturer_->Unhook();
    owner_.keyboard_capturer_uses_window_events_ = true;
    LOG_WARN(
        "Start keyboard capturer failed, falling back to Slint keyboard events");
  } else {
    LOG_INFO("Start keyboard capturer with native input");
  }
  return 0;
}

int SessionDeviceManager::StopKeyboardCapturer() {
  owner_.keyboard_capturer_is_started_ = false;
  if (keyboard_capturer_) keyboard_capturer_->Unhook();
  captured_keyboard_inputs_.Clear();
  if (owner_.keyboard_capturer_uses_window_events_) {
    owner_.keyboard_capturer_uses_window_events_ = false;
    LOG_INFO("Stop keyboard capturer with Slint keyboard backend");
    return 0;
  }

  if (keyboard_capturer_) {
    LOG_INFO("Stop keyboard capturer");
  }
  return 0;
}

int SessionDeviceManager::InitializeAudioOutput() {
  if (output_device_) return 0;
  const auto now = std::chrono::steady_clock::now();
  if (now < next_audio_output_retry_) return -1;
  // A missing/unavailable playback device must not be reopened for every frame.
  next_audio_output_retry_ = now + std::chrono::seconds(1);

  SDL_AudioSpec desired_out{};
  desired_out.freq = 48000;
  desired_out.format = SDL_AUDIO_S16;
  desired_out.channels = 1;

  auto open_device = [&]() {
    output_device_ = SDL_OpenAudioDevice(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_out);
    return output_device_ != 0;
  };

  if (!open_device()) {
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
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
    if (!open_device()) {
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

  if (!SDL_ResumeAudioDevice(output_device_)) {
    LOG_ERROR("Failed to resume output device: {}", SDL_GetError());
    DestroyAudioOutput();
    return -1;
  }
  next_audio_output_retry_ = {};
  return 0;
}

int SessionDeviceManager::DestroyAudioOutput() {
  for (const auto& [_, stream] : output_streams_)
    SDL_DestroyAudioStream(stream);
  output_streams_.clear();
  if (output_device_) {
    SDL_CloseAudioDevice(output_device_);
    output_device_ = 0;
  }
  return 0;
}

void SessionDeviceManager::ClearAudio(const std::string& remote_id) {
  const auto found = output_streams_.find(remote_id);
  if (found == output_streams_.end()) return;
  SDL_DestroyAudioStream(found->second);
  output_streams_.erase(found);
  // An open SDL playback device can keep the Windows audio driver active even
  // without queued samples. Release it when no peer needs playback anymore.
  if (output_streams_.empty()) DestroyAudioOutput();
}

void SessionDeviceManager::PushAudio(const std::string& remote_id,
                                     const char *data, size_t size) {
  if (!data || size == 0 || owner_.unattended_host_) return;
  if (InitializeAudioOutput() != 0) return;
  auto found = output_streams_.find(remote_id);
  if (found == output_streams_.end()) {
    SDL_AudioSpec spec{};
    spec.freq = 48000;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;
    auto* stream = SDL_CreateAudioStream(&spec, &spec);
    if (!stream) {
      LOG_ERROR("Failed to create output stream: {}", SDL_GetError());
      if (output_streams_.empty()) DestroyAudioOutput();
      return;
    }
    if (!SDL_BindAudioStream(output_device_, stream)) {
      LOG_ERROR("Failed to bind output stream: {}", SDL_GetError());
      SDL_DestroyAudioStream(stream);
      if (output_streams_.empty()) DestroyAudioOutput();
      return;
    }
    found = output_streams_.emplace(remote_id, stream).first;
  }
  if (!SDL_PutAudioStreamData(found->second, data, static_cast<int>(size))) {
    LOG_ERROR("Failed to push audio data: {}", SDL_GetError());
    ClearAudio(remote_id);
  }
}

void SessionDeviceManager::UpdateInteractions() {
  // Release even when pending remote-input cleanup makes this tick return early.
  UpdateScreenSharingPower();
#ifdef _WIN32
  if (owner_.screen_capturer_is_started_ &&
      cursor_capture_dirty_.load(std::memory_order_relaxed)) {
    if (auto* windows = dynamic_cast<ScreenCapturerWin*>(screen_capturer_)) {
      cursor_capture_dirty_.store(false, std::memory_order_relaxed);
      windows->SetCursorCapture(ShouldCaptureCursor());
    }
  }
#endif
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
  const bool is_wayland_session = IsWaylandSession();
  const bool stop_wayland_mouse_before_screen =
      is_wayland_session && !owner_.start_screen_capturer_ &&
      owner_.screen_capturer_is_started_ && !owner_.start_mouse_controller_ &&
      owner_.mouse_controller_is_started_;
  if (stop_wayland_mouse_before_screen) {
    if (StopMouseController() != 0) return;
    LOG_INFO("Stopped Wayland mouse controller before screen capturer to "
             "cleanly release the shared portal session");
    owner_.mouse_controller_is_started_ = false;
  }
#endif

  if (owner_.start_screen_capturer_ && !owner_.screen_capturer_is_started_) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_capture_start_retry_) {
      next_capture_start_retry_ = now + std::chrono::seconds(1);
      if (StartScreenCapturer() == 0) {
        owner_.screen_capturer_is_started_ = true;
        next_capture_start_retry_ = {};
      }
    }
  } else if (!owner_.start_screen_capturer_ &&
             owner_.screen_capturer_is_started_) {
    StopScreenCapturer();
    owner_.screen_capturer_is_started_ = false;
  }
  if (!owner_.start_screen_capturer_) next_capture_start_retry_ = {};

  UpdateScreenSharingPower();

  if (owner_.start_mouse_controller_ && !owner_.mouse_controller_is_started_) {
    if (StartMouseController() == 0) {
      owner_.mouse_controller_is_started_ = true;
    }
  } else if (!owner_.start_mouse_controller_ &&
             owner_.mouse_controller_is_started_) {
    if (StopMouseController() == 0) owner_.mouse_controller_is_started_ = false;
  }

  if (owner_.screen_capturer_is_started_ && mouse_controller_ && SyncDisplayInfo()) {
#if defined(_WIN32) || defined(__OHOS__)
    const int active_display = screen_capturer_->GetCurrentMonitorIndex();
    const auto displays = display_info_list();
    if (active_display >= 0 &&
        active_display < static_cast<int>(displays.size())) {
      owner_.selected_display_ = active_display;
#ifdef _WIN32
      if (displays[active_display].width <= 0 ||
          displays[active_display].height <= 0)
        ReleaseRemoteMouseButtons();
#endif
    }
#endif
  }

  if (owner_.start_keyboard_capturer_ && owner_.focus_on_stream_window_ &&
      !owner_.privacy_.Snapshot().input_blocked) {
#if defined(_WIN32) || defined(__OHOS__)
    if (owner_.keyboard_capturer_is_started_ &&
        owner_.keyboard_capturer_uses_window_events_ && keyboard_capturer_ &&
        keyboard_capturer_->ShouldRetryHook() &&
        std::chrono::steady_clock::now() >= next_keyboard_capture_retry_) {
      // A transient installation failure or stalled hook must not leave
      // system shortcuts in the window-event fallback until focus changes.
      StopKeyboardCapturer();
      owner_.keyboard_.ForceReleasePressedKeys();
    }
#endif
    if (!owner_.keyboard_capturer_is_started_ && StartKeyboardCapturer() == 0) {
      owner_.keyboard_capturer_is_started_ = true;
    }
    if (owner_.keyboard_capturer_is_started_) {
      if (!owner_.keyboard_capturer_uses_window_events_ &&
          (!keyboard_capturer_ || !keyboard_capturer_->IsHookActive())) {
        StopKeyboardCapturer();
        owner_.keyboard_.ForceReleasePressedKeys();
        owner_.keyboard_capturer_uses_window_events_ = true;
        owner_.keyboard_capturer_is_started_ = true;
#if defined(_WIN32) || defined(__OHOS__)
        next_keyboard_capture_retry_ =
            std::chrono::steady_clock::now() + kKeyboardCaptureRetryDelay;
#endif
        LOG_WARN("Native keyboard capture stopped; using Slint keyboard events");
      }
      DrainCapturedKeyboardInput();
      owner_.keyboard_.SendHeartbeat(false);
    }
  } else if (owner_.keyboard_capturer_is_started_) {
    owner_.keyboard_.ForceReleasePressedKeys();
    StopKeyboardCapturer();
  }

  owner_.keyboard_.CheckRemoteTimeouts();
  owner_.HandleDisplayResolutionChanges();
}

void SessionDeviceManager::DrainCapturedKeyboardInput() {
  const auto inputs = captured_keyboard_inputs_.Drain();
  if (owner_.privacy_.Snapshot().input_blocked) return;
  if (inputs.resync) {
    LOG_WARN("Captured keyboard queue overflow; reconciling pressed keys");
    owner_.keyboard_.RecoverCapturedState(inputs.pressed);
    return;
  }
  for (const KeyboardInput &input : inputs.events) {
    owner_.keyboard_.SendKeyCommand(input.key_code, input.is_down,
                                    input.scan_code, input.extended);
  }
}

bool SessionDeviceManager::SupportsTextInput() const {
  return keyboard_capturer_ && keyboard_capturer_->SupportsTextInput();
}

bool SessionDeviceManager::SendTextCommand(const std::string& text) {
  return keyboard_capturer_ && keyboard_capturer_->SendTextCommand(text) == 0;
}

bool SessionDeviceManager::SendKeyboardCommand(int key_code, bool is_down,
                                               uint32_t scan_code,
                                               bool extended) {
  return keyboard_capturer_ && keyboard_capturer_->SendKeyboardCommand(
                                   key_code, is_down, scan_code, extended) == 0;
}

bool SessionDeviceManager::SendMouseCommand(const RemoteAction &action,
                                            int selected_display) {
  std::lock_guard lock(mouse_controller_mutex_);
  if (mouse_controller_) {
    return mouse_controller_->SendMouseCommand(action, selected_display) == 0;
  }
  return false;
}

int SessionDeviceManager::SwitchDisplay(int display_id) {
  if (!screen_capturer_) return -1;
  const int ret = screen_capturer_->SwitchTo(display_id);
  if (ret == 0) SyncDisplayInfo();
  return ret;
}

InputOutcome SessionDeviceManager::ResetToInitialDisplay() {
  if (!screen_capturer_) {
    return InputOutcome::ignored;
  }
  const int ret = screen_capturer_->ResetToInitialMonitor();
  if (ret != 0) {
    LOG_WARN("Reset to initial display failed, ret={}", ret);
    return InputOutcome::failed;
  }
  // Capture backends start at display 0. Reset the input mapping as well so
  // the next connection cannot show that display while controlling another.
  owner_.selected_display_ = 0;
  SyncDisplayInfo();
  return InputOutcome::applied;
}

std::vector<DisplayInfo> SessionDeviceManager::display_info_list() const {
  std::lock_guard<std::mutex> lock(display_info_mutex_);
  return display_info_list_;
}

std::vector<HostDisplay> SessionDeviceManager::host_display_list() const {
  std::lock_guard<std::mutex> lock(display_info_mutex_);
  std::vector<HostDisplay> displays;
  displays.reserve(display_info_list_.size());
  for (const auto &display : display_info_list_) {
    displays.push_back({display.name, display.left, display.top, display.right,
                        display.bottom});
  }
  return displays;
}

void SessionDeviceManager::DestroyDevices() {
  owner_.screen_capturer_is_started_ = false;
  screen_sharing_power_.SetSharing(false);
  speaker_capture_.Shutdown();
  StopMouseController(true);
  if (screen_capturer_) {
    screen_capturer_->Destroy();
    delete screen_capturer_;
    screen_capturer_ = nullptr;
  }
  if (keyboard_capturer_) {
    delete keyboard_capturer_;
    keyboard_capturer_ = nullptr;
  }
}

void SessionDeviceManager::DestroyFactories() {
  delete screen_capturer_factory_;
  screen_capturer_factory_ = nullptr;
  delete device_controller_factory_;
  device_controller_factory_ = nullptr;
}

} // namespace crossdesk
