#include "render.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "OPPOSans_Regular.h"
#include "device_controller_factory.h"
#include "fa_regular_400.h"
#include "fa_solid_900.h"
#include "layout_style.h"
#include "localization.h"
#include "platform.h"
#include "rd_log.h"
#include "screen_capturer_factory.h"

#define NV12_BUFFER_SIZE 1280 * 720 * 3 / 2

#define MOUSE_GRAB_PADDING 5

#define STREAM_FRASH (SDL_USEREVENT + 1)

SDL_HitTestResult Render::HitTestCallback(SDL_Window* window,
                                          const SDL_Point* area, void* data) {
  Render* render = (Render*)data;
  if (!render) {
    return SDL_HITTEST_NORMAL;
  }

  if (render->fullscreen_button_pressed_) {
    return SDL_HITTEST_NORMAL;
  }

  int window_width, window_height;
  SDL_GetWindowSize(window, &window_width, &window_height);

  if (area->y < 30 && area->y > MOUSE_GRAB_PADDING &&
      area->x < window_width - 120 && area->x > MOUSE_GRAB_PADDING) {
    return SDL_HITTEST_DRAGGABLE;
  }

  // if (!render->streaming_) {
  //   return SDL_HITTEST_NORMAL;
  // }

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
  cd_cache_mutex_.lock();
  std::ofstream cd_cache_file("cache.cd", std::ios::binary);
  if (!cd_cache_file) {
    cd_cache_mutex_.unlock();
    return -1;
  }

  memset(&cd_cache_.client_id, 0, sizeof(cd_cache_.client_id));
  memcpy(cd_cache_.client_id, client_id_, sizeof(client_id_));
  memset(&cd_cache_.password, 0, sizeof(cd_cache_.password));
  memcpy(cd_cache_.password, password_saved_, sizeof(password_saved_));
  memcpy(&cd_cache_.language, &language_button_value_,
         sizeof(language_button_value_));
  memcpy(&cd_cache_.video_quality, &video_quality_button_value_,
         sizeof(video_quality_button_value_));
  memcpy(&cd_cache_.video_encode_format, &video_encode_format_button_value_,
         sizeof(video_encode_format_button_value_));
  memcpy(&cd_cache_.enable_hardware_video_codec, &enable_hardware_video_codec_,
         sizeof(enable_hardware_video_codec_));
  memcpy(&cd_cache_.enable_turn, &enable_turn_, sizeof(enable_turn_));

  cd_cache_file.write(reinterpret_cast<char*>(&cd_cache_), sizeof(CDCache));
  cd_cache_file.close();
  cd_cache_mutex_.unlock();

  config_center_.SetLanguage((ConfigCenter::LANGUAGE)language_button_value_);
  config_center_.SetVideoQuality(
      (ConfigCenter::VIDEO_QUALITY)video_quality_button_value_);
  config_center_.SetVideoEncodeFormat(
      (ConfigCenter::VIDEO_ENCODE_FORMAT)video_encode_format_button_value_);
  config_center_.SetHardwareVideoCodec(enable_hardware_video_codec_);
  config_center_.SetTurn(enable_turn_);

  LOG_INFO("Save settings into cache file success");

  return 0;
}

int Render::LoadSettingsFromCacheFile() {
  cd_cache_mutex_.lock();
  std::ifstream cd_cache_file("cache.cd", std::ios::binary);
  if (!cd_cache_file) {
    cd_cache_mutex_.unlock();

    LOG_INFO("Init cache file by using default settings");
    memset(password_saved_, 0, sizeof(password_saved_));
    memset(aes128_key_, 0, sizeof(aes128_key_));
    memset(aes128_iv_, 0, sizeof(aes128_iv_));
    language_button_value_ = 0;
    video_quality_button_value_ = 0;
    video_encode_format_button_value_ = 1;
    enable_hardware_video_codec_ = false;
    enable_turn_ = false;

    config_center_.SetLanguage((ConfigCenter::LANGUAGE)language_button_value_);
    config_center_.SetVideoQuality(
        (ConfigCenter::VIDEO_QUALITY)video_quality_button_value_);
    config_center_.SetVideoEncodeFormat(
        (ConfigCenter::VIDEO_ENCODE_FORMAT)video_encode_format_button_value_);
    config_center_.SetHardwareVideoCodec(enable_hardware_video_codec_);
    config_center_.SetTurn(enable_turn_);

    thumbnail_ = std::make_unique<Thumbnail>();
    thumbnail_->GetKeyAndIv(aes128_key_, aes128_iv_);
    thumbnail_->DeleteAllFilesInDirectory();

    SaveSettingsIntoCacheFile();

    return -1;
  }

  cd_cache_file.read(reinterpret_cast<char*>(&cd_cache_), sizeof(CDCache));
  cd_cache_file.close();
  cd_cache_mutex_.unlock();

  memset(&client_id_, 0, sizeof(client_id_));
  memcpy(client_id_, cd_cache_.client_id, sizeof(client_id_));
  memcpy(password_saved_, cd_cache_.password, sizeof(password_saved_));
  if (0 != strcmp(password_saved_, "") && 7 == sizeof(password_saved_)) {
    password_inited_ = true;
  }

  memcpy(aes128_key_, cd_cache_.key, sizeof(cd_cache_.key));
  memcpy(aes128_iv_, cd_cache_.iv, sizeof(cd_cache_.iv));

  thumbnail_ = std::make_unique<Thumbnail>(aes128_key_, aes128_iv_);

  language_button_value_ = cd_cache_.language;
  video_quality_button_value_ = cd_cache_.video_quality;
  video_encode_format_button_value_ = cd_cache_.video_encode_format;
  enable_hardware_video_codec_ = cd_cache_.enable_hardware_video_codec;
  enable_turn_ = cd_cache_.enable_turn;

  language_button_value_last_ = language_button_value_;
  video_quality_button_value_last_ = video_quality_button_value_;
  video_encode_format_button_value_last_ = video_encode_format_button_value_;
  enable_hardware_video_codec_last_ = enable_hardware_video_codec_;
  enable_turn_last_ = enable_turn_;

  config_center_.SetLanguage((ConfigCenter::LANGUAGE)language_button_value_);
  config_center_.SetVideoQuality(
      (ConfigCenter::VIDEO_QUALITY)video_quality_button_value_);
  config_center_.SetVideoEncodeFormat(
      (ConfigCenter::VIDEO_ENCODE_FORMAT)video_encode_format_button_value_);
  config_center_.SetHardwareVideoCodec(enable_hardware_video_codec_);
  config_center_.SetTurn(enable_turn_);

  LOG_INFO("Load settings from cache file");

  return 0;
}

int Render::StartScreenCapturer() {
  screen_capturer_ = (ScreenCapturer*)screen_capturer_factory_->Create();
  last_frame_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();

  int screen_capturer_init_ret = screen_capturer_->Init(
      60, [this](unsigned char* data, int size, int width, int height) -> void {
        auto now_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
        auto duration = now_time - last_frame_time_;
        if (duration >= 0) {
          XVideoFrame frame;
          frame.data = (const char*)data;
          frame.size = size;
          frame.width = width;
          frame.height = height;
          frame.captured_timestamp = GetSystemTimeMicros(peer_);
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

int Render::StopScreenCapturer() {
  if (screen_capturer_) {
    LOG_INFO("Stop screen capturer")
    screen_capturer_->Stop();
    screen_capturer_->Destroy();
    delete screen_capturer_;
    screen_capturer_ = nullptr;
  }

  return 0;
}

int Render::StartSpeakerCapturer() {
  if (!speaker_capturer_) {
    speaker_capturer_ = (SpeakerCapturer*)speaker_capturer_factory_->Create();
    int speaker_capturer_init_ret = speaker_capturer_->Init(
        [this](unsigned char* data, size_t size) -> void {
          SendAudioFrame(peer_, (const char*)data, size);
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

int Render::StopSpeakerCapturer() {
  if (speaker_capturer_) {
    speaker_capturer_->Stop();
  }

  return 0;
}

int Render::StartMouseController() {
  if (!device_controller_factory_) {
    LOG_INFO("Device controller factory is nullptr");
    return -1;
  }
  mouse_controller_ = (MouseController*)device_controller_factory_->Create(
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

int Render::StopMouseController() {
  if (mouse_controller_) {
    mouse_controller_->Destroy();
    delete mouse_controller_;
    mouse_controller_ = nullptr;
  }
  return 0;
}

int Render::StartKeyboardCapturer() {
  if (!keyboard_capturer_) {
    LOG_INFO("keyboard capturer is nullptr");
    return -1;
  }

  int keyboard_capturer_init_ret = keyboard_capturer_->Hook(
      [](int key_code, bool is_down, void* user_ptr) {
        if (user_ptr) {
          Render* render = (Render*)user_ptr;
          render->SendKeyCommand(key_code, is_down);
        }
      },
      this);
  if (0 != keyboard_capturer_init_ret) {
    LOG_ERROR("Start keyboard capturer failed");
  } else {
    LOG_INFO("Start keyboard capturer");
  }

  return 0;
}

int Render::StopKeyboardCapturer() {
  if (keyboard_capturer_) {
    keyboard_capturer_->Unhook();
    LOG_INFO("Stop keyboard capturer");
  }
  return 0;
}

int Render::CreateConnectionPeer() {
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

  params_.user_id = client_id_;
  params_.user_data = this;

  peer_ = CreatePeer(&params_);
  if (peer_) {
    LOG_INFO("[{}] Create peer instance successful", client_id_);
    Init(peer_);
    LOG_INFO("[{}] Peer init finish", client_id_);
  } else {
    LOG_INFO("Create peer [{}] instance failed", client_id_);
  }

  return 0;
}

int Render::AudioDeviceInit() {
  // Audio
  SDL_AudioSpec want_in, want_out;
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

void Render::UpdateInteractions() {
  if (start_screen_capturer_ && !screen_capturer_is_started_) {
    StartScreenCapturer();
    screen_capturer_is_started_ = true;
  } else if (!start_screen_capturer_ && screen_capturer_is_started_) {
    StopScreenCapturer();
    screen_capturer_is_started_ = false;
  }

  if (start_mouse_controller_ && !mouse_controller_is_started_) {
    StartMouseController();
    mouse_controller_is_started_ = true;
  } else if (!start_mouse_controller_ && mouse_controller_is_started_) {
    StopMouseController();
    mouse_controller_is_started_ = false;
  }

  if (start_keyboard_capturer_ && foucs_on_stream_window_) {
    if (!keyboard_capturer_is_started_) {
      StartKeyboardCapturer();
      keyboard_capturer_is_started_ = true;
    }
  } else if (keyboard_capturer_is_started_) {
    StopKeyboardCapturer();
    keyboard_capturer_is_started_ = false;
  }
}

int Render::CreateMainWindow() {
  main_ctx_ = ImGui::CreateContext();
  if (!main_ctx_) {
    LOG_ERROR("Main context is null");
    return -1;
  }

  ImGui::SetCurrentContext(main_ctx_);

  SDL_WindowFlags window_flags =
      (SDL_WindowFlags)(SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_BORDERLESS |
                        SDL_WINDOW_HIDDEN);
  main_window_ =
      SDL_CreateWindow("Remote Desk", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, (int)main_window_width_default_,
                       (int)main_window_height_default_, window_flags);

  main_renderer_ = SDL_CreateRenderer(
      main_window_, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
  if (main_renderer_ == nullptr) {
    LOG_ERROR("Error creating SDL_Renderer");
    return 0;
  }

  SDL_SetWindowResizable(main_window_, SDL_FALSE);

  // for window region action
  SDL_SetWindowHitTest(main_window_, HitTestCallback, this);

  return 0;
}

int Render::DestroyMainWindow() {
  if (main_ctx_) {
    ImGui::SetCurrentContext(main_ctx_);
  }

  if (main_renderer_) {
    SDL_DestroyRenderer(main_renderer_);
  }

  if (main_window_) {
    SDL_DestroyWindow(main_window_);
  }

  return 0;
}

int Render::CreateStreamWindow() {
  if (stream_window_created_) {
    return 0;
  }

  stream_ctx_ = ImGui::CreateContext();
  if (!stream_ctx_) {
    LOG_ERROR("Stream context is null");
    return -1;
  }

  ImGui::SetCurrentContext(stream_ctx_);

  SDL_WindowFlags window_flags =
      (SDL_WindowFlags)(SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_BORDERLESS);
  stream_window_ =
      SDL_CreateWindow("Stream window", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, stream_window_width_default_,
                       stream_window_height_default_, window_flags);

  stream_renderer_ = SDL_CreateRenderer(
      stream_window_, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
  if (stream_renderer_ == nullptr) {
    LOG_ERROR("Error creating SDL_Renderer");
    return 0;
  }

  stream_pixformat_ = SDL_PIXELFORMAT_NV12;
  // stream_texture_ = SDL_CreateTexture(stream_renderer_, stream_pixformat_,
  //                                     SDL_TEXTUREACCESS_STREAMING,
  //                                     texture_width_, texture_height_);

  SDL_SetWindowResizable(stream_window_, SDL_TRUE);

  // for window region action
  SDL_SetWindowHitTest(stream_window_, HitTestCallback, this);

  // change props->stream_render_rect_
  SDL_Event event;
  event.type = SDL_WINDOWEVENT;
  event.window.windowID = SDL_GetWindowID(stream_window_);
  event.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
  SDL_PushEvent(&event);

  stream_window_created_ = true;
  just_created_ = true;

  return 0;
}

int Render::DestroyStreamWindow() {
  stream_window_width_ = (float)stream_window_width_default_;
  stream_window_height_ = (float)stream_window_height_default_;

  if (stream_ctx_) {
    ImGui::SetCurrentContext(stream_ctx_);
  }

  if (stream_renderer_) {
    SDL_DestroyRenderer(stream_renderer_);
  }

  if (stream_window_) {
    SDL_DestroyWindow(stream_window_);
  }

  stream_window_created_ = false;

  return 0;
}

int Render::SetupFontAndStyle() {
  // Setup Dear ImGui style
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

  // Load Fonts
  ImFontConfig config;
  config.FontDataOwnedByAtlas = false;
  io.Fonts->AddFontFromMemoryTTF(OPPOSans_Regular_ttf, OPPOSans_Regular_ttf_len,
                                 32.0f, &config,
                                 io.Fonts->GetGlyphRangesChineseFull());
  config.MergeMode = true;
  static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
  io.Fonts->AddFontFromMemoryTTF(fa_solid_900_ttf, fa_solid_900_ttf_len, 30.0f,
                                 &config, icon_ranges);
  io.Fonts->Build();
  ImGui::StyleColorsLight();

  return 0;
}

int Render::SetupMainWindow() {
  if (!main_ctx_) {
    LOG_ERROR("Main context is null");
    return -1;
  }

  ImGui::SetCurrentContext(main_ctx_);

  SetupFontAndStyle();

  SDL_GL_GetDrawableSize(main_window_, &main_window_width_real_,
                         &main_window_height_real_);
  main_window_dpi_scaling_w_ = main_window_width_real_ / main_window_width_;
  main_window_dpi_scaling_h_ = main_window_width_real_ / main_window_width_;
  SDL_RenderSetScale(main_renderer_, main_window_dpi_scaling_w_,
                     main_window_dpi_scaling_h_);
  LOG_INFO("Use dpi scaling [{}x{}] for main window",
           main_window_dpi_scaling_w_, main_window_dpi_scaling_h_);

  ImGui_ImplSDL2_InitForSDLRenderer(main_window_, main_renderer_);
  ImGui_ImplSDLRenderer2_Init(main_renderer_);

  return 0;
}

int Render::DestroyMainWindowContext() {
  ImGui::SetCurrentContext(main_ctx_);
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext(main_ctx_);

  return 0;
}

int Render::SetupStreamWindow() {
  if (stream_window_inited_) {
    return 0;
  }

  if (!stream_ctx_) {
    LOG_ERROR("Stream context is null");
    return -1;
  }

  ImGui::SetCurrentContext(stream_ctx_);

  SetupFontAndStyle();

  SDL_GL_GetDrawableSize(stream_window_, &stream_window_width_real_,
                         &stream_window_height_real_);

  stream_window_dpi_scaling_w_ =
      stream_window_width_real_ / stream_window_width_;
  stream_window_dpi_scaling_h_ =
      stream_window_width_real_ / stream_window_width_;

  SDL_RenderSetScale(stream_renderer_, stream_window_dpi_scaling_w_,
                     stream_window_dpi_scaling_h_);
  LOG_INFO("Use dpi scaling [{}x{}] for stream window",
           stream_window_dpi_scaling_w_, stream_window_dpi_scaling_h_);

  ImGui_ImplSDL2_InitForSDLRenderer(stream_window_, stream_renderer_);
  ImGui_ImplSDLRenderer2_Init(stream_renderer_);

  stream_window_inited_ = true;
  LOG_INFO("Stream window inited");

  return 0;
}

int Render::DestroyStreamWindowContext() {
  stream_window_inited_ = false;
  ImGui::SetCurrentContext(stream_ctx_);
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext(stream_ctx_);

  return 0;
}

int Render::DrawMainWindow() {
  if (!main_ctx_) {
    LOG_ERROR("Main context is null");
    return -1;
  }

  ImGui::SetCurrentContext(main_ctx_);
  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::SetNextWindowSize(
      ImVec2(main_window_width_, main_window_height_default_),
      ImGuiCond_Always);
  ImGui::Begin("MainRender", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleColor();

  TitleBar(true);

  MainWindow();

  ImGui::End();

  // Rendering
  ImGui::Render();
  SDL_RenderClear(main_renderer_);
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), main_renderer_);
  SDL_RenderPresent(main_renderer_);

  return 0;
}

int Render::DrawStreamWindow() {
  if (!stream_ctx_) {
    LOG_ERROR("Stream context is null");
    return -1;
  }

  ImGui::SetCurrentContext(stream_ctx_);
  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  if (!fullscreen_button_pressed_) {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(stream_window_width_,
               fullscreen_button_pressed_ ? 0 : title_bar_height_),
        ImGuiCond_Always);
    ImGui::Begin("StreamRender", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();

    TitleBar(false);
    ImGui::End();
  }

  StreamWindow();

  // Rendering
  ImGui::Render();
  SDL_RenderClear(stream_renderer_);

  for (auto& it : client_properties_) {
    auto props = it.second;
    SDL_RenderCopy(stream_renderer_, props->stream_texture_, NULL,
                   &(props->stream_render_rect_));
  }
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), stream_renderer_);
  SDL_RenderPresent(stream_renderer_);

  return 0;
}

int Render::Run() {
  InitializeSettings();
  InitializeSDL();
  InitializeModules();
  InitializeMainWindow();

  const int scaled_video_width_ = 160;
  const int scaled_video_height_ = 90;
  argb_buffer_ = new char[scaled_video_width_ * scaled_video_height_ * 40];

  MainLoop();

  Cleanup();

  return 0;
}

void Render::InitializeSettings() {
  LoadSettingsFromCacheFile();

  localization_language_ = (ConfigCenter::LANGUAGE)language_button_value_;
  localization_language_index_ = language_button_value_;
  if (localization_language_index_ != 0 && localization_language_index_ != 1) {
    localization_language_index_ = 0;
    LOG_ERROR("Invalid language index: [{}], use [0] by default",
              localization_language_index_);
  }
}

void Render::InitializeSDL() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER |
               SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Error: %s\n", SDL_GetError());
    exit(-1);
  }

  SDL_DisplayMode DM;
  SDL_GetCurrentDisplayMode(0, &DM);
  screen_width_ = DM.w;
  screen_height_ = DM.h;

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
}

void Render::InitializeModules() {
  if (!modules_inited_) {
    AudioDeviceInit();
    screen_capturer_factory_ = new ScreenCapturerFactory();
    speaker_capturer_factory_ = new SpeakerCapturerFactory();
    device_controller_factory_ = new DeviceControllerFactory();
    keyboard_capturer_ = (KeyboardCapturer*)device_controller_factory_->Create(
        DeviceControllerFactory::Device::Keyboard);
    CreateConnectionPeer();
    modules_inited_ = true;
  }
}

void Render::InitializeMainWindow() {
  CreateMainWindow();
  SetupMainWindow();
  if (SDL_WINDOW_HIDDEN & SDL_GetWindowFlags(main_window_)) {
    SDL_ShowWindow(main_window_);
  }
}

void Render::MainLoop() {
  while (!exit_) {
    UpdateLabels();
    ProcessSdlEvent();
    HandleRecentConnections();
    HandleStreamWindow();

    DrawMainWindow();
    if (stream_window_inited_) {
      DrawStreamWindow();
    }

    UpdateInteractions();

    if (SignalStatus::SignalConnected == signal_status_ &&
        !is_create_connection_ && password_inited_) {
      LOG_INFO("Connected with signal server, create p2p connection");
      is_create_connection_ =
          CreateConnection(peer_, client_id_, password_saved_) ? false : true;
    }

    if (!host_info_sent_ && screen_width_ > 0 && screen_height_ > 0) {
      std::string host_name = GetHostName();
      RemoteAction remote_action;
      remote_action.type = ControlType::host_infomation;
      memcpy(&remote_action.i.host_name, host_name.data(), host_name.size());
      remote_action.i.host_name_size = host_name.size();
      remote_action.i.original_display_width =
          screen_width_ * main_window_dpi_scaling_w_;
      remote_action.i.original_display_height =
          screen_height_ * main_window_dpi_scaling_h_;
      int ret = SendDataFrame(peer_, (const char*)&remote_action,
                              sizeof(remote_action));
      if (0 == ret) {
        host_info_sent_ = true;
      }
    }
  }
}

void Render::UpdateLabels() {
  if (!label_inited_ ||
      localization_language_index_last_ != localization_language_index_) {
    connect_button_label_ =
        connect_button_pressed_
            ? localization::disconnect[localization_language_index_]
            : localization::connect[localization_language_index_];
    label_inited_ = true;
    localization_language_index_last_ = localization_language_index_;
  }
}

void Render::HandleRecentConnections() {
  if (reload_recent_connections_ && main_renderer_) {
    uint32_t now_time = SDL_GetTicks();
    if (now_time - recent_connection_image_save_time_ >= 50) {
      int ret = thumbnail_->LoadThumbnail(main_renderer_, recent_connections_,
                                          &recent_connection_image_width_,
                                          &recent_connection_image_height_);
      if (!ret) {
        LOG_INFO("Load recent connection thumbnails");
      }
      reload_recent_connections_ = false;
    }
  }
}

void Render::HandleStreamWindow() {
  if (need_to_create_stream_window_) {
    CreateStreamWindow();
    SetupStreamWindow();
    need_to_create_stream_window_ = false;
  }

  if (stream_window_inited_) {
    if (!stream_window_grabbed_ && control_mouse_) {
      SDL_SetWindowGrab(stream_window_, SDL_TRUE);
      stream_window_grabbed_ = true;
    } else if (stream_window_grabbed_ && !control_mouse_) {
      SDL_SetWindowGrab(stream_window_, SDL_FALSE);
      stream_window_grabbed_ = false;
    }
  }
}

void Render::Cleanup() {
  delete[] argb_buffer_;

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

  CleanupFactories();
  CleanupPeers();
  AudioDeviceDestroy();
  DestroyMainWindow();
  DestroyMainWindowContext();
  SDL_Quit();
}

void Render::CleanupFactories() {
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
}

void Render::CleanupPeers() {
  if (peer_) {
    LOG_INFO("[{}] Leave connection [{}]", client_id_, client_id_);
    LeaveConnection(peer_, client_id_);
    is_client_mode_ = false;
    LOG_INFO("Destroy peer [{}]", client_id_);
    DestroyPeer(&peer_);
  }

  for (auto& it : client_properties_) {
    auto props = it.second;
    if (props->dst_buffer_) {
      thumbnail_->SaveToThumbnail(
          (char*)props->dst_buffer_, props->video_width_, props->video_height_,
          it.first, props->remote_host_name_,
          props->remember_password_ ? props->remote_password_ : "");
    }

    std::string host_name = it.first;
    PeerPtr* peer_client = props->peer_;
    if (peer_client) {
      std::string client_id;
      if (host_name == client_id_) {
        client_id = "C-" + std::string(client_id_);
      } else {
        client_id = client_id_;
      }
      LOG_INFO("[{}] Leave connection [{}]", client_id, host_name);
      LeaveConnection(peer_client, host_name.c_str());
      LOG_INFO("Destroy peer [{}]", client_id);
      DestroyPeer(&peer_client);
    }
  }
}

void Render::ProcessSdlEvent() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (main_ctx_) {
      ImGui::SetCurrentContext(main_ctx_);
      ImGui_ImplSDL2_ProcessEvent(&event);
    } else {
      LOG_ERROR("Main context is null");
      return;
    }

    if (stream_window_inited_) {
      if (stream_ctx_) {
        ImGui::SetCurrentContext(stream_ctx_);
        ImGui_ImplSDL2_ProcessEvent(&event);
      } else {
        LOG_ERROR("Stream context is null");
        return;
      }
    }

    switch (event.type) {
      case SDL_QUIT:
        if (stream_window_inited_) {
          LOG_INFO("Destroy stream window");
          SDL_SetWindowGrab(stream_window_, SDL_FALSE);
          DestroyStreamWindow();
          DestroyStreamWindowContext();

          for (auto& [host_name, props] : client_properties_) {
            if (props->dst_buffer_) {
              thumbnail_->SaveToThumbnail(
                  (char*)props->dst_buffer_, props->video_width_,
                  props->video_height_, host_name, props->remote_host_name_,
                  props->remember_password_ ? props->remote_password_ : "");
            }

            if (props->peer_) {
              std::string client_id = (host_name == client_id_)
                                          ? "C-" + std::string(client_id_)
                                          : client_id_;
              LOG_INFO("[{}] Leave connection [{}]", client_id, host_name);
              LeaveConnection(props->peer_, host_name.c_str());
              LOG_INFO("Destroy peer [{}]", client_id);
              DestroyPeer(&props->peer_);
            }

            props->streaming_ = false;
            props->remember_password_ = false;
            props->connection_established_ = false;
            props->audio_capture_button_pressed_ = false;

            memset(&props->net_traffic_stats_, 0,
                   sizeof(props->net_traffic_stats_));
            SDL_SetWindowFullscreen(main_window_, SDL_FALSE);
            SDL_DestroyTexture(props->stream_texture_);
            props->stream_texture_ = nullptr;
            SDL_FlushEvents(STREAM_FRASH, STREAM_FRASH);
            memset(audio_buffer_, 0, 720);
          }
          client_properties_.clear();

          rejoin_ = false;
          is_client_mode_ = false;
          reload_recent_connections_ = true;
          fullscreen_button_pressed_ = false;
          just_created_ = false;
          recent_connection_image_save_time_ = SDL_GetTicks();
        } else {
          LOG_INFO("Quit program");
          exit_ = true;
        }
        break;

      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_CLOSE &&
            event.window.windowID != SDL_GetWindowID(stream_window_)) {
          exit_ = true;
        } else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED &&
                   stream_window_created_ &&
                   event.window.windowID == SDL_GetWindowID(stream_window_)) {
          for (auto& [_, props] : client_properties_) {
            if (!props->reset_control_bar_pos_) {
              props->mouse_diff_control_bar_pos_x_ = 0;
              props->mouse_diff_control_bar_pos_y_ = 0;
            }

            if (!just_created_) {
              props->reset_control_bar_pos_ = true;
            }

            int stream_window_width, stream_window_height;
            SDL_GetWindowSize(stream_window_, &stream_window_width,
                              &stream_window_height);
            stream_window_width_ = (float)stream_window_width;
            stream_window_height_ = (float)stream_window_height;

            float video_ratio = (float)props->original_display_width_ /
                                (float)props->original_display_height_;
            float video_ratio_reverse = (float)props->original_display_height_ /
                                        (float)props->original_display_width_;

            float render_area_width = stream_window_width_;
            float render_area_height =
                stream_window_height_ -
                (fullscreen_button_pressed_ ? 0 : title_bar_height_);

            props->stream_render_rect_last_ = props->stream_render_rect_;
            if (render_area_width < render_area_height * video_ratio) {
              props->stream_render_rect_ = {
                  0,
                  (int)(abs(render_area_height -
                            render_area_width * video_ratio_reverse) /
                            2 +
                        (fullscreen_button_pressed_ ? 0 : title_bar_height_)),
                  (int)render_area_width,
                  (int)(render_area_width * video_ratio_reverse)};
            } else if (render_area_width > render_area_height * video_ratio) {
              props->stream_render_rect_ = {
                  (int)abs(render_area_width -
                           render_area_height * video_ratio) /
                      2,
                  fullscreen_button_pressed_ ? 0 : (int)title_bar_height_,
                  (int)(render_area_height * video_ratio),
                  (int)render_area_height};
            } else {
              props->stream_render_rect_ = {
                  0, fullscreen_button_pressed_ ? 0 : (int)title_bar_height_,
                  (int)render_area_width, (int)render_area_height};
            }
          }

        } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
                   event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
          bool focus_gained =
              event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED;
          if (stream_window_ &&
              SDL_GetWindowID(stream_window_) == event.window.windowID) {
            foucs_on_stream_window_ = focus_gained;
            LOG_INFO("{} focus on stream window",
                     focus_gained ? "Gained" : "Lost");
          } else if (main_window_ &&
                     SDL_GetWindowID(main_window_) == event.window.windowID) {
            foucs_on_main_window_ = focus_gained;
            LOG_INFO("{} focus on main window",
                     focus_gained ? "Gained" : "Lost");
          }
        }
        break;

      case STREAM_FRASH: {
        auto* props = static_cast<SubStreamWindowProperties*>(event.user.data1);
        if (!props) {
          LOG_ERROR("Invalid stream window properties");
          continue;
        }

        if (props->stream_texture_) {
          if (props->video_width_ != props->texture_width_ ||
              props->video_height_ != props->texture_height_) {
            LOG_WARN("Resolution changed, old: [{}x{}], new: [{}x{}]",
                     props->texture_width_, props->texture_height_,
                     props->video_width_, props->video_height_);
            props->texture_width_ = props->video_width_;
            props->texture_height_ = props->video_height_;

            SDL_DestroyTexture(props->stream_texture_);
            props->stream_texture_ = SDL_CreateTexture(
                stream_renderer_, stream_pixformat_,
                SDL_TEXTUREACCESS_STREAMING, props->texture_width_,
                props->texture_height_);
          }
        } else {
          props->texture_width_ = props->video_width_;
          props->texture_height_ = props->video_height_;
          props->stream_texture_ = SDL_CreateTexture(
              stream_renderer_, stream_pixformat_, SDL_TEXTUREACCESS_STREAMING,
              props->texture_width_, props->texture_height_);
        }

        SDL_UpdateTexture(props->stream_texture_, NULL, props->dst_buffer_,
                          props->texture_width_);
        break;
      }

      case SDL_MOUSEMOTION:
      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP:
      case SDL_MOUSEWHEEL:
        if (foucs_on_stream_window_) {
          ProcessMouseEvent(event);
        }
        break;

      default:
        break;
    }
  }
}