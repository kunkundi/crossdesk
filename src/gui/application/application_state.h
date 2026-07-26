#ifndef CROSSDESK_GUI_APPLICATION_STATE_H_
#define CROSSDESK_GUI_APPLICATION_STATE_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace crossdesk::gui_detail {

struct MainWindowState {
  float title_bar_width_ = 640;
  float title_bar_height_ = 30;
  float title_bar_button_width_ = 30;
  float title_bar_button_height_ = 30;

  bool exit_ = false;
  const int sdl_refresh_ms_ = 16;

  bool main_window_minimized_ = false;
  int main_window_width_real_ = 720;
  int main_window_height_real_ = 540;
  float main_window_dpi_scaling_w_ = 1.0f;
  float main_window_dpi_scaling_h_ = 1.0f;
  float dpi_scale_ = 1.0f;
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
  float about_window_width_ = 300;
  float about_window_height_ = 170;
  float update_notification_window_width_ = 400;
  float update_notification_window_height_ = 320;
};

// Application-global interaction flags that coordinate SDL events with media
// and remote-control devices.
struct InteractionState {
  bool start_mouse_controller_ = false;
  bool mouse_controller_is_started_ = false;
  bool start_screen_capturer_ = false;
  bool screen_capturer_is_started_ = false;
  bool start_speaker_capturer_ = false;
  bool speaker_capturer_is_started_ = false;
  bool start_keyboard_capturer_ = false;
  bool show_cursor_ = false;
  bool keyboard_capturer_is_started_ = false;
  bool keyboard_capturer_uses_sdl_events_ = false;
  bool foucs_on_main_window_ = false;
  bool focus_on_stream_window_ = false;
  bool audio_capture_ = false;
  int screen_width_ = 1280;
  int screen_height_ = 720;
  int selected_display_ = 0;
  std::string connect_button_label_ = "Connect";
  char input_password_tmp_[7] = "";
  char input_password_[7] = "";
  std::string random_password_;
  char new_password_[7] = "";
  char remote_id_display_[12] = "";
  unsigned char audio_buffer_[720]{};
  int audio_len_ = 0;
  bool audio_buffer_fresh_ = false;
  bool need_to_rejoin_ = false;
  std::chrono::steady_clock::time_point last_rejoin_check_time_ =
      std::chrono::steady_clock::now();
  bool just_created_ = false;
  std::string controlled_remote_id_;
  std::string focused_remote_id_;
  std::string remote_client_id_;
};

struct UpdateState {
  nlohmann::json latest_version_info_ = nlohmann::json{};
  bool update_available_ = false;
  std::string latest_version_;
  std::string release_name_;
  std::string release_notes_;
  std::string release_date_;
  bool show_new_version_icon_ = false;
  bool show_new_version_icon_in_menu_ = true;
  double new_version_icon_last_trigger_time_ = 0.0;
  double new_version_icon_render_start_time_ = 0.0;
};

struct StreamWindowState {
  bool need_to_create_stream_window_ = false;
  bool stream_window_created_ = false;
  bool stream_window_inited_ = false;
  bool window_maximized_ = false;
  bool control_mouse_ = false;
  int stream_window_width_default_ = 1280;
  int stream_window_height_default_ = 720;
  float stream_window_width_ = 1280;
  float stream_window_height_ = 720;
  int stream_window_width_real_ = 1280;
  int stream_window_height_real_ = 720;
  float stream_window_dpi_scaling_w_ = 1.0f;
  float stream_window_dpi_scaling_h_ = 1.0f;
};

struct ServerWindowState {
  // Transport callbacks run outside the UI thread. Keep the two lifecycle
  // requests atomic so a disconnect cannot be lost while the Slint timer is
  // deciding whether to create or destroy the controlled-side window.
  std::atomic<bool> need_to_create_server_window_{false};
  std::atomic<bool> need_to_destroy_server_window_{false};
  bool server_window_created_ = false;
  bool server_window_inited_ = false;
  int server_window_width_default_ = 250;
  int server_window_height_default_ = 150;
  float server_window_width_ = 250;
  float server_window_height_ = 150;
  float server_window_title_bar_height_ = 30.0f;
  int server_window_normal_width_ = 250;
  int server_window_normal_height_ = 150;
  float server_window_dpi_scaling_w_ = 1.0f;
  float server_window_dpi_scaling_h_ = 1.0f;
  float window_rounding_ = 6.0f;
  float window_rounding_default_ = 6.0f;
  bool server_window_collapsed_ = false;
  bool server_window_collapsed_dragging_ = false;
  float server_window_collapsed_drag_start_mouse_x_ = 0.0f;
  float server_window_collapsed_drag_start_mouse_y_ = 0.0f;
  int server_window_collapsed_drag_start_win_x_ = 0;
  int server_window_collapsed_drag_start_win_y_ = 0;
  bool server_window_dragging_ = false;
  float server_window_drag_start_mouse_x_ = 0.0f;
  float server_window_drag_start_mouse_y_ = 0.0f;
  int server_window_drag_start_win_x_ = 0;
  int server_window_drag_start_win_y_ = 0;
};

struct UiState {
  bool label_inited_ = false;
  bool connect_button_pressed_ = false;
  bool password_validating_ = false;
  uint32_t password_validating_time_ = 0;
  bool show_settings_window_ = false;
  bool show_self_hosted_server_config_window_ = false;
  bool rejoin_ = false;
  bool local_id_copied_ = false;
  bool show_password_ = true;
  bool show_about_window_ = false;
  bool show_connection_status_window_ = false;
  bool show_reset_password_window_ = false;
  bool show_update_notification_window_ = false;
  bool fullscreen_button_pressed_ = false;
  bool focus_on_input_widget_ = true;
  bool is_client_mode_ = false;
  bool is_server_mode_ = false;
  bool reload_recent_connections_ = true;
  bool show_confirm_delete_connection_ = false;
  bool show_edit_connection_alias_window_ = false;
  bool show_offline_warning_window_ = false;
  bool delete_connection_ = false;
  bool is_tab_bar_hovered_ = false;
  std::string delete_connection_name_;
  std::string edit_connection_alias_remote_id_;
  char edit_connection_alias_[128] = "";
  std::string offline_warning_text_;
  bool re_enter_remote_id_ = false;
  double copy_start_time_ = 0;
};

struct ApplicationState : MainWindowState,
                          InteractionState,
                          UpdateState,
                          StreamWindowState,
                          ServerWindowState,
                          UiState {};

}  // namespace crossdesk::gui_detail

#endif  // CROSSDESK_GUI_APPLICATION_STATE_H_
