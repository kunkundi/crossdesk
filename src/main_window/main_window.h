/*
 * @Author: DI JUNKUN
 * @Date: 2024-05-29
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MAIN_WINDOW_H_
#define _MAIN_WINDOW_H_

#include <SDL.h>

#include <atomic>
#include <string>

#include "config_center.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

class MainWindow {
 public:
  MainWindow();
  ~MainWindow();

 public:
  int Run();

 private:
  typedef struct {
    char password[7];
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
  int default_main_window_width_ = 1280;
  int default_main_window_height_ = 720;

 private:
  std::string window_title = "Remote Desk Client";

 private:
  int screen_width_ = 1280;
  int screen_height_ = 720;
  int main_window_width_ = 1280;
  int main_window_height_ = 720;

  int texture_width_ = 1280;
  int texture_height_ = 720;

  SDL_Texture *sdl_texture_ = nullptr;
  SDL_Renderer *sdl_renderer_ = nullptr;
  SDL_Rect sdlRect;
  SDL_Window *main_window_;
  uint32_t pixformat_ = 0;

  std::string mac_addr_str_ = "";

  bool exit_ = false;

  std::string connect_button_label_ = "Connect";
  std::string fullscreen_button_label_ = "Fullscreen";

  bool connection_established_ = false;
  bool menu_hovered_ = false;

  char input_password_tmp_[7] = "";
  char input_password_[7] = "";

  char remote_id_[20] = "";
  char client_password_[20] = "";

  bool connect_button_pressed_ = false;
  bool fullscreen_button_pressed_ = false;

  int fps_ = 0;
  uint32_t start_time_;
  uint32_t end_time_;
  uint32_t elapsed_time_;
  uint32_t frame_count_ = 0;

  bool received_frame_ = false;

 private:
  std::string server_connection_status_str_ = "-";
  std::string client_connection_status_str_ = "-";
  std::string server_signal_status_str_ = "-";
  std::string client_signal_status_str_ = "-";
};

#endif