#include "render.h"

#include <fstream>
#include <iostream>
#include <string>

#include "IconsFontAwesome6.h"
#include "OPPOSans_Regular.h"
#include "device_controller_factory.h"
#include "fa_regular_400.h"
#include "fa_solid_900.h"
#include "layout_style.h"
#include "localization.h"
#include "platform.h"
#include "rd_log.h"
#include "screen_capturer_factory.h"

// Refresh Event
#define REFRESH_EVENT (SDL_USEREVENT + 1)
#define NV12_BUFFER_SIZE 1280 * 720 * 3 / 2

#define MOUSE_GRAB_PADDING 5

SDL_HitTestResult Render::HitTestCallback(SDL_Window *window,
                                          const SDL_Point *area, void *data) {
  Render *render = (Render *)data;
  if (!render) {
    return SDL_HITTEST_NORMAL;
  }

  int window_width, window_height;
  SDL_GetWindowSize(window, &window_width, &window_height);

  if (area->y < 30 && area->y > MOUSE_GRAB_PADDING &&
      area->x < window_width - 120 && area->x > MOUSE_GRAB_PADDING) {
    return SDL_HITTEST_DRAGGABLE;
  }

  if (!render->streaming_) {
    return SDL_HITTEST_NORMAL;
  }

  if (area->y < MOUSE_GRAB_PADDING) {
    if (area->x < MOUSE_GRAB_PADDING) {
      return SDL_HITTEST_RESIZE_TOPLEFT;
    } else if (area->x > window_width - MOUSE_GRAB_PADDING) {
      return SDL_HITTEST_RESIZE_TOPRIGHT;
    } else {
      return SDL_HITTEST_RESIZE_TOP;
    }
  } else if (area->y > window_height - MOUSE_GRAB_PADDING) {
    if (area->x < MOUSE_GRAB_PADDING) {
      return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    } else if (area->x > window_width - MOUSE_GRAB_PADDING) {
      return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    } else {
      return SDL_HITTEST_RESIZE_BOTTOM;
    }
  } else if (area->x < MOUSE_GRAB_PADDING) {
    return SDL_HITTEST_RESIZE_LEFT;
  } else if (area->x > window_width - MOUSE_GRAB_PADDING) {
    return SDL_HITTEST_RESIZE_RIGHT;
  }

  return SDL_HITTEST_NORMAL;
}

Render::Render() {}

Render::~Render() {}

int Render::SaveSettingsIntoCacheFile() {
  std::lock_guard<std::mutex> lock(cd_cache_mutex_);
  cd_cache_file_ = fopen("cache.cd", "w+");
  if (!cd_cache_file_) {
    return -1;
  }

  fseek(cd_cache_file_, 0, SEEK_SET);
  memset(&cd_cache_.client_id, 0, sizeof(cd_cache_.client_id));
  strncpy(cd_cache_.client_id, client_id_, sizeof(client_id_));
  memset(&cd_cache_.password, 0, sizeof(cd_cache_.password));
  strncpy(cd_cache_.password, password_saved_, sizeof(password_saved_));
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

  config_center_.SetLanguage((ConfigCenter::LANGUAGE)language_button_value_);
  config_center_.SetVideoQuality(
      (ConfigCenter::VIDEO_QUALITY)video_quality_button_value_);
  config_center_.SetVideoEncodeFormat(
      (ConfigCenter::VIDEO_ENCODE_FORMAT)video_encode_format_button_value_);
  config_center_.SetHardwareVideoCodec(enable_hardware_video_codec_);

  LOG_INFO("Save settings into cache file success");

  return 0;
}

int Render::LoadSettingsFromCacheFile() {
  std::lock_guard<std::mutex> lock(cd_cache_mutex_);
  cd_cache_file_ = fopen("cache.cd", "r+");
  if (!cd_cache_file_) {
    LOG_INFO("Init cache file by using default settings");
    memset(password_saved_, 0, sizeof(password_saved_));
    language_button_value_ = 0;
    video_quality_button_value_ = 0;
    video_encode_format_button_value_ = 1;
    enable_hardware_video_codec_ = false;

    config_center_.SetLanguage((ConfigCenter::LANGUAGE)language_button_value_);
    config_center_.SetVideoQuality(
        (ConfigCenter::VIDEO_QUALITY)video_quality_button_value_);
    config_center_.SetVideoEncodeFormat(
        (ConfigCenter::VIDEO_ENCODE_FORMAT)video_encode_format_button_value_);
    config_center_.SetHardwareVideoCodec(enable_hardware_video_codec_);

    return -1;
  }

  fseek(cd_cache_file_, 0, SEEK_SET);
  fread(&cd_cache_, sizeof(cd_cache_), 1, cd_cache_file_);
  fclose(cd_cache_file_);

  memset(&client_id_, 0, sizeof(client_id_));
  strncpy(client_id_, cd_cache_.client_id, sizeof(client_id_));
  strncpy(password_saved_, cd_cache_.password, sizeof(password_saved_));
  if (0 != strcmp(password_saved_, "") && 7 == sizeof(password_saved_)) {
    password_inited_ = true;
  }
  language_button_value_ = cd_cache_.language;
  video_quality_button_value_ = cd_cache_.video_quality;
  video_encode_format_button_value_ = cd_cache_.video_encode_format;
  enable_hardware_video_codec_ = cd_cache_.enable_hardware_video_codec;

  language_button_value_last_ = language_button_value_;
  video_quality_button_value_last_ = video_quality_button_value_;
  video_encode_format_button_value_last_ = video_encode_format_button_value_;
  enable_hardware_video_codec_last_ = enable_hardware_video_codec_;

  config_center_.SetLanguage((ConfigCenter::LANGUAGE)language_button_value_);
  config_center_.SetVideoQuality(
      (ConfigCenter::VIDEO_QUALITY)video_quality_button_value_);
  config_center_.SetVideoEncodeFormat(
      (ConfigCenter::VIDEO_ENCODE_FORMAT)video_encode_format_button_value_);
  config_center_.SetHardwareVideoCodec(enable_hardware_video_codec_);

  LOG_INFO("Load settings from cache file");

  return 0;
}

int Render::StartScreenCapture() {
  screen_capturer_ = (ScreenCapturer *)screen_capturer_factory_->Create();
  last_frame_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();

  int screen_capturer_init_ret = screen_capturer_->Init(
      60, [this](unsigned char *data, int size, int width, int height) -> void {
        auto now_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
        auto duration = now_time - last_frame_time_;
        if (duration >= 0 && connection_established_) {
          // SendData(peer_, DATA_TYPE::VIDEO, (const char *)data,
          //          NV12_BUFFER_SIZE);

          XVideoFrame frame;
          frame.data = (const char *)data;
          frame.size = size;
          frame.width = width;
          frame.height = height;

          SendVideoFrame(peer_, &frame);
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
    LOG_INFO("Stop screen capturer")
    screen_capturer_->Stop();
    screen_capturer_->Destroy();
    delete screen_capturer_;
    screen_capturer_ = nullptr;
  }

  return 0;
}

int Render::StartSpeakerCapture() {
  if (!speaker_capturer_) {
    speaker_capturer_ = (SpeakerCapturer *)speaker_capturer_factory_->Create();
    int speaker_capturer_init_ret = speaker_capturer_->Init(
        [this](unsigned char *data, size_t size) -> void {
          if (connection_established_) {
            SendData(peer_, DATA_TYPE::AUDIO, (const char *)data, size);
          }
        });

    if (0 != speaker_capturer_init_ret) {
      speaker_capturer_->Destroy();
      delete speaker_capturer_;
      speaker_capturer_ = nullptr;
    }
  }

  if (speaker_capturer_) {
    speaker_capturer_->Start();
  }
  return 0;
}

int Render::StopSpeakerCapture() {
  if (speaker_capturer_) {
    speaker_capturer_->Stop();
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
  params_.enable_turn = config_center_.IsEnableTurn();
  params_.on_receive_video_buffer = nullptr;
  params_.on_receive_audio_buffer = OnReceiveAudioBufferCb;
  params_.on_receive_data_buffer = OnReceiveDataBufferCb;

  params_.on_receive_video_frame = OnReceiveVideoBufferCb;

  params_.on_signal_status = OnSignalStatusCb;
  params_.on_connection_status = OnConnectionStatusCb;
  params_.net_status_report = NetStatusReport;
  params_.user_data = this;

  peer_ = CreatePeer(&params_);
  if (peer_) {
    LOG_INFO("[{}] Create peer instance successful", client_id_);
    Init(peer_, client_id_);
    LOG_INFO("[{}] Peer init finish", client_id_);
  } else {
    LOG_INFO("Create peer instance failed");
  }

  return 0;
}

int Render::AudioDeviceInit() {
  // Audio
  SDL_AudioSpec want_in, have_in, want_out, have_out;
  SDL_zero(want_in);
  want_in.freq = 48000;
  want_in.format = AUDIO_S16LSB;
  want_in.channels = 1;
  want_in.samples = 480;
  want_in.callback = SdlCaptureAudioIn;
  want_in.userdata = this;

  // input_dev_ = SDL_OpenAudioDevice(NULL, 1, &want_in, &have_in, 0);
  // if (input_dev_ == 0) {
  //   SDL_Log("Failed to open input: %s", SDL_GetError());
  //   // return 1;
  // }

  SDL_zero(want_out);
  want_out.freq = 48000;
  want_out.format = AUDIO_S16LSB;
  want_out.channels = 1;
  // want_out.silence = 0;
  want_out.samples = 480;
  want_out.callback = nullptr;
  want_out.userdata = this;

  output_dev_ = SDL_OpenAudioDevice(nullptr, 0, &want_out, NULL, 0);
  if (output_dev_ == 0) {
    SDL_Log("Failed to open input: %s", SDL_GetError());
    // return 1;
  }

  // SDL_PauseAudioDevice(input_dev_, 0);
  SDL_PauseAudioDevice(output_dev_, 0);

  return 0;
}

int Render::AudioDeviceDestroy() {
  SDL_CloseAudioDevice(output_dev_);
  // SDL_CloseAudioDevice(input_dev_);

  return 0;
}

int Render::Run() {
  LoadSettingsFromCacheFile();

  localization_language_ = (ConfigCenter::LANGUAGE)language_button_value_;
  localization_language_index_ = language_button_value_;

  // Setup SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER |
               SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Error: %s\n", SDL_GetError());
    return -1;
  }

  // get screen resolution
  SDL_DisplayMode DM;
  SDL_GetCurrentDisplayMode(0, &DM);
  screen_width_ = DM.w;
  screen_height_ = DM.h;

  stream_render_rect_.x = 0;
  stream_render_rect_.y = title_bar_height_;
  stream_render_rect_.w = main_window_width_;
  stream_render_rect_.h = main_window_height_ - title_bar_height_;

  // use linear filtering to render textures otherwise the graphics will be
  // blurry
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  // Master keyboard navigation enable flag. Enable full Tabbing + directional
  // arrows + space/enter to activate.
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
  // io.Fonts->AddFontFromMemoryTTF(fa_regular_400_ttf,
  // sizeof(fa_regular_400_ttf),
  //                                30.0f, &config, icon_ranges);
  io.Fonts->AddFontFromMemoryTTF(fa_solid_900_ttf, sizeof(fa_solid_900_ttf),
                                 30.0f, &config, icon_ranges);

  io.Fonts->Build();

  // Setup Dear ImGui style
  // ImGui::StyleColorsDark();
  ImGui::StyleColorsLight();

  // init modules
  if (!modules_inited_) {
    // audio
    AudioDeviceInit();

    // screen capture init
    screen_capturer_factory_ = new ScreenCapturerFactory();

    // speaker capture init
    speaker_capturer_factory_ = new SpeakerCapturerFactory();

    // mouse control
    device_controller_factory_ = new DeviceControllerFactory();

    // RTC
    CreateConnectionPeer();

    modules_inited_ = true;
  }

  // create main window with SDL_Renderer graphics context
  SDL_WindowFlags window_flags =
      (SDL_WindowFlags)(SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_BORDERLESS);
  main_window_ = SDL_CreateWindow(
      "Remote Desk", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      main_window_width_default_, main_window_height_default_, window_flags);

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

  // for window region action
  SDL_SetWindowHitTest(main_window_, HitTestCallback, this);

  SDL_GL_GetDrawableSize(main_window_, &main_window_width_real_,
                         &main_window_height_real_);
  dpi_scaling_w_ = (float)main_window_width_real_ / (float)main_window_width_;
  dpi_scaling_h_ = (float)main_window_width_real_ / (float)main_window_width_;
  SDL_RenderSetScale(main_renderer_, dpi_scaling_w_, dpi_scaling_h_);
  LOG_INFO("Use dpi scaling [{}x{}]", dpi_scaling_w_, dpi_scaling_h_);

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(main_window_, main_renderer_);
  ImGui_ImplSDLRenderer2_Init(main_renderer_);

  // Main loop
  while (!exit_) {
    if (!label_inited_ ||
        localization_language_index_last_ != localization_language_index_) {
      connect_button_label_ =
          connect_button_pressed_
              ? localization::disconnect[localization_language_index_]
              : localization::connect[localization_language_index_];

      mouse_control_button_label_ =
          mouse_control_button_pressed_
              ? localization::release_mouse[localization_language_index_]
              : localization::control_mouse[localization_language_index_];

      audio_capture_button_label_ =
          audio_capture_button_pressed_
              ? localization::mute[localization_language_index_]
              : localization::audio_capture[localization_language_index_];

      fullscreen_button_label_ =
          fullscreen_button_pressed_
              ? localization::exit_fullscreen[localization_language_index_]
              : localization::fullscreen[localization_language_index_];

      settings_button_label_ =
          localization::settings[localization_language_index_];
      label_inited_ = true;
      localization_language_index_last_ = localization_language_index_;
    }

    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(1.0f, 1.0f, 1.0f, fullscreen_button_pressed_ ? 0 : 1.0f));
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(main_window_width_,
               (connection_established_ && streaming_ && is_client_mode_)
                   ? (fullscreen_button_pressed_ ? 0 : title_bar_height_)
                   : main_window_height_default_),
        ImGuiCond_Always);
    ImGui::Begin("Render", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();

    if (!fullscreen_button_pressed_) {
      TitleBar();
    }

    if (connection_established_ && streaming_ && is_client_mode_) {
      if (!resizable_) {
        resizable_ = !resizable_;
        SDL_SetWindowResizable(main_window_, SDL_TRUE);
      }

      ControlWindow();
    } else {
      if (resizable_) {
        resizable_ = !resizable_;
        SDL_SetWindowResizable(main_window_, SDL_FALSE);
      }
      MainWindow();
    }

    ImGui::End();

    // create connection
    if (SignalStatus::SignalConnected == signal_status_ &&
        !is_create_connection_ && password_inited_) {
      LOG_INFO("Connected with signal server, create p2p connection");
      is_create_connection_ =
          CreateConnection(peer_, client_id_, password_saved_) ? false : true;
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

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) {
        if (streaming_) {
          LOG_INFO("Return to main interface");
          streaming_ = false;
          LOG_INFO("[{}] Leave connection [{}]", client_id_, remote_id_);
          LeaveConnection(peer_reserved_ ? peer_reserved_ : peer_,
                          remote_id_.c_str());
          if (peer_reserved_) {
            LOG_INFO("Destroy peer[reserved]");
            DestroyPeer(&peer_reserved_);
          }

          rejoin_ = false;
          memset(audio_buffer_, 0, 960);
          connection_established_ = false;
          received_frame_ = false;
          is_client_mode_ = false;
          audio_capture_button_pressed_ = false;
          SDL_SetWindowSize(main_window_, main_window_width_default_,
                            main_window_height_default_);

          SDL_Rect display_bounds;
          SDL_GetDisplayBounds(0, &display_bounds);
          int center_x = (display_bounds.w - main_window_width_default_) / 2;
          int center_y = (display_bounds.h - main_window_height_default_) / 2;
          SDL_SetWindowPosition(main_window_, center_x, center_y);

          continue;
        } else {
          LOG_INFO("Quit program");
          exit_ = true;
        }
      } else if (event.window.event == SDL_WINDOWEVENT_MAXIMIZED) {
      } else if (event.window.event == SDL_WINDOWEVENT_MINIMIZED) {
      } else if (event.window.event == SDL_WINDOWEVENT_RESTORED) {
        window_maximized_ = false;
      } else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        SDL_GetWindowSize(main_window_, &main_window_width_,
                          &main_window_height_);

        float video_ratio = (float)video_width_ / (float)video_height_;
        float video_ratio_reverse = (float)video_height_ / (float)video_width_;

        int render_area_width = main_window_width_;
        int render_area_height =
            main_window_height_ -
            (fullscreen_button_pressed_ ? 0 : title_bar_height_);

        if (render_area_width < render_area_height * video_ratio) {
          stream_render_rect_.x = 0;
          stream_render_rect_.y =
              abs(render_area_height -
                  render_area_width * video_ratio_reverse) /
                  2 +
              (fullscreen_button_pressed_ ? 0 : title_bar_height_);
          stream_render_rect_.w = render_area_width;
          stream_render_rect_.h = render_area_width * video_ratio_reverse;
        } else if (render_area_width > render_area_height * video_ratio) {
          stream_render_rect_.x =
              abs(render_area_width - render_area_height * video_ratio) / 2;
          stream_render_rect_.y =
              fullscreen_button_pressed_ ? 0 : title_bar_height_;
          stream_render_rect_.w = render_area_height * video_ratio;
          stream_render_rect_.h = render_area_height;
        } else {
          stream_render_rect_.x = 0;
          stream_render_rect_.y =
              fullscreen_button_pressed_ ? 0 : title_bar_height_;
          stream_render_rect_.w = render_area_width;
          stream_render_rect_.h = render_area_height;
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
          if (video_width_ != texture_width_ ||
              video_height_ != texture_height_) {
            texture_width_ = video_width_;
            texture_height_ = video_height_;

            SDL_DestroyTexture(stream_texture_);
            stream_texture_ = SDL_CreateTexture(
                main_renderer_, stream_pixformat_, SDL_TEXTUREACCESS_STREAMING,
                texture_width_, texture_height_);
          }
        SDL_UpdateTexture(stream_texture_, NULL, dst_buffer_, texture_width_);
      } else {
        if (connection_established_) {
          ProcessMouseKeyEven(event);
        }
      }
    }

    // Rendering
    ImGui::Render();

    SDL_RenderClear(main_renderer_);
    if (connection_established_ && received_frame_ && streaming_) {
      SDL_RenderCopy(main_renderer_, stream_texture_, NULL,
                     &stream_render_rect_);
    }

    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), main_renderer_);
    SDL_RenderPresent(main_renderer_);

    // frame_count_++;
    // end_time_ = SDL_GetTicks();
    // elapsed_time_ = end_time_ - start_time_;
    // if (elapsed_time_ >= 1000) {
    //   fps_ = frame_count_ / (elapsed_time_ / 1000);
    //   frame_count_ = 0;
    //   window_title = "Remote Desk Client FPS [" + std::to_string(fps_) +
    //                  "] status [" + connection_status_str_ + "|" +
    //                  connection_status_str_ + "]";
    //   // For MacOS, UI frameworks can only be called from the main thread
    //   SDL_SetWindowTitle(main_window_, window_title.c_str());
    //   start_time_ = end_time_;
    // }
  }

  // Cleanup
  if (screen_capturer_) {
    screen_capturer_->Destroy();
    delete screen_capturer_;
    screen_capturer_ = nullptr;
  }

  if (speaker_capturer_) {
    speaker_capturer_->Destroy();
    delete speaker_capturer_;
    speaker_capturer_ = nullptr;
  }

  if (mouse_controller_) {
    mouse_controller_->Destroy();
    delete mouse_controller_;
    mouse_controller_ = nullptr;
  }

  if (screen_capturer_factory_) {
    delete screen_capturer_factory_;
    screen_capturer_factory_ = nullptr;
  }

  if (speaker_capturer_factory_) {
    delete speaker_capturer_factory_;
    speaker_capturer_factory_ = nullptr;
  }

  if (device_controller_factory_) {
    delete device_controller_factory_;
    device_controller_factory_ = nullptr;
  }

  if (peer_) {
    LOG_INFO("[{}] Leave connection [{}]", client_id_, client_id_);
    LeaveConnection(peer_, client_id_);
    is_client_mode_ = false;
    LOG_INFO("Destroy peer");
    DestroyPeer(&peer_);
  }

  if (peer_reserved_) {
    LOG_INFO("Destroy peer[reserved]");
    DestroyPeer(&peer_reserved_);
  }

  AudioDeviceDestroy();

  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();

  SDL_DestroyTexture(stream_texture_);
  SDL_DestroyRenderer(main_renderer_);
  SDL_DestroyWindow(main_window_);

  SDL_Quit();

  return 0;
}