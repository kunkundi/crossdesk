/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _APPLICATION_STATE_H_
#define _APPLICATION_STATE_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace crossdesk::gui_detail {

struct MainWindowState {
  bool exit_ = false;
};

// Application-global interaction flags that coordinate UI events with media
// and remote-control devices.
struct InteractionState {
  bool start_mouse_controller_ = false;
  bool mouse_controller_is_started_ = false;
  bool start_screen_capturer_ = false;
  bool screen_capturer_is_started_ = false;
  bool start_keyboard_capturer_ = false;
  bool keyboard_capturer_is_started_ = false;
  bool keyboard_capturer_uses_window_events_ = false;
  bool focus_on_stream_window_ = false;
  bool audio_capture_ = false;
  int screen_width_ = 1280;
  int screen_height_ = 720;
  int selected_display_ = 0;
  char remote_id_display_[12] = "";
  unsigned char audio_buffer_[720]{};
  int audio_len_ = 0;
  bool audio_buffer_fresh_ = false;
  bool need_to_rejoin_ = false;
  std::chrono::steady_clock::time_point last_rejoin_check_time_ =
      std::chrono::steady_clock::now();
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
};

struct StreamWindowState {
  bool need_to_create_stream_window_ = false;
  bool stream_window_created_ = false;
  bool stream_window_inited_ = false;
  bool control_mouse_ = false;
};

struct ServerWindowState {
  // Transport callbacks run outside the UI thread. Keep the two lifecycle
  // requests atomic so a disconnect cannot be lost while the Slint timer is
  // deciding whether to create or destroy the controlled-side window.
  std::atomic<bool> need_to_create_server_window_{false};
  std::atomic<bool> need_to_destroy_server_window_{false};
  bool server_window_created_ = false;
  bool server_window_inited_ = false;
  bool server_window_collapsed_ = false;
};

struct UiState {
  bool password_validating_ = false;
  uint32_t password_validating_time_ = 0;
  bool rejoin_ = false;
  bool show_password_ = true;
  bool show_connection_status_window_ = false;
  bool fullscreen_button_pressed_ = false;
  bool is_client_mode_ = false;
  bool is_server_mode_ = false;
  bool reload_recent_connections_ = true;
  bool show_offline_warning_window_ = false;
  std::string offline_warning_text_;
  bool re_enter_remote_id_ = false;
};

struct ApplicationState : MainWindowState,
                          InteractionState,
                          UpdateState,
                          StreamWindowState,
                          ServerWindowState,
                          UiState {};

}  // namespace crossdesk::gui_detail

#endif
