#include "main_window.h"

#include <fstream>
#include <iostream>
#include <string>

#include "localization.h"
#include "log.h"
#include "platform.h"

// Refresh Event
#define REFRESH_EVENT (SDL_USEREVENT + 1)
#define QUIT_EVENT (SDL_USEREVENT + 2)

MainWindow::MainWindow() {}

MainWindow::~MainWindow() {}

int MainWindow::Run() {
  cd_cache_file_ = fopen("cache.cd", "r+");
  if (cd_cache_file_) {
    fseek(cd_cache_file_, 0, SEEK_SET);
    fread(&cd_cache_.password, sizeof(cd_cache_.password), 1, cd_cache_file_);
    fclose(cd_cache_file_);
    strncpy(input_password_, cd_cache_.password, sizeof(cd_cache_.password));
  }

  // Setup SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER |
               SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Error: %s\n", SDL_GetError());
    return -1;
  }

  // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

  // Create main window with SDL_Renderer graphics context
  SDL_WindowFlags window_flags =
      (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  main_window_ = SDL_CreateWindow("Remote Desk", SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, main_window_width_,
                                  main_window_height_, window_flags);

  SDL_DisplayMode DM;
  SDL_GetCurrentDisplayMode(0, &DM);
  screen_width_ = DM.w;
  screen_height_ = DM.h;

  sdl_renderer_ = SDL_CreateRenderer(
      main_window_, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
  if (sdl_renderer_ == nullptr) {
    SDL_Log("Error creating SDL_Renderer!");
    return 0;
  }

  pixformat_ = SDL_PIXELFORMAT_NV12;

  sdl_texture_ =
      SDL_CreateTexture(sdl_renderer_, pixformat_, SDL_TEXTUREACCESS_STREAMING,
                        texture_width_, texture_height_);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();

  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

  if (config_center_.GetLanguage() == ConfigCenter::LANGUAGE::CHINESE) {
    // Load Fonts
#ifdef _WIN32
    std::string default_font_path = "c:/windows/fonts/simhei.ttf";
    std::ifstream font_path_f(default_font_path.c_str());
    std::string font_path =
        font_path_f.good() ? "c:/windows/fonts/simhei.ttf" : "";
    if (!font_path.empty()) {
      io.Fonts->AddFontFromFileTTF(font_path.c_str(), 13.0f, NULL,
                                   io.Fonts->GetGlyphRangesChineseFull());
    }
#elif __APPLE__
    std::string default_font_path = "/System/Library/Fonts/PingFang.ttc";
    std::ifstream font_path_f(default_font_path.c_str());
    std::string font_path =
        font_path_f.good() ? "/System/Library/Fonts/PingFang.ttc" : "";
    if (!font_path.empty()) {
      io.Fonts->AddFontFromFileTTF(font_path.c_str(), 13.0f, NULL,
                                   io.Fonts->GetGlyphRangesChineseFull());
    }
#elif __linux__
    io.Fonts->AddFontFromFileTTF("c:/windows/fonts/msyh.ttc", 13.0f, NULL,
                                 io.Fonts->GetGlyphRangesChineseFull());
#endif
  }

  // Setup Dear ImGui style
  // ImGui::StyleColorsDark();
  ImGui::StyleColorsLight();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(main_window_, sdl_renderer_);
  ImGui_ImplSDLRenderer2_Init(sdl_renderer_);

  // Our state
  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  mac_addr_str_ = GetMac();

  // Main loop
  while (!exit_) {
    localization_language_ = config_center_.GetLanguage();
    localization_language_index_ = (int)localization_language_;

    if (localization_language_index_last_ != localization_language_index_) {
      LOG_ERROR("localization_language_: {}",
                localization_language_index_ == 0 ? "zh" : "en");
      localization_language_index_last_ = localization_language_index_;
    }

    connect_button_label_ = localization::connect[localization_language_index_];
    fullscreen_button_label_ =
        localization::fullscreen[localization_language_index_];

    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (connection_established_ && !menu_hovered_) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    // main window layout
    {
      ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Once);

      if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
        ImGui::SetNextWindowSize(ImVec2(160, 210));
      } else {
        ImGui::SetNextWindowSize(ImVec2(180, 210));
      }

      if (!connection_established_) {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::Begin(localization::menu[localization_language_index_].c_str(),
                     nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoMove);
      } else {
        ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
        ImGui::Begin(localization::menu[localization_language_index_].c_str(),
                     nullptr, ImGuiWindowFlags_None);
      }

      {
        menu_hovered_ = ImGui::IsWindowHovered();

        // local
        {
          ImGui::Text(
              localization::local_id[localization_language_index_].c_str());

          ImGui::SameLine();
          ImGui::SetNextItemWidth(90);
          if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
            ImGui::SetCursorPosX(60.0f);
          } else {
            ImGui::SetCursorPosX(80.0f);
          }
          ImGui::InputText("##local_id", (char *)mac_addr_str_.c_str(),
                           mac_addr_str_.length() + 1,
                           ImGuiInputTextFlags_CharsUppercase |
                               ImGuiInputTextFlags_ReadOnly);

          ImGui::Text(
              localization::password[localization_language_index_].c_str());

          ImGui::SameLine();

          strncpy(input_password_tmp_, input_password_,
                  sizeof(input_password_));
          ImGui::SetNextItemWidth(90);
          if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
            ImGui::SetCursorPosX(60.0f);
          } else {
            ImGui::SetCursorPosX(80.0f);
          }
          ImGui::InputTextWithHint(
              "##server_pwd",
              localization::max_password_len[localization_language_index_]
                  .c_str(),
              input_password_, IM_ARRAYSIZE(input_password_),
              ImGuiInputTextFlags_CharsNoBlank);

          if (strcmp(input_password_tmp_, input_password_)) {
            cd_cache_file_ = fopen("cache.cd", "w+");
            if (cd_cache_file_) {
              fseek(cd_cache_file_, 0, SEEK_SET);
              strncpy(cd_cache_.password, input_password_,
                      sizeof(input_password_));
              fwrite(&cd_cache_.password, sizeof(cd_cache_.password), 1,
                     cd_cache_file_);
              fclose(cd_cache_file_);
            }
          }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // remote
        {
          ImGui::Text(
              localization::remote_id[localization_language_index_].c_str());

          ImGui::SameLine();
          ImGui::SetNextItemWidth(90);
          if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
            ImGui::SetCursorPosX(60.0f);
          } else {
            ImGui::SetCursorPosX(80.0f);
          }
          ImGui::InputTextWithHint("##remote_id_", mac_addr_str_.c_str(),
                                   remote_id_, IM_ARRAYSIZE(remote_id_),
                                   ImGuiInputTextFlags_CharsUppercase |
                                       ImGuiInputTextFlags_CharsNoBlank);

          ImGui::Spacing();

          ImGui::Text(
              localization::password[localization_language_index_].c_str());

          ImGui::SameLine();
          ImGui::SetNextItemWidth(90);

          if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
            ImGui::SetCursorPosX(60.0f);
          } else {
            ImGui::SetCursorPosX(80.0f);
          }

          ImGui::InputTextWithHint(
              "##client_pwd",
              localization::max_password_len[localization_language_index_]
                  .c_str(),
              client_password_, IM_ARRAYSIZE(client_password_),
              ImGuiInputTextFlags_CharsNoBlank);

          ImGui::Spacing();
          ImGui::Separator();
          ImGui::Spacing();

          if (ImGui::Button(connect_button_label_.c_str())) {
            LOG_ERROR("Click connect button");
            int ret = -1;
            if ("ClientSignalConnected" == client_signal_status_str_) {
              if (connect_button_label_ ==
                      localization::connect[localization_language_index_] &&
                  !connection_established_) {
                std::string user_id = "C-" + mac_addr_str_;

              } else if (connect_button_label_ ==
                             localization::disconnect
                                 [localization_language_index_] &&
                         connection_established_) {
              }

              if (0 == ret) {
                connect_button_pressed_ = !connect_button_pressed_;

                connect_button_label_ =
                    connect_button_pressed_
                        ? localization::disconnect[localization_language_index_]

                        : localization::connect[localization_language_index_];
              }
            }
          }
        }
      }

      ImGui::Spacing();

      ImGui::Separator();

      ImGui::Spacing();

      if (ImGui::Button(fullscreen_button_label_.c_str())) {
        if (fullscreen_button_label_ ==
            localization::fullscreen[localization_language_index_]) {
          SDL_SetWindowFullscreen(main_window_, SDL_WINDOW_FULLSCREEN_DESKTOP);
        } else {
          SDL_SetWindowFullscreen(main_window_, SDL_FALSE);
        }
        fullscreen_button_pressed_ = !fullscreen_button_pressed_;

        fullscreen_button_label_ =
            fullscreen_button_pressed_
                ? localization::exit_fullscreen[localization_language_index_]

                : localization::fullscreen[localization_language_index_];
      }
      ImGui::End();
    }

    // Rendering
    ImGui::Render();
    SDL_RenderSetScale(sdl_renderer_, io.DisplayFramebufferScale.x,
                       io.DisplayFramebufferScale.y);

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) {
        exit_ = true;
      } else if (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_RESIZED) {
        int window_w_last = main_window_width_;
        int window_h_last = main_window_height_;

        SDL_GetWindowSize(main_window_, &main_window_width_,
                          &main_window_height_);

        int w_change_ratio = abs(main_window_width_ - window_w_last) / 16;
        int h_change_ratio = abs(main_window_height_ - window_h_last) / 9;

        if (w_change_ratio > h_change_ratio) {
          main_window_height_ = main_window_width_ * 9 / 16;
        } else {
          main_window_width_ = main_window_height_ * 16 / 9;
        }

        SDL_SetWindowSize(main_window_, main_window_width_,
                          main_window_height_);
      } else if (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_CLOSE &&
                 event.window.windowID == SDL_GetWindowID(main_window_)) {
        exit_ = true;
      } else if (event.type == REFRESH_EVENT) {
        sdlRect.x = 0;
        sdlRect.y = 0;
        sdlRect.w = main_window_width_;
        sdlRect.h = main_window_height_;

        // SDL_UpdateTexture(sdl_texture_, NULL, dst_buffer, pixel_w);
      } else {
      }
    }

    SDL_RenderClear(sdl_renderer_);
    SDL_RenderCopy(sdl_renderer_, sdl_texture_, NULL, &sdlRect);

    if (!connection_established_ || !received_frame_) {
      SDL_RenderClear(sdl_renderer_);
      SDL_SetRenderDrawColor(
          sdl_renderer_, (Uint8)(clear_color.x * 0), (Uint8)(clear_color.y * 0),
          (Uint8)(clear_color.z * 0), (Uint8)(clear_color.w * 0));
    }

    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(sdl_renderer_);

    frame_count_++;
    end_time_ = SDL_GetTicks();
    elapsed_time_ = end_time_ - start_time_;
    if (elapsed_time_ >= 1000) {
      fps_ = frame_count_ / (elapsed_time_ / 1000);
      frame_count_ = 0;
      window_title = "Remote Desk Client FPS [" + std::to_string(fps_) +
                     "] status [" + server_signal_status_str_ + "|" +
                     client_signal_status_str_ + "|" +
                     server_connection_status_str_ + "|" +
                     client_connection_status_str_ + "]";
      // For MacOS, UI frameworks can only be called from the main thread
      SDL_SetWindowTitle(main_window_, window_title.c_str());
      start_time_ = end_time_;
    }
  }

  // Cleanup
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyRenderer(sdl_renderer_);
  SDL_DestroyWindow(main_window_);

  SDL_CloseAudio();
  SDL_Quit();

  return 0;
}