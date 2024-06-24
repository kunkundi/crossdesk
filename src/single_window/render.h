/*
 * @Author: DI JUNKUN
 * @Date: 2024-05-29
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MAIN_WINDOW_H_
#define _MAIN_WINDOW_H_

#include <SDL.h>

#include <atomic>
#include <chrono>
#include <string>

#include "../../thirdparty/projectx/src/interface/x.h"
#include "config_center.h"
#include "device_controller_factory.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "screen_capturer_factory.h"

class Render {
 public:
  Render();
  ~Render();

 public:
  int Run();

 public:
  static void OnReceiveVideoBufferCb(const char *data, size_t size,
                                     const char *user_id, size_t user_id_size,
                                     void *user_data);

  static void OnReceiveAudioBufferCb(const char *data, size_t size,
                                     const char *user_id, size_t user_id_size,
                                     void *user_data);

  static void OnReceiveDataBufferCb(const char *data, size_t size,
                                    const char *user_id, size_t user_id_size,
                                    void *user_data);

  static void OnSignalStatusCb(SignalStatus status, void *user_data);

  static void OnConnectionStatusCb(ConnectionStatus status, void *user_data);

 private:
  int ProcessMouseKeyEven(SDL_Event &ev);
  void SdlCaptureAudioIn(void *userdata, Uint8 *stream, int len);
  void SdlCaptureAudioOut(void *userdata, Uint8 *stream, int len);

 private:
  int SaveSettingsIntoCacheFile();
  int LoadSettingsIntoCacheFile();

  int StartScreenCapture();
  int StopScreenCapture();

  int StartMouseControl();
  int StopMouseControl();

  int CreateConnectionPeer();

 private:
  typedef struct {
    char password[7];
    int language;
    int video_quality;
    int video_encode_format;
    bool enable_hardware_video_codec;
  } CDCache;

 private:
  FILE *cd_cache_file_ = nullptr;
  CDCache cd_cache_;

  ConfigCenter config_center_;

  ConfigCenter::LANGUAGE localization_language_ =
      ConfigCenter::LANGUAGE::CHINESE;

  int localization_language_index_ = -1;
  int localization_language_index_last_ = -1;

 private:
  std::string window_title = "Remote Desk Client";
  std::string mac_addr_str_ = "";
  std::string connect_button_label_ = "Connect";
  std::string fullscreen_button_label_ = "Fullscreen";
  std::string mouse_control_button_label_ = "Mouse Control";
  std::string settings_button_label_ = "Setting";
  char input_password_tmp_[7] = "";
  char input_password_[7] = "";
  std::string local_id_ = "";
  char remote_id_[20] = "";
  char client_password_[20] = "";
  bool is_client_mode_ = false;

 private:
  int screen_width_ = 1280;
  int screen_height_ = 720;
  int main_window_width_ = 1280;
  int main_window_height_ = 720;
  int main_window_width_before_fullscreen_ = 1280;
  int main_window_height_before_fullscreen_ = 720;

  int texture_width_ = 1280;
  int texture_height_ = 720;

  SDL_Texture *sdl_texture_ = nullptr;
  SDL_Renderer *sdl_renderer_ = nullptr;
  SDL_Rect sdl_rect_;
  SDL_Window *main_window_;
  uint32_t pixformat_ = 0;

  bool inited_ = false;
  bool exit_ = false;
  bool connection_established_ = false;
  bool subwindow_hovered_ = false;
  bool connect_button_pressed_ = false;
  bool fullscreen_button_pressed_ = false;
  bool mouse_control_button_pressed_ = false;
  bool settings_button_pressed_ = false;
  bool received_frame_ = false;
  bool is_create_connection_ = false;
  bool audio_buffer_fresh_ = false;
  bool rejoin_ = false;
  bool control_mouse_ = false;

  int fps_ = 0;
  uint32_t start_time_;
  uint32_t end_time_;
  uint32_t elapsed_time_;
  uint32_t frame_count_ = 0;

 private:
  ConnectionStatus connection_status_ = ConnectionStatus::Closed;
  SignalStatus signal_status_ = SignalStatus::SignalClosed;
  std::string signal_status_str_ = "";
  std::string connection_status_str_ = "";

 private:
  PeerPtr *peer_ = nullptr;
  PeerPtr *peer_reserved_ = nullptr;
  Params params_;

 private:
  SDL_AudioDeviceID input_dev_;
  SDL_AudioDeviceID output_dev_;
  unsigned char audio_buffer_[960];
  int audio_len_ = 0;
  char *nv12_buffer_ = nullptr;
  unsigned char *dst_buffer_ = new unsigned char[1280 * 720 * 3];

 private:
  ScreenCapturerFactory *screen_capturer_factory_ = nullptr;
  ScreenCapturer *screen_capturer_ = nullptr;
  DeviceControllerFactory *device_controller_factory_ = nullptr;
  MouseController *mouse_controller_ = nullptr;

#ifdef __linux__
  std::chrono::_V2::system_clock::time_point last_frame_time_;
#else
  std::chrono::steady_clock::time_point last_frame_time_;
#endif

 private:
  int language_button_value_ = 0;
  int video_quality_button_value_ = 0;
  int video_encode_format_button_value_ = 0;
  bool enable_hardware_video_codec_ = false;

  int language_button_value_last_ = 0;
  int video_quality_button_value_last_ = 0;
  int video_encode_format_button_value_last_ = 0;
  bool enable_hardware_video_codec_last_ = false;

 private:
  std::atomic<bool> start_screen_capture_{false};
  std::atomic<bool> start_mouse_control_{false};
  std::atomic<bool> screen_capture_is_started_{false};
  std::atomic<bool> mouse_control_is_started_{false};

 private:
  bool settings_window_pos_reset_ = true;
};

#endif