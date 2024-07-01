#include "render.h"

#include <fstream>
#include <iostream>
#include <string>

#include "IconsFontAwesome6.h"
#include "OPPOSans-Regular.h"
#include "device_controller_factory.h"
#include "fa-solid-900.h"
#include "layout_style.h"
#include "localization.h"
#include "log.h"
#include "platform.h"
#include "screen_capturer_factory.h"

// Refresh Event
#define REFRESH_EVENT (SDL_USEREVENT + 1)
#define NV12_BUFFER_SIZE 1280 * 720 * 3 / 2

Render::Render() {}

Render::~Render() {}

int Render::SaveSettingsIntoCacheFile() {
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

int Render::LoadSettingsIntoCacheFile() {
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

int Render::StartScreenCapture() {
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

int Render::StopScreenCapture() {
  if (screen_capturer_) {
    LOG_INFO("Destroy screen capturer")
    screen_capturer_->Destroy();
    delete screen_capturer_;
    screen_capturer_ = nullptr;
  }

  return 0;
}

int Render::StartMouseControl() {
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

int Render::StopMouseControl() {
  if (mouse_controller_) {
    mouse_controller_->Destroy();
    delete mouse_controller_;
    mouse_controller_ = nullptr;
  }
  return 0;
}

int Render::CreateConnectionPeer() {
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

int Render::Run() {
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
  SDL_WindowFlags window_flags =
      (SDL_WindowFlags)(SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
  main_window_ = SDL_CreateWindow("Remote Desk", SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED, main_window_width_,
                                  main_window_height_, window_flags);

  main_renderer_ = SDL_CreateRenderer(
      main_window_, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
  if (main_renderer_ == nullptr) {
    LOG_ERROR("1 Error creating SDL_Renderer");
    return 0;
  }

  stream_pixformat_ = SDL_PIXELFORMAT_NV12;

  stream_texture_ = SDL_CreateTexture(main_renderer_, stream_pixformat_,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      texture_width_, texture_height_);

  stream_render_rect_.x = 0;
  stream_render_rect_.y = 0;
  stream_render_rect_.w = main_window_width_;
  stream_render_rect_.h = main_window_height_;

  SDL_DisplayMode DM;
  SDL_GetCurrentDisplayMode(0, &DM);
  screen_width_ = DM.w;
  screen_height_ = DM.h;

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();

  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

  // Load Fonts
  io.Fonts->AddFontFromMemoryTTF(OPPOSans_Regular_ttf,
                                 sizeof(OPPOSans_Regular_ttf), 32.0f, NULL,
                                 io.Fonts->GetGlyphRangesChineseFull());

  ImFontConfig config;
  config.MergeMode = true;
  config.GlyphMinAdvanceX =
      13.0f;  // Use if you want to make the icon monospaced
  static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
  io.Fonts->AddFontFromMemoryTTF(fa_solid_900_ttf, sizeof(fa_solid_900_ttf),
                                 30.0f, &config, icon_ranges);

  io.Fonts->Build();

  SDL_GL_GetDrawableSize(main_window_, &main_window_width_real_,
                         &main_window_height_real_);
  dpi_scaling_w_ = (float)main_window_width_real_ / (float)main_window_width_;
  dpi_scaling_h_ = (float)main_window_width_real_ / (float)main_window_width_;

  LOG_INFO("Use dpi scaling [{}x{}]", dpi_scaling_w_, dpi_scaling_h_);

  SDL_RenderSetScale(main_renderer_, dpi_scaling_w_, dpi_scaling_h_);

  // Setup Dear ImGui style
  // ImGui::StyleColorsDark();
  ImGui::StyleColorsLight();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(main_window_, main_renderer_);
  ImGui_ImplSDLRenderer2_Init(main_renderer_);

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

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(main_window_width_,
               streaming_ ? menu_window_height_ : main_window_height_),
        ImGuiCond_Always);
    ImGui::Begin("Render", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

    if (!streaming_) {
      MainWindow();
    } else {
      ControlWindow();
    }

    ImGui::End();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) {
        if (streaming_) {
          LOG_INFO("Return to main interface");
          streaming_ = false;
          LeaveConnection(peer_reserved_);
          rejoin_ = false;
          memset(audio_buffer_, 0, 960);
          connection_established_ = false;
          received_frame_ = false;
          is_client_mode_ = false;
          SDL_SetWindowSize(main_window_, main_window_width_last_,
                            main_window_height_last_);
          SDL_SetWindowPosition(main_window_, SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED);
          continue;
        } else {
          LOG_INFO("Quit program");
          exit_ = true;
        }
      } else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        SDL_GetWindowSize(main_window_, &main_window_width_,
                          &main_window_height_);
        if (!streaming_) {
          main_window_width_last_ = main_window_width_;
          main_window_height_last_ = main_window_height_;
        }

        if (main_window_width_ * 9 < main_window_height_ * 16) {
          stream_render_rect_.x = 0;
          stream_render_rect_.y =
              abs(main_window_height_ - main_window_width_ * 9 / 16) / 2;
          stream_render_rect_.w = main_window_width_;
          stream_render_rect_.h = main_window_width_ * 9 / 16;
        } else if (main_window_width_ * 9 > main_window_height_ * 16) {
          stream_render_rect_.x =
              abs(main_window_width_ - main_window_height_ * 16 / 9) / 2;
          stream_render_rect_.y = 0;
          stream_render_rect_.w = main_window_height_ * 16 / 9;
          stream_render_rect_.h = main_window_height_;
        } else {
          stream_render_rect_.x = 0;
          stream_render_rect_.y = 0;
          stream_render_rect_.w = main_window_width_;
          stream_render_rect_.h = main_window_height_;
        }

      } else if (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_CLOSE) {
        if (connection_established_) {
          continue;
        } else {
          exit_ = true;
        }
      } else if (event.type == REFRESH_EVENT) {
        if (stream_texture_)
          SDL_UpdateTexture(stream_texture_, NULL, dst_buffer_, 1280 * 720 * 3);
      } else {
        if (connection_established_) {
          ProcessMouseKeyEven(event);
        }
      }
    }

    // Rendering
    ImGui::Render();

    if (connection_established_ && received_frame_ && streaming_) {
      SDL_RenderClear(main_renderer_);
      SDL_RenderCopy(main_renderer_, stream_texture_, NULL,
                     &stream_render_rect_);
    }

    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(main_renderer_);

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

  SDL_DestroyRenderer(main_renderer_);
  SDL_DestroyWindow(main_window_);

  SDL_CloseAudio();
  SDL_Quit();

  return 0;
}