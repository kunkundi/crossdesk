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
#include "speaker_capturer_factory.h"
#include "thumbnail.h"

class Render {
 public:
  Render();
  ~Render();

 public:
  int Run();

 private:
  int CreateStreamRenderWindow();
  int TitleBar(bool main_window);
  int MainWindow();
  int LocalWindow();
  int RemoteWindow();
  int SettingWindow();
  int ControlWindow();
  int ControlBar();
  int AboutWindow();
  int StatusBar();
  int ConnectionStatusWindow();
  int LoadRecentConnections();
  int ShowRecentConnections();

 private:
  int CreateRtcConnection();
  int CreateMainWindow();
  int DestroyMainWindow();
  int CreateStreamWindow();
  int DestroyStreamWindow();
  int SetupFontAndStyle();
  int SetupMainWindow();
  int DestroyMainWindowContext();
  int SetupStreamWindow();
  int DestroyStreamWindowContext();
  int DrawMainWindow();
  int DrawStreamWindow();

 public:
  static void OnReceiveVideoBufferCb(const XVideoFrame *video_frame,
                                     const char *user_id, size_t user_id_size,
                                     void *user_data);

  static void OnReceiveAudioBufferCb(const char *data, size_t size,
                                     const char *user_id, size_t user_id_size,
                                     void *user_data);

  static void OnReceiveDataBufferCb(const char *data, size_t size,
                                    const char *user_id, size_t user_id_size,
                                    void *user_data);

  static void OnSignalStatusCb(SignalStatus status, void *user_data);

  static void OnConnectionStatusCb(ConnectionStatus status, const char *user_id,
                                   size_t user_id_size, void *user_data);

  static void NetStatusReport(const char *client_id, size_t client_id_size,
                              TraversalMode mode, const unsigned short send,
                              const unsigned short receive, void *user_data);

  static SDL_HitTestResult HitTestCallback(SDL_Window *window,
                                           const SDL_Point *area, void *data);

 private:
  int ProcessMouseKeyEvent(SDL_Event &event);
  int ProcessKeyEvent(SDL_Event &event);
  int ProcessMouseEvent(SDL_Event &event);

  static void SdlCaptureAudioIn(void *userdata, Uint8 *stream, int len);
  static void SdlCaptureAudioOut(void *userdata, Uint8 *stream, int len);

 private:
  int SaveSettingsIntoCacheFile();
  int LoadSettingsFromCacheFile();

  int StartScreenCapture();
  int StopScreenCapture();

  int StartSpeakerCapture();
  int StopSpeakerCapture();

  int StartMouseControl();
  int StopMouseControl();

  int CreateConnectionPeer();

  int AudioDeviceInit();
  int AudioDeviceDestroy();

 private:
  typedef struct {
    char client_id[10];
    char password[7];
    int language;
    int video_quality;
    int video_encode_format;
    bool enable_hardware_video_codec;
  } CDCache;

 private:
  FILE *cd_cache_file_ = nullptr;
  CDCache cd_cache_;
  std::mutex cd_cache_mutex_;

  ConfigCenter config_center_;
  ConfigCenter::LANGUAGE localization_language_ =
      ConfigCenter::LANGUAGE::CHINESE;

  int localization_language_index_ = -1;
  int localization_language_index_last_ = -1;

  bool modules_inited_ = false;

 private:
  std::string window_title = "Remote Desk Client";
  std::string mac_addr_str_ = "";
  std::string connect_button_label_ = "Connect";
  std::string fullscreen_button_label_ = "Fullscreen";
  std::string mouse_control_button_label_ = "Mouse Control";
  std::string audio_capture_button_label_ = "Audio Capture";
  std::string settings_button_label_ = "Setting";
  char input_password_tmp_[7] = "";
  char input_password_[7] = "";
  std::string random_password_ = "";
  char remote_password_[7] = "";
  char new_password_[7] = "";
  char remote_id_display_[12] = "";
  std::string remote_id_ = "";
  char client_password_[20] = "";

 private:
  int title_bar_width_ = 640;
  int title_bar_height_ = 30;
  int screen_width_ = 1280;
  int screen_height_ = 720;
  int main_window_width_default_ = 640;
  int main_window_height_default_ = 480;
  int main_window_width_ = 640;
  int main_window_height_ = 480;
  int main_window_width_last_ = 640;
  int main_window_height_last_ = 480;
  int stream_window_width_default_ = 1280;
  int stream_window_height_default_ = 720;
  int stream_window_width_ = 1280;
  int stream_window_height_ = 720;
  int stream_window_width_last_ = 1280;
  int stream_window_height_last_ = 720;
  int main_window_width_before_maximized_ = 640;
  int main_window_height_before_maximized_ = 480;
  int control_window_min_width_ = 20;
  int control_window_max_width_ = 170;
  int control_window_width_ = 170;
  int control_window_height_ = 40;
  int local_window_width_ = 320;
  int local_window_height_ = 260;
  int remote_window_width_ = 320;
  int remote_window_height_ = 260;
  int local_child_window_width_ = 266;
  int local_child_window_height_ = 180;
  int remote_child_window_width_ = 266;
  int remote_child_window_height_ = 180;
  int main_window_text_y_padding_ = 20;
  int main_child_window_x_padding_ = 27;
  int main_child_window_y_padding_ = 60;
  int status_bar_height_ = 20;
  int connection_status_window_width_ = 200;
  int connection_status_window_height_ = 150;
  int notification_window_width_ = 200;
  int notification_window_height_ = 80;
  int about_window_width_ = 200;
  int about_window_height_ = 150;

  int control_bar_pos_x_ = 0;
  int control_bar_pos_y_ = 30;

  int main_window_width_real_ = 720;
  int main_window_height_real_ = 540;
  float main_window_dpi_scaling_w_ = 1.0f;
  float main_window_dpi_scaling_h_ = 1.0f;

  int stream_window_width_real_ = 1280;
  int stream_window_height_real_ = 720;
  float stream_window_dpi_scaling_w_ = 1.0f;
  float stream_window_dpi_scaling_h_ = 1.0f;

  int texture_width_ = 1280;
  int texture_height_ = 720;

  int video_width_ = 1280;
  int video_height_ = 720;
  int video_size_ = 1280 * 720 * 3;

  SDL_Window *main_window_ = nullptr;
  SDL_Renderer *main_renderer_ = nullptr;
  ImGuiContext *main_ctx_ = nullptr;

  SDL_Window *stream_window_ = nullptr;
  SDL_Renderer *stream_renderer_ = nullptr;
  ImGuiContext *stream_ctx_ = nullptr;
  bool stream_window_created_ = false;
  bool stream_window_inited_ = false;

  // recent connections
  SDL_Texture *recent_connection_texture_ = nullptr;
  int recent_connection_image_width_ = 128;
  int recent_connection_image_height_ = 72;
  uint32_t recent_connection_image_save_time_ = 0;

  // video window
  SDL_Texture *stream_texture_ = nullptr;
  SDL_Rect stream_render_rect_;
  uint32_t stream_pixformat_ = 0;
  std::string host_name_ = "";
  std::string image_path_ = "thumbnails";

  Thumbnail thumbnail_;

  bool resizable_ = false;
  bool label_inited_ = false;
  bool exit_ = false;
  bool exit_video_window_ = false;
  bool connection_established_ = false;
  bool control_bar_hovered_ = false;
  bool connect_button_pressed_ = false;
  bool password_validating_ = false;
  uint32_t password_validating_time_ = 0;
  bool control_bar_expand_ = true;
  bool fullscreen_button_pressed_ = false;
  bool mouse_control_button_pressed_ = false;
  bool audio_capture_button_pressed_ = false;
  bool show_settings_window_ = false;
  bool received_frame_ = false;
  bool is_create_connection_ = false;
  bool audio_buffer_fresh_ = false;
  bool rejoin_ = false;
  bool control_mouse_ = false;
  bool stream_window_grabbed_ = false;
  bool audio_capture_ = true;
  bool local_id_copied_ = false;
  bool show_password_ = true;
  bool password_inited_ = false;
  bool regenerate_password_ = false;
  bool show_about_window_ = false;
  bool show_connection_status_window_ = false;
  bool show_reset_password_window_ = false;
  bool focus_on_input_widget_ = true;
  bool window_maximized_ = false;
  bool streaming_ = false;
  bool is_client_mode_ = false;
  bool is_control_bar_in_left_ = true;
  bool control_window_width_is_changing_ = false;
  bool reload_recent_connections_ = true;
  bool hostname_sent_ = false;

  double copy_start_time_ = 0;
  double regenerate_password_start_time_ = 0;
  double control_bar_button_pressed_time_ = 0;

  ImVec2 control_winodw_pos_;

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
  bool signal_connected_ = false;
  bool p2p_mode_ = true;

 private:
  PeerPtr *peer_ = nullptr;
  PeerPtr *peer_reserved_ = nullptr;
  Params params_;

 private:
  SDL_AudioDeviceID input_dev_;
  SDL_AudioDeviceID output_dev_;
  unsigned char audio_buffer_[720];
  int audio_len_ = 0;
  unsigned char *dst_buffer_ = nullptr;
  int dst_buffer_capacity_ = 0;

 private:
  ScreenCapturerFactory *screen_capturer_factory_ = nullptr;
  ScreenCapturer *screen_capturer_ = nullptr;
  SpeakerCapturerFactory *speaker_capturer_factory_ = nullptr;
  SpeakerCapturer *speaker_capturer_ = nullptr;
  DeviceControllerFactory *device_controller_factory_ = nullptr;
  MouseController *mouse_controller_ = nullptr;
  uint32_t last_frame_time_;

 private:
  char client_id_[10] = "";
  char client_id_display_[12] = "";
  char password_saved_[7] = "";
  int language_button_value_ = 0;
  int video_quality_button_value_ = 0;
  int video_encode_format_button_value_ = 0;
  bool enable_hardware_video_codec_ = false;
  bool enable_turn_ = false;

  int language_button_value_last_ = 0;
  int video_quality_button_value_last_ = 0;
  int video_encode_format_button_value_last_ = 0;
  bool enable_hardware_video_codec_last_ = false;
  bool enable_turn_last_ = false;

 private:
  std::atomic<bool> start_screen_capture_{false};
  std::atomic<bool> start_mouse_control_{false};
  std::atomic<bool> screen_capture_is_started_{false};
  std::atomic<bool> mouse_control_is_started_{false};

 private:
  bool settings_window_pos_reset_ = true;
};

#endif