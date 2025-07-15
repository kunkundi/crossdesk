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
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "IconsFontAwesome6.h"
#include "config_center.h"
#include "device_controller_factory.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_internal.h"
#include "minirtc.h"
#include "screen_capturer_factory.h"
#include "speaker_capturer_factory.h"
#include "thumbnail.h"

class Render {
 public:
  struct SubStreamWindowProperties {
    Params params_;
    PeerPtr *peer_ = nullptr;
    std::string audio_label_ = "control_audio";
    std::string data_label_ = "control_data";
    std::string local_id_ = "";
    std::string remote_id_ = "";
    bool exit_ = false;
    bool signal_connected_ = false;
    SignalStatus signal_status_ = SignalStatus::SignalClosed;
    bool connection_established_ = false;
    bool rejoin_ = false;
    bool net_traffic_stats_button_pressed_ = false;
    bool mouse_control_button_pressed_ = false;
    bool mouse_controller_is_started_ = false;
    bool audio_capture_button_pressed_ = false;
    bool control_mouse_ = false;
    bool streaming_ = false;
    bool is_control_bar_in_left_ = true;
    bool control_bar_hovered_ = false;
    bool display_selectable_hovered_ = false;
    bool control_bar_expand_ = true;
    bool reset_control_bar_pos_ = false;
    bool control_window_width_is_changing_ = false;
    bool control_window_height_is_changing_ = false;
    bool p2p_mode_ = true;
    bool remember_password_ = false;
    char remote_password_[7] = "";
    float sub_stream_window_width_ = 1280;
    float sub_stream_window_height_ = 720;
    float control_window_min_width_ = 20;
    float control_window_max_width_ = 230;
    float control_window_min_height_ = 40;
    float control_window_max_height_ = 150;
    float control_window_width_ = 230;
    float control_window_height_ = 40;
    float control_bar_pos_x_ = 0;
    float control_bar_pos_y_ = 30;
    float mouse_diff_control_bar_pos_x_ = 0;
    float mouse_diff_control_bar_pos_y_ = 0;
    double control_bar_button_pressed_time_ = 0;
    double net_traffic_stats_button_pressed_time_ = 0;
    unsigned char *dst_buffer_ = nullptr;
    size_t dst_buffer_capacity_ = 0;
    int mouse_pos_x_ = 0;
    int mouse_pos_y_ = 0;
    int mouse_pos_x_last_ = 0;
    int mouse_pos_y_last_ = 0;
    int texture_width_ = 1280;
    int texture_height_ = 720;
    int video_width_ = 0;
    int video_height_ = 0;
    int video_width_last_ = 0;
    int video_height_last_ = 0;
    int selected_display_ = 0;
    size_t video_size_ = 0;
    bool tab_selected_ = false;
    bool tab_opened_ = true;
    std::optional<float> pos_x_before_docked_;
    std::optional<float> pos_y_before_docked_;
    float render_window_x_ = 0;
    float render_window_y_ = 0;
    float render_window_width_ = 0;
    float render_window_height_ = 0;
    std::string fullscreen_button_label_ = "Fullscreen";
    std::string net_traffic_stats_button_label_ = "Show Net Traffic Stats";
    std::string mouse_control_button_label_ = "Mouse Control";
    std::string audio_capture_button_label_ = "Audio Capture";
    std::string remote_host_name_ = "";
    std::vector<DisplayInfo> display_info_list_;
    SDL_Texture *stream_texture_ = nullptr;
    SDL_Rect stream_render_rect_;
    SDL_Rect stream_render_rect_last_;
    ImVec2 control_window_pos_;
    ConnectionStatus connection_status_ = ConnectionStatus::Closed;
    TraversalMode traversal_mode_ = TraversalMode::UnknownMode;
    XNetTrafficStats net_traffic_stats_;
  };

 public:
  Render();
  ~Render();

 public:
  int Run();

 private:
  void InitializeSettings();
  void InitializeSDL();
  void InitializeModules();
  void InitializeMainWindow();
  void MainLoop();
  void UpdateLabels();
  void UpdateInteractions();
  void HandleRecentConnections();
  void HandleStreamWindow();
  void Cleanup();
  void CleanupFactories();
  void CleanupPeer(std::shared_ptr<SubStreamWindowProperties> props);
  void CleanupPeers();
  void CleanSubStreamWindowProperties(
      std::shared_ptr<SubStreamWindowProperties> props);
  void UpdateRenderRect();
  void ProcessSdlEvent();

 private:
  int CreateStreamRenderWindow();
  int TitleBar(bool main_window);
  int MainWindow();
  int StreamWindow();
  int LocalWindow();
  int RemoteWindow();
  int RecentConnectionsWindow();
  int SettingWindow();
  int ControlWindow(std::shared_ptr<SubStreamWindowProperties> &props);
  int ControlBar(std::shared_ptr<SubStreamWindowProperties> &props);
  int AboutWindow();
  int StatusBar();
  int ConnectionStatusWindow(std::shared_ptr<SubStreamWindowProperties> &props);
  int ShowRecentConnections();

 private:
  int ConnectTo(const std::string &remote_id, const char *password,
                bool remember_password);
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
  int ConfirmDeleteConnection();
  int NetTrafficStats(std::shared_ptr<SubStreamWindowProperties> &props);
  void DrawConnectionStatusText(
      std::shared_ptr<SubStreamWindowProperties> &props);

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

  static void OnSignalStatusCb(SignalStatus status, const char *user_id,
                               size_t user_id_size, void *user_data);

  static void OnConnectionStatusCb(ConnectionStatus status, const char *user_id,
                                   size_t user_id_size, void *user_data);

  static void NetStatusReport(const char *client_id, size_t client_id_size,
                              TraversalMode mode,
                              const XNetTrafficStats *net_traffic_stats,
                              const char *user_id, const size_t user_id_size,
                              void *user_data);

  static SDL_HitTestResult HitTestCallback(SDL_Window *window,
                                           const SDL_Point *area, void *data);

  static std::vector<char> SerializeRemoteAction(const RemoteAction &action);

  static bool DeserializeRemoteAction(const char *data, size_t size,
                                      RemoteAction &out);

  static void FreeRemoteAction(RemoteAction &action);

 private:
  int SendKeyCommand(int key_code, bool is_down);
  int ProcessMouseEvent(SDL_Event &event);

  static void SdlCaptureAudioIn(void *userdata, Uint8 *stream, int len);
  static void SdlCaptureAudioOut(void *userdata, Uint8 *stream, int len);

 private:
  int SaveSettingsIntoCacheFile();
  int LoadSettingsFromCacheFile();

  int ScreenCapturerInit();
  int StartScreenCapturer();
  int StopScreenCapturer();

  int StartSpeakerCapturer();
  int StopSpeakerCapturer();

  int StartMouseController();
  int StopMouseController();

  int StartKeyboardCapturer();
  int StopKeyboardCapturer();

  int CreateConnectionPeer();

  int AudioDeviceInit();
  int AudioDeviceDestroy();

 private:
  struct CDCache {
    char client_id_with_password[17];
    int language;
    int video_quality;
    int video_encode_format;
    bool enable_hardware_video_codec;
    bool enable_turn;

    unsigned char key[16];
    unsigned char iv[16];
  };

 private:
  CDCache cd_cache_;
  std::mutex cd_cache_mutex_;
  ConfigCenter config_center_;
  ConfigCenter::LANGUAGE localization_language_ =
      ConfigCenter::LANGUAGE::CHINESE;
  int localization_language_index_ = -1;
  int localization_language_index_last_ = -1;
  bool modules_inited_ = false;

  /* ------ all windows property start ------ */
  float title_bar_width_ = 640;
  float title_bar_height_ = 30;
  /* ------ all windows property end ------ */

  /* ------ main window property start ------ */
  // thumbnail
  unsigned char aes128_key_[16];
  unsigned char aes128_iv_[16];
  std::unique_ptr<Thumbnail> thumbnail_;

  // recent connections
  std::unordered_map<std::string, Thumbnail::RecentConnection>
      recent_connections_;
  int recent_connection_image_width_ = 160;
  int recent_connection_image_height_ = 90;
  uint32_t recent_connection_image_save_time_ = 0;

  // main window render
  SDL_Window *main_window_ = nullptr;
  SDL_Renderer *main_renderer_ = nullptr;
  ImGuiContext *main_ctx_ = nullptr;
  bool exit_ = false;

  // main window properties
  bool start_mouse_controller_ = false;
  bool mouse_controller_is_started_ = false;
  bool start_screen_capturer_ = false;
  bool screen_capturer_is_started_ = false;
  bool start_keyboard_capturer_ = false;
  bool keyboard_capturer_is_started_ = false;
  bool foucs_on_main_window_ = false;
  bool foucs_on_stream_window_ = false;
  bool audio_capture_ = false;
  int main_window_width_real_ = 720;
  int main_window_height_real_ = 540;
  float main_window_dpi_scaling_w_ = 1.0f;
  float main_window_dpi_scaling_h_ = 1.0f;
  float main_window_width_default_ = 640;
  float main_window_height_default_ = 480;
  float main_window_width_ = 640;
  float main_window_height_ = 480;
  float main_window_width_last_ = 640;
  float main_window_height_last_ = 480;
  float local_window_width_ = 320;
  float local_window_height_ = 235;
  float remote_window_width_ = 320;
  float remote_window_height_ = 235;
  float local_child_window_width_ = 266;
  float local_child_window_height_ = 180;
  float remote_child_window_width_ = 266;
  float remote_child_window_height_ = 180;
  float main_window_text_y_padding_ = 10;
  float main_child_window_x_padding_ = 27;
  float main_child_window_y_padding_ = 45;
  float status_bar_height_ = 22;
  float connection_status_window_width_ = 200;
  float connection_status_window_height_ = 150;
  float notification_window_width_ = 200;
  float notification_window_height_ = 80;
  float about_window_width_ = 200;
  float about_window_height_ = 150;
  int screen_width_ = 1280;
  int screen_height_ = 720;
  int selected_display_ = 0;
  std::string connect_button_label_ = "Connect";
  char input_password_tmp_[7] = "";
  char input_password_[7] = "";
  std::string random_password_ = "";
  char new_password_[7] = "";
  char remote_id_display_[12] = "";
  unsigned char audio_buffer_[720];
  int audio_len_ = 0;
  bool audio_buffer_fresh_ = false;
  bool need_to_rejoin_ = false;
  bool just_created_ = false;
  std::string controlled_remote_id_ = "";
  bool need_to_send_host_info_ = false;
  SDL_Event last_mouse_event;

  // stream window render
  SDL_Window *stream_window_ = nullptr;
  SDL_Renderer *stream_renderer_ = nullptr;
  ImGuiContext *stream_ctx_ = nullptr;

  // stream window properties
  bool need_to_create_stream_window_ = false;
  bool stream_window_created_ = false;
  bool stream_window_inited_ = false;
  bool window_maximized_ = false;
  bool stream_window_grabbed_ = false;
  bool control_mouse_ = false;
  int stream_window_width_default_ = 1280;
  int stream_window_height_default_ = 720;
  float stream_window_width_ = 1280;
  float stream_window_height_ = 720;
  uint32_t stream_pixformat_ = 0;
  int stream_window_width_real_ = 1280;
  int stream_window_height_real_ = 720;
  float stream_window_dpi_scaling_w_ = 1.0f;
  float stream_window_dpi_scaling_h_ = 1.0f;

  bool label_inited_ = false;
  bool connect_button_pressed_ = false;
  bool password_validating_ = false;
  uint32_t password_validating_time_ = 0;
  bool show_settings_window_ = false;
  bool rejoin_ = false;
  bool local_id_copied_ = false;
  bool show_password_ = true;
  bool regenerate_password_ = false;
  bool show_about_window_ = false;
  bool show_connection_status_window_ = false;
  bool show_reset_password_window_ = false;
  bool fullscreen_button_pressed_ = false;
  bool focus_on_input_widget_ = true;
  bool is_client_mode_ = false;
  bool reload_recent_connections_ = true;
  bool show_confirm_delete_connection_ = false;
  bool delete_connection_ = false;
  bool is_tab_bar_hovered_ = false;
  std::string delete_connection_name_ = "";
  bool re_enter_remote_id_ = false;
  double copy_start_time_ = 0;
  double regenerate_password_start_time_ = 0;
  SignalStatus signal_status_ = SignalStatus::SignalClosed;
  std::string signal_status_str_ = "";
  bool signal_connected_ = false;
  PeerPtr *peer_ = nullptr;
  PeerPtr *peer_reserved_ = nullptr;
  std::string video_primary_label_ = "primary_display";
  std::string video_secondary_label_ = "secondary_display";
  std::string audio_label_ = "audio";
  std::string data_label_ = "data";
  Params params_;
  SDL_AudioDeviceID input_dev_;
  SDL_AudioDeviceID output_dev_;
  ScreenCapturerFactory *screen_capturer_factory_ = nullptr;
  ScreenCapturer *screen_capturer_ = nullptr;
  SpeakerCapturerFactory *speaker_capturer_factory_ = nullptr;
  SpeakerCapturer *speaker_capturer_ = nullptr;
  DeviceControllerFactory *device_controller_factory_ = nullptr;
  MouseController *mouse_controller_ = nullptr;
  KeyboardCapturer *keyboard_capturer_ = nullptr;
  std::vector<DisplayInfo> display_info_list_;
  uint64_t last_frame_time_;
  char client_id_[10] = "";
  char client_id_display_[12] = "";
  char client_id_with_password_[17] = "";
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
  bool settings_window_pos_reset_ = true;
  /* ------ main window property end ------ */

  /* ------ sub stream window property start ------ */
  std::unordered_map<std::string, std::shared_ptr<SubStreamWindowProperties>>
      client_properties_;
  void CloseTab(decltype(client_properties_)::iterator &it);
  /* ------ stream window property end ------ */
};

#endif