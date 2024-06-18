#include "main_window.h"

#include <fstream>
#include <iostream>
#include <string>

#include "IconsFontAwesome6.h"
#include "device_controller_factory.h"
#include "layout_style.h"
#include "localization.h"
#include "log.h"
#include "platform.h"
#include "screen_capturer_factory.h"

// Refresh Event
#define REFRESH_EVENT (SDL_USEREVENT + 1)
#define NV12_BUFFER_SIZE 1280 * 720 * 3 / 2

MainWindow::MainWindow() {}

MainWindow::~MainWindow() {}

int MainWindow::SaveSettingsIntoCacheFile() {
  cd_cache_file_ = fopen("cache.cd", "w+");
  if (!cd_cache_file_) {
    return -1;
  }

  fseek(cd_cache_file_, 0, SEEK_SET);
  strncpy(cd_cache_.password, input_password_, sizeof(input_password_));
  memcpy(&cd_cache_.language, &language_button_value_,
         sizeof(language_button_value_));
  memcpy(&cd_cache_.video_quality, &video_quality_button_value_,
         sizeof(video_quality_button_value_));
  memcpy(&cd_cache_.video_encode_format, &video_encode_format_button_value_,
         sizeof(video_encode_format_button_value_));
  memcpy(&cd_cache_.enable_hardware_video_codec, &enable_hardware_video_codec_,
         sizeof(enable_hardware_video_codec_));
  fwrite(&cd_cache_, sizeof(cd_cache_), 1, cd_cache_file_);
  fclose(cd_cache_file_);

  return 0;
}

int MainWindow::LoadSettingsIntoCacheFile() {
  cd_cache_file_ = fopen("cache.cd", "r+");
  if (!cd_cache_file_) {
    return -1;
  }

  fseek(cd_cache_file_, 0, SEEK_SET);
  fread(&cd_cache_, sizeof(cd_cache_), 1, cd_cache_file_);
  fclose(cd_cache_file_);
  strncpy(input_password_, cd_cache_.password, sizeof(cd_cache_.password));
  language_button_value_ = cd_cache_.language;
  video_quality_button_value_ = cd_cache_.video_quality;
  video_encode_format_button_value_ = cd_cache_.video_encode_format;
  enable_hardware_video_codec_ = cd_cache_.enable_hardware_video_codec;

  return 0;
}

int MainWindow::StartScreenCapture() {
  screen_capturer_ = (ScreenCapturer *)screen_capturer_factory_->Create();
  ScreenCapturer::RECORD_DESKTOP_RECT rect;
  rect.left = 0;
  rect.top = 0;
  rect.right = screen_width_;
  rect.bottom = screen_height_;
  last_frame_time_ = std::chrono::high_resolution_clock::now();

  int screen_capturer_init_ret = screen_capturer_->Init(
      rect, 60,
      [this](unsigned char *data, int size, int width, int height) -> void {
        auto now_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration = now_time - last_frame_time_;
        auto tc = duration.count() * 1000;

        if (tc >= 0) {
          SendData(peer_, DATA_TYPE::VIDEO, (const char *)data,
                   NV12_BUFFER_SIZE);
          last_frame_time_ = now_time;
        }
      });

  if (0 == screen_capturer_init_ret) {
    screen_capturer_->Start();
  } else {
    screen_capturer_->Destroy();
    delete screen_capturer_;
    screen_capturer_ = nullptr;
  }

  return 0;
}

int MainWindow::StopScreenCapture() {
  if (screen_capturer_) {
    LOG_INFO("Destroy screen capturer")
    screen_capturer_->Destroy();
    delete screen_capturer_;
    screen_capturer_ = nullptr;
  }

  return 0;
}

int MainWindow::StartMouseControl() {
  device_controller_factory_ = new DeviceControllerFactory();
  mouse_controller_ = (MouseController *)device_controller_factory_->Create(
      DeviceControllerFactory::Device::Mouse);
  int mouse_controller_init_ret =
      mouse_controller_->Init(screen_width_, screen_height_);
  if (0 != mouse_controller_init_ret) {
    LOG_INFO("Destroy mouse controller")
    mouse_controller_->Destroy();
    mouse_controller_ = nullptr;
  }

  return 0;
}

int MainWindow::StopMouseControl() {
  if (mouse_controller_) {
    mouse_controller_->Destroy();
    delete mouse_controller_;
    mouse_controller_ = nullptr;
  }
  return 0;
}

int MainWindow::CreateConnectionPeer() {
  mac_addr_str_ = GetMac();

  params_.use_cfg_file = false;
  params_.signal_server_ip = "150.158.81.30";
  params_.signal_server_port = 9099;
  params_.stun_server_ip = "150.158.81.30";
  params_.stun_server_port = 3478;
  params_.turn_server_ip = "150.158.81.30";
  params_.turn_server_port = 3478;
  params_.turn_server_username = "dijunkun";
  params_.turn_server_password = "dijunkunpw";
  params_.hardware_acceleration = config_center_.IsHardwareVideoCodec();
  params_.av1_encoding = config_center_.GetVideoEncodeFormat() ==
                                 ConfigCenter::VIDEO_ENCODE_FORMAT::AV1
                             ? true
                             : false;
  params_.on_receive_video_buffer = OnReceiveVideoBufferCb;
  params_.on_receive_audio_buffer = OnReceiveAudioBufferCb;
  params_.on_receive_data_buffer = OnReceiveDataBufferCb;
  params_.on_signal_status = OnSignalStatusCb;
  params_.on_connection_status = OnConnectionStatusCb;
  params_.user_data = this;

  peer_ = CreatePeer(&params_);
  if (peer_) {
    LOG_INFO("Create peer instance successful");
    local_id_ = mac_addr_str_;
    Init(peer_, local_id_.c_str());
    LOG_INFO("Peer init finish");
  } else {
    LOG_INFO("Create peer instance failed");
  }

  return 0;
}

int MainWindow::Run() {
  LoadSettingsIntoCacheFile();

  localization_language_ = (ConfigCenter::LANGUAGE)language_button_value_;
  localization_language_index_ = language_button_value_;

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
  SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_ALLOW_HIGHDPI);
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
      io.Fonts->AddFontFromFileTTF(font_path.c_str(), 30.0f, NULL,
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

  ImFontConfig config;
  config.MergeMode = true;
  config.GlyphMinAdvanceX =
      13.0f;  // Use if you want to make the icon monospaced
  static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
  io.Fonts->AddFontFromFileTTF("fonts/fa-solid-900.ttf", 30.0f, &config,
                               icon_ranges);
  io.Fonts->Build();

  // Setup Dear ImGui style
  // ImGui::StyleColorsDark();
  ImGui::StyleColorsLight();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(main_window_, sdl_renderer_);
  ImGui_ImplSDLRenderer2_Init(sdl_renderer_);

  CreateConnectionPeer();

  {
    nv12_buffer_ = new char[NV12_BUFFER_SIZE];

    // Screen capture
    screen_capturer_factory_ = new ScreenCapturerFactory();

    // Mouse control
    device_controller_factory_ = new DeviceControllerFactory();
  }

  // Main loop
  while (!exit_) {
    if (SignalStatus::SignalConnected == signal_status_ &&
        !is_create_connection_) {
      is_create_connection_ =
          CreateConnection(peer_, mac_addr_str_.c_str(), input_password_)
              ? false
              : true;
      LOG_INFO("Connected with signal server, create p2p connection");
    }

    if (!inited_ ||
        localization_language_index_last_ != localization_language_index_) {
      connect_button_label_ =
          connect_button_pressed_
              ? localization::disconnect[localization_language_index_]
              : localization::connect[localization_language_index_];
      fullscreen_button_label_ =
          fullscreen_button_pressed_
              ? localization::exit_fullscreen[localization_language_index_]
              : localization::fullscreen[localization_language_index_];

      mouse_control_button_label_ =
          mouse_control_button_pressed_
              ? localization::release_mouse[localization_language_index_]
              : localization::control_mouse[localization_language_index_];

      settings_button_label_ =
          localization::settings[localization_language_index_];
      inited_ = true;
      localization_language_index_last_ = localization_language_index_;
    }

    if (start_screen_capture_ && !screen_capture_is_started_) {
      StartScreenCapture();
      screen_capture_is_started_ = true;
    } else if (!start_screen_capture_ && screen_capture_is_started_) {
      StopScreenCapture();
      screen_capture_is_started_ = false;
    }

    if (start_mouse_control_ && !mouse_control_is_started_) {
      StartMouseControl();
      mouse_control_is_started_ = true;
    } else if (!start_mouse_control_ && mouse_control_is_started_) {
      StopMouseControl();
      mouse_control_is_started_ = false;
    }

    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // main window layout
    {
      ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Once);

      if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
        ImGui::SetNextWindowSize(
            ImVec2(MENU_WINDOW_WIDTH_CN, MENU_WINDOW_HEIGHT_CN));
      } else {
        ImGui::SetNextWindowSize(
            ImVec2(MENU_WINDOW_WIDTH_EN, MENU_WINDOW_HEIGHT_EN));
      }

      if (!connection_established_) {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::Begin(localization::menu[localization_language_index_].c_str(),
                     nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
        ImGui::SetWindowFontScale(0.5f);
        ImGui::Text(u8"本桌面");
      } else {
        // ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
        ImGui::Begin(localization::menu[localization_language_index_].c_str(),
                     nullptr, ImGuiWindowFlags_None);
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      {
        // local
        {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0.3));
          ImGui::Text(
              localization::local_id[localization_language_index_].c_str());
          ImGui::PopStyleColor();

          ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH);
          ImGui::Spacing();
          ImGui::SetWindowFontScale(1.0f);
          ImGui::InputText("##local_id", (char *)mac_addr_str_.c_str(),
                           mac_addr_str_.length() + 1,
                           ImGuiInputTextFlags_CharsUppercase |
                               ImGuiInputTextFlags_ReadOnly);
          ImGui::SetWindowFontScale(0.5f);
          ImGui::Text(
              localization::password[localization_language_index_].c_str());

          strncpy(input_password_tmp_, input_password_,
                  sizeof(input_password_));
          ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH);
          ImGui::Spacing();
          ImGui::SetWindowFontScale(1.0f);
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0.3));
          ImGui::InputTextWithHint(
              "##server_pwd",
              localization::max_password_len[localization_language_index_]
                  .c_str(),
              input_password_, IM_ARRAYSIZE(input_password_),
              show_password_ ? ImGuiInputTextFlags_CharsNoBlank
                             : ImGuiInputTextFlags_CharsNoBlank |
                                   ImGuiInputTextFlags_Password);
          ImGui::PopStyleColor();
          ImGui::SameLine();
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));

          if (ImGui::Button(show_password_ ? ICON_FA_EYE : ICON_FA_EYE_SLASH,
                            ImVec2(50, 35))) {
            show_password_ = !show_password_;
          }
          ImGui::PopStyleColor(3);

          ImGui::SameLine();
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
          if (!regenerate_password_) {
            if (ImGui::Button(ICON_FA_ARROWS_ROTATE, ImVec2(38, 38))) {
              regenerate_password_ = true;
            }
          } else {
            regenerate_password_frame_count_++;
            if (ImGui::Button(ICON_FA_SPINNER)) {
            }
            if (regenerate_password_frame_count_ == 30) {
              regenerate_password_frame_count_ = 0;
              regenerate_password_ = false;
            }
          }
          ImGui::PopStyleColor(3);

          if (strcmp(input_password_tmp_, input_password_)) {
            SaveSettingsIntoCacheFile();
          }
        }
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::SetWindowFontScale(0.5f);
      // Mouse control
      if (ImGui::Button(mouse_control_button_label_.c_str())) {
        if (mouse_control_button_label_ ==
                localization::control_mouse[localization_language_index_] &&
            connection_established_) {
          mouse_control_button_pressed_ = true;
          control_mouse_ = true;
          mouse_control_button_label_ =
              localization::release_mouse[localization_language_index_];
        } else {
          control_mouse_ = false;
          mouse_control_button_label_ =
              localization::control_mouse[localization_language_index_];
        }
        mouse_control_button_pressed_ = !mouse_control_button_pressed_;
      }

      ImGui::SameLine();
      // Fullscreen
      if (ImGui::Button(fullscreen_button_label_.c_str())) {
        if (fullscreen_button_label_ ==
            localization::fullscreen[localization_language_index_]) {
          main_window_width_before_fullscreen_ = main_window_width_;
          main_window_height_before_fullscreen_ = main_window_height_;
          SDL_SetWindowFullscreen(main_window_, SDL_WINDOW_FULLSCREEN_DESKTOP);
          fullscreen_button_label_ =
              localization::exit_fullscreen[localization_language_index_];
        } else {
          SDL_SetWindowFullscreen(main_window_, SDL_FALSE);
          SDL_SetWindowSize(main_window_, main_window_width_before_fullscreen_,
                            main_window_height_before_fullscreen_);
          main_window_width_ = main_window_width_before_fullscreen_;
          main_window_height_ = main_window_height_before_fullscreen_;
          fullscreen_button_label_ =
              localization::fullscreen[localization_language_index_];
        }
        fullscreen_button_pressed_ = !fullscreen_button_pressed_;
      }

      ImGui::Spacing();

      ImGui::Separator();

      ImGui::Spacing();

      ImGui::SetWindowFontScale(0.5f);
      std::string gear = ICON_FA_GEAR + settings_button_label_;
      if (ImGui::Button(gear.c_str(), ImVec2(55, 25))) {
        settings_button_pressed_ = !settings_button_pressed_;
        settings_window_pos_reset_ = true;
      }
      ImGui::SetWindowFontScale(1.0f);

      if (settings_button_pressed_) {
        if (settings_window_pos_reset_) {
          const ImGuiViewport *viewport = ImGui::GetMainViewport();
          if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
            ImGui::SetNextWindowPos(
                ImVec2((viewport->WorkSize.x - viewport->WorkPos.x -
                        SETTINGS_WINDOW_WIDTH_CN) /
                           2,
                       (viewport->WorkSize.y - viewport->WorkPos.y -
                        SETTINGS_WINDOW_HEIGHT_CN) /
                           2));

            ImGui::SetNextWindowSize(
                ImVec2(SETTINGS_WINDOW_WIDTH_CN, SETTINGS_WINDOW_HEIGHT_CN));
          } else {
            ImGui::SetNextWindowPos(
                ImVec2((viewport->WorkSize.x - viewport->WorkPos.x -
                        SETTINGS_WINDOW_WIDTH_EN) /
                           2,
                       (viewport->WorkSize.y - viewport->WorkPos.y -
                        SETTINGS_WINDOW_HEIGHT_EN) /
                           2));

            ImGui::SetNextWindowSize(
                ImVec2(SETTINGS_WINDOW_WIDTH_EN, SETTINGS_WINDOW_HEIGHT_EN));
          }

          settings_window_pos_reset_ = false;
        }

        ImGui::SetWindowFontScale(0.5f);
        // Settings
        ImGui::Begin(
            localization::settings[localization_language_index_].c_str(),
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowFontScale(0.5f);
        {
          const char *language_items[] = {
              localization::language_zh[localization_language_index_].c_str(),
              localization::language_en[localization_language_index_].c_str()};

          ImGui::SetCursorPosY(32);
          ImGui::Text(
              localization::language[localization_language_index_].c_str());
          if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
            ImGui::SetCursorPosX(LANGUAGE_SELECT_WINDOW_PADDING_CN);
          } else {
            ImGui::SetCursorPosX(LANGUAGE_SELECT_WINDOW_PADDING_EN);
          }
          ImGui::SetCursorPosY(30);
          ImGui::SetNextItemWidth(SETTINGS_SELECT_WINDOW_WIDTH);

          ImGui::Combo("##language", &language_button_value_, language_items,
                       IM_ARRAYSIZE(language_items));
        }

        ImGui::Separator();

        {
          const char *video_quality_items[] = {
              localization::video_quality_high[localization_language_index_]
                  .c_str(),
              localization::video_quality_medium[localization_language_index_]
                  .c_str(),
              localization::video_quality_low[localization_language_index_]
                  .c_str()};

          ImGui::SetCursorPosY(62);
          ImGui::Text(localization::video_quality[localization_language_index_]
                          .c_str());

          if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
            ImGui::SetCursorPosX(VIDEO_QUALITY_SELECT_WINDOW_PADDING_CN);
          } else {
            ImGui::SetCursorPosX(VIDEO_QUALITY_SELECT_WINDOW_PADDING_EN);
          }
          ImGui::SetCursorPosY(60);
          ImGui::SetNextItemWidth(SETTINGS_SELECT_WINDOW_WIDTH);

          ImGui::Combo("##video_quality", &video_quality_button_value_,
                       video_quality_items, IM_ARRAYSIZE(video_quality_items));
        }

        ImGui::Separator();

        {
          const char *video_encode_format_items[] = {
              localization::av1[localization_language_index_].c_str(),
              localization::h264[localization_language_index_].c_str()};

          ImGui::SetCursorPosY(92);
          ImGui::Text(
              localization::video_encode_format[localization_language_index_]
                  .c_str());

          if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
            ImGui::SetCursorPosX(VIDEO_ENCODE_FORMAT_SELECT_WINDOW_PADDING_CN);
          } else {
            ImGui::SetCursorPosX(VIDEO_ENCODE_FORMAT_SELECT_WINDOW_PADDING_EN);
          }
          ImGui::SetCursorPosY(90);
          ImGui::SetNextItemWidth(SETTINGS_SELECT_WINDOW_WIDTH);

          ImGui::Combo("##video_encode_format",
                       &video_encode_format_button_value_,
                       video_encode_format_items,
                       IM_ARRAYSIZE(video_encode_format_items));
        }

        ImGui::Separator();

        {
          ImGui::SetCursorPosY(122);
          ImGui::Text(localization::enable_hardware_video_codec
                          [localization_language_index_]
                              .c_str());

          if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
            ImGui::SetCursorPosX(
                ENABLE_HARDWARE_VIDEO_CODEC_CHECKBOX_PADDING_CN);
          } else {
            ImGui::SetCursorPosX(
                ENABLE_HARDWARE_VIDEO_CODEC_CHECKBOX_PADDING_EN);
          }
          ImGui::SetCursorPosY(120);
          ImGui::Checkbox("##enable_hardware_video_codec",
                          &enable_hardware_video_codec_);
        }

        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(SETTINGS_OK_BUTTON_PADDING_CN);
        } else {
          ImGui::SetCursorPosX(SETTINGS_OK_BUTTON_PADDING_EN);
        }
        ImGui::SetCursorPosY(160.0f);

        // OK
        if (ImGui::Button(
                localization::ok[localization_language_index_].c_str())) {
          settings_button_pressed_ = false;

          // Language
          if (language_button_value_ == 0) {
            config_center_.SetLanguage(ConfigCenter::LANGUAGE::CHINESE);
          } else {
            config_center_.SetLanguage(ConfigCenter::LANGUAGE::ENGLISH);
          }
          language_button_value_last_ = language_button_value_;
          localization_language_ =
              (ConfigCenter::LANGUAGE)language_button_value_;
          localization_language_index_ = language_button_value_;
          LOG_INFO("Set localization language: {}",
                   localization_language_index_ == 0 ? "zh" : "en");

          // Video quality
          if (video_quality_button_value_ == 0) {
            config_center_.SetVideoQuality(ConfigCenter::VIDEO_QUALITY::HIGH);
          } else if (video_quality_button_value_ == 1) {
            config_center_.SetVideoQuality(ConfigCenter::VIDEO_QUALITY::MEDIUM);
          } else {
            config_center_.SetVideoQuality(ConfigCenter::VIDEO_QUALITY::LOW);
          }
          video_quality_button_value_last_ = video_quality_button_value_;

          // Video encode format
          if (video_encode_format_button_value_ == 0) {
            config_center_.SetVideoEncodeFormat(
                ConfigCenter::VIDEO_ENCODE_FORMAT::AV1);
          } else if (video_encode_format_button_value_ == 1) {
            config_center_.SetVideoEncodeFormat(
                ConfigCenter::VIDEO_ENCODE_FORMAT::H264);
          }
          video_encode_format_button_value_last_ =
              video_encode_format_button_value_;

          // Hardware video codec
          if (enable_hardware_video_codec_) {
            config_center_.SetHardwareVideoCodec(true);
          } else {
            config_center_.SetHardwareVideoCodec(false);
          }
          enable_hardware_video_codec_last_ = enable_hardware_video_codec_;

          SaveSettingsIntoCacheFile();
          settings_window_pos_reset_ = true;

          // Recreate peer instance
          LoadSettingsIntoCacheFile();

          // Recreate peer instance
          {
            DestroyPeer(peer_);
            CreateConnectionPeer();
            LOG_INFO("Recreate peer instance successful");
          }
        }
        ImGui::SameLine();
        // Cancel
        if (ImGui::Button(
                localization::cancel[localization_language_index_].c_str())) {
          settings_button_pressed_ = false;
          if (language_button_value_ != language_button_value_last_) {
            language_button_value_ = language_button_value_last_;
          }

          if (video_quality_button_value_ != video_quality_button_value_last_) {
            video_quality_button_value_ = video_quality_button_value_last_;
          }

          if (video_encode_format_button_value_ !=
              video_encode_format_button_value_last_) {
            video_encode_format_button_value_ =
                video_encode_format_button_value_last_;
          }

          if (enable_hardware_video_codec_ !=
              enable_hardware_video_codec_last_) {
            enable_hardware_video_codec_ = enable_hardware_video_codec_last_;
          }

          settings_window_pos_reset_ = true;
        }

        ImGui::End();
      }

      ImGui::End();
    }

    {
      ImGui::SetNextWindowPos(ImVec2(MENU_WINDOW_WIDTH_CN, 0),
                              ImGuiCond_Always);
      if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
        ImGui::SetNextWindowSize(
            ImVec2(MENU_WINDOW_WIDTH_CN, MENU_WINDOW_HEIGHT_CN));
      } else {
        ImGui::SetNextWindowSize(
            ImVec2(MENU_WINDOW_WIDTH_EN, MENU_WINDOW_HEIGHT_EN));
      }
      ImGui::Begin("test", nullptr,
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

      ImGui::SetWindowFontScale(0.5f);
      ImGui::Text(u8"远程控制桌面");

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      {
        ImGui::Text(
            localization::remote_id[localization_language_index_].c_str());

        ImGui::SameLine();
        ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH);
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(INPUT_WINDOW_PADDING_CN);
        } else {
          ImGui::SetCursorPosX(INPUT_WINDOW_PADDING_EN);
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::InputTextWithHint("##remote_id_", mac_addr_str_.c_str(),
                                 remote_id_, IM_ARRAYSIZE(remote_id_),
                                 ImGuiInputTextFlags_CharsUppercase |
                                     ImGuiInputTextFlags_CharsNoBlank);

        ImGui::Spacing();
        ImGui::SetWindowFontScale(0.5f);
        ImGui::Text(
            localization::password[localization_language_index_].c_str());

        ImGui::SameLine();
        ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH);

        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(INPUT_WINDOW_PADDING_CN);
        } else {
          ImGui::SetCursorPosX(INPUT_WINDOW_PADDING_EN);
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::InputTextWithHint(
            "##client_pwd",
            localization::max_password_len[localization_language_index_]
                .c_str(),
            client_password_, IM_ARRAYSIZE(client_password_),
            ImGuiInputTextFlags_CharsNoBlank);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(connect_button_label_.c_str(),
                          ImVec2(MENU_WINDOW_WIDTH_CN - 16, 40)) ||
            rejoin_) {
          int ret = -1;
          if ("SignalConnected" == signal_status_str_) {
            if (connect_button_label_ ==
                    localization::connect[localization_language_index_] &&
                !connection_established_ && strlen(remote_id_)) {
              if (remote_id_ == local_id_ && !peer_reserved_) {
                peer_reserved_ = CreatePeer(&params_);
                if (peer_reserved_) {
                  LOG_INFO("Create peer[reserved] instance successful");
                  std::string local_id = "C-" + mac_addr_str_;
                  Init(peer_reserved_, local_id.c_str());
                  LOG_INFO("Peer[reserved] init finish");
                } else {
                  LOG_INFO("Create peer[reserved] instance failed");
                }
              }
              ret = JoinConnection(peer_reserved_ ? peer_reserved_ : peer_,
                                   remote_id_, client_password_);
              if (0 == ret) {
                if (!peer_reserved_) {
                  is_client_mode_ = true;
                }
                rejoin_ = false;
              } else {
                rejoin_ = true;
              }

            } else if (connect_button_label_ ==
                           localization::disconnect
                               [localization_language_index_] &&
                       connection_established_) {
              ret = LeaveConnection(peer_reserved_ ? peer_reserved_ : peer_);

              if (0 == ret) {
                rejoin_ = false;
                memset(audio_buffer_, 0, 960);
                connection_established_ = false;
                received_frame_ = false;
                is_client_mode_ = false;
              }
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
      ImGui::End();
    }

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
        sdl_rect_.x = 0;
        sdl_rect_.y = 0;
        sdl_rect_.w = main_window_width_;
        sdl_rect_.h = main_window_height_;

        // SDL_UpdateTexture(sdl_texture_, NULL, dst_buffer_, 1280);
      } else {
        if (connection_established_) {
          ProcessMouseKeyEven(event);
        }
      }
    }

    // Rendering
    ImGui::Render();
    SDL_RenderSetScale(sdl_renderer_, io.DisplayFramebufferScale.x,
                       io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(sdl_renderer_, 255, 255, 255, 255);
    SDL_RenderClear(sdl_renderer_);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(sdl_renderer_);

    frame_count_++;
    end_time_ = SDL_GetTicks();
    elapsed_time_ = end_time_ - start_time_;
    if (elapsed_time_ >= 1000) {
      fps_ = frame_count_ / (elapsed_time_ / 1000);
      frame_count_ = 0;
      window_title = "Remote Desk Client FPS [" + std::to_string(fps_) +
                     "] status [" + connection_status_str_ + "|" +
                     connection_status_str_ + "]";
      // For MacOS, UI frameworks can only be called from the main thread
      SDL_SetWindowTitle(main_window_, window_title.c_str());
      start_time_ = end_time_;
    }
  }

  // Cleanup
  if (is_create_connection_) {
    LeaveConnection(peer_);
    is_client_mode_ = false;
  }

  if (peer_) {
    DestroyPeer(peer_);
  }

  if (peer_reserved_) {
    DestroyPeer(peer_reserved_);
  }

  SDL_CloseAudioDevice(output_dev_);
  SDL_CloseAudioDevice(input_dev_);

  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyRenderer(sdl_renderer_);
  SDL_DestroyWindow(main_window_);

  SDL_CloseAudio();
  SDL_Quit();

  return 0;
}