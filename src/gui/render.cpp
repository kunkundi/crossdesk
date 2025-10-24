#include "render.h"

#include <libyuv.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "OPPOSans_Regular.h"
#include "device_controller_factory.h"
#include "fa_regular_400.h"
#include "fa_solid_900.h"
#include "layout.h"
#include "localization.h"
#include "platform.h"
#include "rd_log.h"
#include "screen_capturer_factory.h"

#define NV12_BUFFER_SIZE 1280 * 720 * 3 / 2

#define MOUSE_GRAB_PADDING 5

std::vector<char> Render::SerializeRemoteAction(const RemoteAction& action) {
  std::vector<char> buffer;
  buffer.push_back(static_cast<char>(action.type));

  auto insert_bytes = [&](const void* ptr, size_t len) {
    buffer.insert(buffer.end(), (const char*)ptr, (const char*)ptr + len);
  };

  if (action.type == ControlType::host_infomation) {
    insert_bytes(&action.i.host_name_size, sizeof(size_t));
    insert_bytes(action.i.host_name, action.i.host_name_size);

    size_t num = action.i.display_num;
    insert_bytes(&num, sizeof(size_t));

    for (size_t i = 0; i < num; ++i) {
      size_t len = strlen(action.i.display_list[i]);
      insert_bytes(&len, sizeof(size_t));
      insert_bytes(action.i.display_list[i], len);
    }

    insert_bytes(action.i.left, sizeof(int) * num);
    insert_bytes(action.i.top, sizeof(int) * num);
    insert_bytes(action.i.right, sizeof(int) * num);
    insert_bytes(action.i.bottom, sizeof(int) * num);
  }

  return buffer;
}

bool Render::DeserializeRemoteAction(const char* data, size_t size,
                                     RemoteAction& out) {
  size_t offset = 0;
  auto read = [&](void* dst, size_t len) -> bool {
    if (offset + len > size) return false;
    memcpy(dst, data + offset, len);
    offset += len;
    return true;
  };

  if (size < 1) return false;
  out.type = static_cast<ControlType>(data[offset++]);

  if (out.type == ControlType::host_infomation) {
    size_t name_len;
    if (!read(&name_len, sizeof(size_t)) || name_len >= sizeof(out.i.host_name))
      return false;
    if (!read(out.i.host_name, name_len)) return false;
    out.i.host_name[name_len] = '\0';
    out.i.host_name_size = name_len;

    size_t num;
    if (!read(&num, sizeof(size_t))) return false;
    out.i.display_num = num;

    out.i.display_list = (char**)malloc(num * sizeof(char*));
    for (size_t i = 0; i < num; ++i) {
      size_t len;
      if (!read(&len, sizeof(size_t))) return false;
      if (offset + len > size) return false;
      out.i.display_list[i] = (char*)malloc(len + 1);
      memcpy(out.i.display_list[i], data + offset, len);
      out.i.display_list[i][len] = '\0';
      offset += len;
    }

    auto alloc_int_array = [&](int*& arr) {
      arr = (int*)malloc(num * sizeof(int));
      return read(arr, num * sizeof(int));
    };

    return alloc_int_array(out.i.left) && alloc_int_array(out.i.top) &&
           alloc_int_array(out.i.right) && alloc_int_array(out.i.bottom);
  }

  return true;
}

void Render::FreeRemoteAction(RemoteAction& action) {
  if (action.type == ControlType::host_infomation) {
    for (size_t i = 0; i < action.i.display_num; ++i) {
      free(action.i.display_list[i]);
    }
    free(action.i.display_list);
    free(action.i.left);
    free(action.i.top);
    free(action.i.right);
    free(action.i.bottom);

    action.i.display_list = nullptr;
    action.i.left = action.i.top = action.i.right = action.i.bottom = nullptr;
    action.i.display_num = 0;
  }
}

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
      area->x < window_width - 120 && area->x > MOUSE_GRAB_PADDING &&
      !render->is_tab_bar_hovered_) {
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
  std::ofstream cd_cache_file(cache_path_ + "/secure_cache.enc",
                              std::ios::binary);
  if (!cd_cache_file) {
    cd_cache_mutex_.unlock();
    return -1;
  }

  memset(&cd_cache_.client_id_with_password, 0,
         sizeof(cd_cache_.client_id_with_password));
  memcpy(cd_cache_.client_id_with_password, client_id_with_password_,
         sizeof(client_id_with_password_));
  memcpy(&cd_cache_.key, &aes128_key_, sizeof(aes128_key_));
  memcpy(&cd_cache_.iv, &aes128_iv_, sizeof(aes128_iv_));

  cd_cache_file.write(reinterpret_cast<char*>(&cd_cache_), sizeof(CDCache));
  cd_cache_file.close();
  cd_cache_mutex_.unlock();

  return 0;
}

int Render::LoadSettingsFromCacheFile() {
  cd_cache_mutex_.lock();
  std::ifstream cd_cache_file(cache_path_ + "/secure_cache.enc",
                              std::ios::binary);
  if (!cd_cache_file) {
    cd_cache_mutex_.unlock();

    memset(password_saved_, 0, sizeof(password_saved_));
    memset(aes128_key_, 0, sizeof(aes128_key_));
    memset(aes128_iv_, 0, sizeof(aes128_iv_));

    thumbnail_.reset();
    thumbnail_ = std::make_unique<Thumbnail>(cache_path_ + "/thumbnails/");
    thumbnail_->GetKeyAndIv(aes128_key_, aes128_iv_);
    thumbnail_->DeleteAllFilesInDirectory();

    SaveSettingsIntoCacheFile();

    return -1;
  }

  cd_cache_file.read(reinterpret_cast<char*>(&cd_cache_), sizeof(CDCache));
  cd_cache_file.close();
  cd_cache_mutex_.unlock();

  memset(&client_id_with_password_, 0, sizeof(client_id_with_password_));
  memcpy(client_id_with_password_, cd_cache_.client_id_with_password,
         sizeof(client_id_with_password_));

  if (strchr(client_id_with_password_, '@') != nullptr) {
    std::string id, password;
    const char* at_pos = strchr(client_id_with_password_, '@');
    if (at_pos == nullptr) {
      id = client_id_with_password_;
      password.clear();
    } else {
      id.assign(client_id_with_password_, at_pos - client_id_with_password_);
      password = at_pos + 1;
    }

    memset(&client_id_, 0, sizeof(client_id_));
    strncpy(client_id_, id.c_str(), sizeof(client_id_) - 1);
    client_id_[sizeof(client_id_) - 1] = '\0';

    memset(&password_saved_, 0, sizeof(password_saved_));
    strncpy(password_saved_, password.c_str(), sizeof(password_saved_) - 1);
    password_saved_[sizeof(password_saved_) - 1] = '\0';
  }

  memcpy(aes128_key_, cd_cache_.key, sizeof(cd_cache_.key));
  memcpy(aes128_iv_, cd_cache_.iv, sizeof(cd_cache_.iv));

  thumbnail_.reset();
  thumbnail_ = std::make_unique<Thumbnail>(cache_path_ + "/thumbnails/",
                                           aes128_key_, aes128_iv_);

  language_button_value_ = (int)config_center_->GetLanguage();
  video_quality_button_value_ = (int)config_center_->GetVideoQuality();
  video_frame_rate_button_value_ = (int)config_center_->GetVideoFrameRate();
  video_encode_format_button_value_ =
      (int)config_center_->GetVideoEncodeFormat();
  enable_hardware_video_codec_ = config_center_->IsHardwareVideoCodec();
  enable_turn_ = config_center_->IsEnableTurn();
  enable_srtp_ = config_center_->IsEnableSrtp();

  language_button_value_last_ = language_button_value_;
  video_quality_button_value_last_ = video_quality_button_value_;
  video_encode_format_button_value_last_ = video_encode_format_button_value_;
  enable_hardware_video_codec_last_ = enable_hardware_video_codec_;
  enable_turn_last_ = enable_turn_;
  enable_srtp_last_ = enable_srtp_;

  LOG_INFO("Load settings from cache file");

  return 0;
}

int Render::ScreenCapturerInit() {
  if (!screen_capturer_) {
    screen_capturer_ = (ScreenCapturer*)screen_capturer_factory_->Create();
  }

  last_frame_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
  int fps = config_center_->GetVideoFrameRate() ==
                    ConfigCenter::VIDEO_FRAME_RATE::FPS_30
                ? 30
                : 60;
  LOG_INFO("Init screen capturer with {} fps", fps);

  int screen_capturer_init_ret = screen_capturer_->Init(
      fps,
      [this, fps](unsigned char* data, int size, int width, int height,
                  const char* display_name) -> void {
        auto now_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
        auto duration = now_time - last_frame_time_;
        if (duration * fps >= 1000) {  // ~60 FPS
          XVideoFrame frame;
          frame.data = (const char*)data;
          frame.size = size;
          frame.width = width;
          frame.height = height;
          frame.captured_timestamp = GetSystemTimeMicros(peer_);
          SendVideoFrame(peer_, &frame, display_name);
          last_frame_time_ = now_time;
        }
      });

  if (0 == screen_capturer_init_ret) {
    LOG_INFO("Init screen capturer success");
    if (display_info_list_.empty()) {
      display_info_list_ = screen_capturer_->GetDisplayInfoList();
    }
    return 0;
  } else {
    LOG_ERROR("Init screen capturer failed");
    screen_capturer_->Destroy();
    delete screen_capturer_;
    screen_capturer_ = nullptr;
    return -1;
  }
}

int Render::StartScreenCapturer() {
  if (screen_capturer_) {
    LOG_INFO("Start screen capturer");
    screen_capturer_->Start();
  }

  return 0;
}

int Render::StopScreenCapturer() {
  if (screen_capturer_) {
    LOG_INFO("Stop screen capturer");
    screen_capturer_->Stop();
  }

  return 0;
}

int Render::StartSpeakerCapturer() {
  if (!speaker_capturer_) {
    speaker_capturer_ = (SpeakerCapturer*)speaker_capturer_factory_->Create();
    int speaker_capturer_init_ret =
        speaker_capturer_->Init([this](unsigned char* data, size_t size,
                                       const char* audio_name) -> void {
          SendAudioFrame(peer_, (const char*)data, size, audio_label_.c_str());
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

  int mouse_controller_init_ret = mouse_controller_->Init(display_info_list_);
  if (0 != mouse_controller_init_ret) {
    LOG_INFO("Destroy mouse controller");
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

  std::string server_ip;
  int server_port;
  std::string server_cert_path;

  if (config_center_->IsSelfHosted()) {
    server_ip = config_center_->GetServerHost();
    server_port = config_center_->GetServerPort();
    server_cert_path = config_center_->GetCertFilePath();
  } else {
    server_ip = config_center_->GetDefaultServerHost();
    server_port = config_center_->GetDefaultServerPort();
    server_cert_path = config_center_->GetDefaultCertFilePath();
  }

  strncpy((char*)params_.signal_server_ip, server_ip.c_str(),
          sizeof(params_.signal_server_ip) - 1);
  params_.signal_server_ip[sizeof(params_.signal_server_ip) - 1] = '\0';
  params_.signal_server_port = server_port;
  strncpy((char*)params_.stun_server_ip, server_ip.c_str(),
          sizeof(params_.stun_server_ip) - 1);
  params_.stun_server_ip[sizeof(params_.stun_server_ip) - 1] = '\0';
  params_.stun_server_port = 3478;
  strncpy((char*)params_.turn_server_ip, server_ip.c_str(),
          sizeof(params_.turn_server_ip) - 1);
  params_.turn_server_ip[sizeof(params_.turn_server_ip) - 1] = '\0';
  params_.turn_server_port = 3478;
  strncpy((char*)params_.turn_server_username, "crossdesk",
          sizeof(params_.turn_server_username) - 1);
  params_.turn_server_username[sizeof(params_.turn_server_username) - 1] = '\0';
  strncpy((char*)params_.turn_server_password, "crossdeskpw",
          sizeof(params_.turn_server_password) - 1);
  params_.turn_server_password[sizeof(params_.turn_server_password) - 1] = '\0';
  strncpy(params_.tls_cert_path, server_cert_path.c_str(),
          sizeof(params_.tls_cert_path) - 1);
  params_.tls_cert_path[sizeof(params_.tls_cert_path) - 1] = '\0';

  strncpy(params_.log_path, dll_log_path_.c_str(),
          sizeof(params_.log_path) - 1);
  params_.log_path[sizeof(params_.log_path) - 1] = '\0';
  params_.hardware_acceleration = config_center_->IsHardwareVideoCodec();
  params_.av1_encoding = config_center_->GetVideoEncodeFormat() ==
                                 ConfigCenter::VIDEO_ENCODE_FORMAT::AV1
                             ? true
                             : false;
  params_.enable_turn = config_center_->IsEnableTurn();
  params_.enable_srtp = config_center_->IsEnableSrtp();
  params_.on_receive_video_buffer = nullptr;
  params_.on_receive_audio_buffer = OnReceiveAudioBufferCb;
  params_.on_receive_data_buffer = OnReceiveDataBufferCb;

  params_.on_receive_video_frame = OnReceiveVideoBufferCb;

  params_.on_signal_status = OnSignalStatusCb;
  params_.on_connection_status = OnConnectionStatusCb;
  params_.net_status_report = NetStatusReport;

  params_.user_id = client_id_with_password_;
  params_.user_data = this;

  peer_ = CreatePeer(&params_);
  if (peer_) {
    LOG_INFO("Create peer instance [{}] successful", client_id_);
    Init(peer_);
    LOG_INFO("Peer [{}] init finish", client_id_);
  } else {
    LOG_INFO("Create peer [{}] instance failed", client_id_);
  }

  if (0 == ScreenCapturerInit()) {
    for (auto& display_info : display_info_list_) {
      AddVideoStream(peer_, display_info.name.c_str());
    }

    AddAudioStream(peer_, audio_label_.c_str());
    AddDataStream(peer_, data_label_.c_str());
    return 0;
  } else {
    return -1;
  }
}

int Render::AudioDeviceInit() {
  SDL_AudioSpec desired_out{};
  desired_out.freq = 48000;
  desired_out.format = SDL_AUDIO_S16;
  desired_out.channels = 1;

  output_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                             &desired_out, nullptr, nullptr);
  if (!output_stream_) {
    LOG_ERROR("Failed to open output stream: {}", SDL_GetError());
    return -1;
  }

  SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(output_stream_));

  return 0;
}

int Render::AudioDeviceDestroy() {
  if (output_stream_) {
    SDL_CloseAudioDevice(SDL_GetAudioStreamDevice(output_stream_));
    SDL_DestroyAudioStream(output_stream_);
    output_stream_ = nullptr;
  }
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

  if (!SDL_CreateWindowAndRenderer(
          "Remote Desk", (int)main_window_width_default_,
          (int)main_window_height_default_,
          SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_BORDERLESS |
              SDL_WINDOW_HIDDEN,
          &main_window_, &main_renderer_)) {
    LOG_ERROR("Error creating main_window_ and main_renderer_: {}",
              SDL_GetError());
    return -1;
  }

  SDL_SetWindowResizable(main_window_, false);

  // for window region action
  SDL_SetWindowHitTest(main_window_, HitTestCallback, this);

#if _WIN32
  SDL_PropertiesID props = SDL_GetWindowProperties(main_window_);
  HWND main_hwnd = (HWND)SDL_GetPointerProperty(
      props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);

  HICON tray_icon = (HICON)LoadImageW(NULL, L"crossdesk.ico", IMAGE_ICON, 0, 0,
                                      LR_LOADFROMFILE | LR_DEFAULTSIZE);
  tray_ = std::make_unique<WinTray>(main_hwnd, tray_icon, L"CrossDesk",
                                    localization_language_index_);
#endif

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

  if (!SDL_CreateWindowAndRenderer(
          "Stream window", (int)stream_window_width_default_,
          (int)stream_window_height_default_,
          SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_BORDERLESS,
          &stream_window_, &stream_renderer_)) {
    LOG_ERROR("Error creating stream_window_ and stream_renderer_: {}",
              SDL_GetError());
    return -1;
  }

  stream_pixformat_ = SDL_PIXELFORMAT_NV12;

  SDL_SetWindowResizable(stream_window_, true);

  // for window region action
  SDL_SetWindowHitTest(stream_window_, HitTestCallback, this);

  // change props->stream_render_rect_
  SDL_Event event;
  event.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
  event.window.windowID = SDL_GetWindowID(stream_window_);
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

  io.IniFilename = NULL;  // disable imgui.ini

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

  SDL_GetWindowSizeInPixels(main_window_, &main_window_width_real_,
                            &main_window_height_real_);
  main_window_dpi_scaling_w_ = main_window_width_real_ / main_window_width_;
  main_window_dpi_scaling_h_ = main_window_width_real_ / main_window_width_;
  SDL_SetRenderScale(main_renderer_, main_window_dpi_scaling_w_,
                     main_window_dpi_scaling_h_);
  LOG_INFO("Use dpi scaling [{}x{}] for main window",
           main_window_dpi_scaling_w_, main_window_dpi_scaling_h_);

  ImGui_ImplSDL3_InitForSDLRenderer(main_window_, main_renderer_);
  ImGui_ImplSDLRenderer3_Init(main_renderer_);

  return 0;
}

int Render::DestroyMainWindowContext() {
  ImGui::SetCurrentContext(main_ctx_);
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
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

  SDL_GetWindowSizeInPixels(stream_window_, &stream_window_width_real_,
                            &stream_window_height_real_);

  stream_window_dpi_scaling_w_ =
      stream_window_width_real_ / stream_window_width_;
  stream_window_dpi_scaling_h_ =
      stream_window_width_real_ / stream_window_width_;

  SDL_SetRenderScale(stream_renderer_, stream_window_dpi_scaling_w_,
                     stream_window_dpi_scaling_h_);
  LOG_INFO("Use dpi scaling [{}x{}] for stream window",
           stream_window_dpi_scaling_w_, stream_window_dpi_scaling_h_);

  ImGui_ImplSDL3_InitForSDLRenderer(stream_window_, stream_renderer_);
  ImGui_ImplSDLRenderer3_Init(stream_renderer_);

  stream_window_inited_ = true;
  LOG_INFO("Stream window inited");

  return 0;
}

int Render::DestroyStreamWindowContext() {
  stream_window_inited_ = false;
  ImGui::SetCurrentContext(stream_ctx_);
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext(stream_ctx_);

  return 0;
}

int Render::DrawMainWindow() {
  if (!main_ctx_) {
    LOG_ERROR("Main context is null");
    return -1;
  }

  ImGui::SetCurrentContext(main_ctx_);
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
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
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), main_renderer_);
  SDL_RenderPresent(main_renderer_);

  return 0;
}

int Render::DrawStreamWindow() {
  if (!stream_ctx_) {
    LOG_ERROR("Stream context is null");
    return -1;
  }

  ImGui::SetCurrentContext(stream_ctx_);
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  StreamWindow();

  if (!fullscreen_button_pressed_) {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(stream_window_width_,
               fullscreen_button_pressed_ ? 0 : title_bar_height_),
        ImGuiCond_Always);
    ImGui::Begin("StreamWindowTitleBar", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoDocking);

    ImGui::PopStyleColor();

    TitleBar(false);
    ImGui::End();
  }

  // Rendering
  ImGui::Render();
  SDL_RenderClear(stream_renderer_);

  for (auto& it : client_properties_) {
    auto props = it.second;
    if (props->tab_selected_) {
      SDL_FRect render_rect_f = {
          static_cast<float>(props->stream_render_rect_.x),
          static_cast<float>(props->stream_render_rect_.y),
          static_cast<float>(props->stream_render_rect_.w),
          static_cast<float>(props->stream_render_rect_.h)};
      SDL_RenderTexture(stream_renderer_, props->stream_texture_, NULL,
                        &render_rect_f);
    }
  }
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), stream_renderer_);
  SDL_RenderPresent(stream_renderer_);

  return 0;
}

int Render::Run() {
  path_manager_ = std::make_unique<PathManager>("CrossDesk");
  if (path_manager_) {
    cert_path_ =
        (path_manager_->GetCertPath() / "crossdesk.cn_root.crt").string();
    exec_log_path_ = path_manager_->GetLogPath().string();
    dll_log_path_ = path_manager_->GetLogPath().string();
    cache_path_ = path_manager_->GetCachePath().string();
    config_center_ =
        std::make_unique<ConfigCenter>(cache_path_ + "/config.ini", cert_path_);
    strncpy(signal_server_ip_tmp_, config_center_->GetServerHost().c_str(),
            sizeof(signal_server_ip_tmp_) - 1);
    signal_server_ip_tmp_[sizeof(signal_server_ip_tmp_) - 1] = '\0';
    strncpy(signal_server_port_tmp_,
            std::to_string(config_center_->GetServerPort()).c_str(),
            sizeof(signal_server_port_tmp_) - 1);
    signal_server_port_tmp_[sizeof(signal_server_port_tmp_) - 1] = '\0';
    strncpy(cert_file_path_, cert_path_.c_str(), sizeof(cert_file_path_) - 1);
    cert_file_path_[sizeof(cert_file_path_) - 1] = '\0';
  } else {
    std::cerr << "Failed to create PathManager" << std::endl;
    return -1;
  }

  InitializeLogger();
  InitializeSettings();
  InitializeSDL();
  InitializeModules();
  InitializeMainWindow();

  const int scaled_video_width_ = 160;
  const int scaled_video_height_ = 90;

  MainLoop();

  Cleanup();

  return 0;
}

void Render::InitializeLogger() { InitLogger(exec_log_path_); }

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
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    LOG_ERROR("Error: {}", SDL_GetError());
    return;
  }

  const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(0);
  if (dm) {
    screen_width_ = dm->w;
    screen_height_ = dm->h;
  }

  STREAM_REFRESH_EVENT = SDL_RegisterEvents(1);
  if (STREAM_REFRESH_EVENT == (uint32_t)-1) {
    LOG_ERROR("Failed to register custom SDL event");
  }

  LOG_INFO("Screen resolution: [{}x{}]", screen_width_, screen_height_);
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
    if (!peer_) {
      CreateConnectionPeer();
    }

    SDL_Event event;
    if (SDL_WaitEventTimeout(&event, sdl_refresh_ms_)) {
      ProcessSdlEvent(event);
    }

#if _WIN32
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
#endif

    UpdateLabels();
    HandleRecentConnections();
    HandleStreamWindow();

    DrawMainWindow();
    if (stream_window_inited_) {
      DrawStreamWindow();
    }

    UpdateInteractions();

    if (need_to_send_host_info_) {
      RemoteAction remote_action;
      remote_action.i.display_num = display_info_list_.size();
      remote_action.i.display_list =
          (char**)malloc(remote_action.i.display_num * sizeof(char*));
      remote_action.i.left =
          (int*)malloc(remote_action.i.display_num * sizeof(int));
      remote_action.i.top =
          (int*)malloc(remote_action.i.display_num * sizeof(int));
      remote_action.i.right =
          (int*)malloc(remote_action.i.display_num * sizeof(int));
      remote_action.i.bottom =
          (int*)malloc(remote_action.i.display_num * sizeof(int));
      for (int i = 0; i < remote_action.i.display_num; i++) {
        LOG_INFO("Local display [{}:{}]", i + 1, display_info_list_[i].name);
        remote_action.i.display_list[i] =
            (char*)malloc(display_info_list_[i].name.length() + 1);
        strncpy(remote_action.i.display_list[i],
                display_info_list_[i].name.c_str(),
                display_info_list_[i].name.length());
        remote_action.i.display_list[i][display_info_list_[i].name.length()] =
            '\0';
        remote_action.i.left[i] = display_info_list_[i].left;
        remote_action.i.top[i] = display_info_list_[i].top;
        remote_action.i.right[i] = display_info_list_[i].right;
        remote_action.i.bottom[i] = display_info_list_[i].bottom;
      }

      std::string host_name = GetHostName();
      remote_action.type = ControlType::host_infomation;
      memcpy(&remote_action.i.host_name, host_name.data(), host_name.size());
      remote_action.i.host_name[host_name.size()] = '\0';
      remote_action.i.host_name_size = host_name.size();

      std::vector<char> serialized = SerializeRemoteAction(remote_action);
      int ret = SendDataFrame(peer_, serialized.data(), serialized.size(),
                              data_label_.c_str());
      FreeRemoteAction(remote_action);
      if (0 == ret) {
        need_to_send_host_info_ = false;
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
      SDL_SetWindowMouseGrab(stream_window_, true);
      stream_window_grabbed_ = true;
    } else if (stream_window_grabbed_ && !control_mouse_) {
      SDL_SetWindowMouseGrab(stream_window_, false);
      stream_window_grabbed_ = false;
    }
  }
}

void Render::Cleanup() {
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

  if (keyboard_capturer_) {
    delete keyboard_capturer_;
    keyboard_capturer_ = nullptr;
  }

  CleanupFactories();
  CleanupPeers();
  AudioDeviceDestroy();
  DestroyMainWindowContext();
  DestroyMainWindow();
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

void Render::CleanupPeer(std::shared_ptr<SubStreamWindowProperties> props) {
  SDL_FlushEvent(STREAM_REFRESH_EVENT);

  if (props->dst_buffer_) {
    thumbnail_->SaveToThumbnail(
        (char*)props->dst_buffer_, props->video_width_, props->video_height_,
        props->remote_id_, props->remote_host_name_,
        props->remember_password_ ? props->remote_password_ : "");
  }

  if (props->peer_) {
    LOG_INFO("[{}] Leave connection [{}]", props->local_id_, props->remote_id_);
    LeaveConnection(props->peer_, props->remote_id_.c_str());
    LOG_INFO("Destroy peer [{}]", props->local_id_);
    DestroyPeer(&props->peer_);
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
    CleanupPeer(props);
  }

  client_properties_.clear();
}

void Render::CleanSubStreamWindowProperties(
    std::shared_ptr<SubStreamWindowProperties> props) {
  if (props->stream_texture_) {
    SDL_DestroyTexture(props->stream_texture_);
    props->stream_texture_ = nullptr;
  }

  if (props->dst_buffer_) {
    delete[] props->dst_buffer_;
    props->dst_buffer_ = nullptr;
  }
}

void Render::UpdateRenderRect() {
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

    float video_ratio =
        (float)props->video_width_ / (float)props->video_height_;
    float video_ratio_reverse =
        (float)props->video_height_ / (float)props->video_width_;

    float render_area_width = props->render_window_width_;
    float render_area_height = props->render_window_height_;

    props->stream_render_rect_last_ = props->stream_render_rect_;
    if (render_area_width < render_area_height * video_ratio) {
      props->stream_render_rect_ = {
          (int)props->render_window_x_,
          (int)(abs(render_area_height -
                    render_area_width * video_ratio_reverse) /
                    2 +
                (int)props->render_window_y_),
          (int)render_area_width,
          (int)(render_area_width * video_ratio_reverse)};
    } else if (render_area_width > render_area_height * video_ratio) {
      props->stream_render_rect_ = {
          (int)abs(render_area_width - render_area_height * video_ratio) / 2,
          (int)props->render_window_y_, (int)(render_area_height * video_ratio),
          (int)render_area_height};
    } else {
      props->stream_render_rect_ = {
          (int)props->render_window_x_, (int)props->render_window_y_,
          (int)render_area_width, (int)render_area_height};
    }
  }
}

void Render::ProcessSdlEvent(const SDL_Event& event) {
  if (main_ctx_) {
    ImGui::SetCurrentContext(main_ctx_);
    ImGui_ImplSDL3_ProcessEvent(&event);
  } else {
    LOG_ERROR("Main context is null");
    return;
  }

  if (stream_window_inited_) {
    if (stream_ctx_) {
      ImGui::SetCurrentContext(stream_ctx_);
      ImGui_ImplSDL3_ProcessEvent(&event);
    } else {
      LOG_ERROR("Stream context is null");
      return;
    }
  }

  switch (event.type) {
    case SDL_EVENT_QUIT:
      if (stream_window_inited_) {
        LOG_INFO("Destroy stream window");
        SDL_SetWindowMouseGrab(stream_window_, false);
        DestroyStreamWindow();
        DestroyStreamWindowContext();

        for (auto& [host_name, props] : client_properties_) {
          thumbnail_->SaveToThumbnail(
              (char*)props->dst_buffer_, props->video_width_,
              props->video_height_, host_name, props->remote_host_name_,
              props->remember_password_ ? props->remote_password_ : "");

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
          SDL_SetWindowFullscreen(main_window_, false);
          SDL_FlushEvents(STREAM_REFRESH_EVENT, STREAM_REFRESH_EVENT);
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

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      if (event.window.windowID != SDL_GetWindowID(stream_window_)) {
        exit_ = true;
      }
      break;

    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      if (stream_window_created_ &&
          event.window.windowID == SDL_GetWindowID(stream_window_)) {
        UpdateRenderRect();
      }
      break;

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      if (stream_window_ &&
          SDL_GetWindowID(stream_window_) == event.window.windowID) {
        foucs_on_stream_window_ = true;
      } else if (main_window_ &&
                 SDL_GetWindowID(main_window_) == event.window.windowID) {
        foucs_on_main_window_ = true;
      }
      break;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
      if (stream_window_ &&
          SDL_GetWindowID(stream_window_) == event.window.windowID) {
        foucs_on_stream_window_ = false;
      } else if (main_window_ &&
                 SDL_GetWindowID(main_window_) == event.window.windowID) {
        foucs_on_main_window_ = false;
      }
      break;

    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
      if (foucs_on_stream_window_) {
        ProcessMouseEvent(event);
      }
      break;

    default:
      if (event.type == STREAM_REFRESH_EVENT) {
        auto* props = static_cast<SubStreamWindowProperties*>(event.user.data1);
        if (!props) {
          break;
        }
        if (props->video_width_ <= 0 || props->video_height_ <= 0) {
          break;
        }
        if (!props->dst_buffer_) {
          break;
        }

        if (props->stream_texture_) {
          if (props->video_width_ != props->texture_width_ ||
              props->video_height_ != props->texture_height_) {
            props->texture_width_ = props->video_width_;
            props->texture_height_ = props->video_height_;

            SDL_DestroyTexture(props->stream_texture_);
            // props->stream_texture_ = SDL_CreateTexture(
            //     stream_renderer_, stream_pixformat_,
            //     SDL_TEXTUREACCESS_STREAMING, props->texture_width_,
            //     props->texture_height_);

            SDL_PropertiesID nvProps = SDL_CreateProperties();
            SDL_SetNumberProperty(nvProps, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,
                                  props->texture_width_);
            SDL_SetNumberProperty(nvProps,
                                  SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,
                                  props->texture_height_);
            SDL_SetNumberProperty(nvProps,
                                  SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
                                  SDL_PIXELFORMAT_NV12);
            SDL_SetNumberProperty(nvProps,
                                  SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER,
                                  SDL_COLORSPACE_BT601_LIMITED);
            props->stream_texture_ =
                SDL_CreateTextureWithProperties(stream_renderer_, nvProps);
            SDL_DestroyProperties(nvProps);
          }
        } else {
          props->texture_width_ = props->video_width_;
          props->texture_height_ = props->video_height_;
          // props->stream_texture_ = SDL_CreateTexture(
          //     stream_renderer_, stream_pixformat_,
          //     SDL_TEXTUREACCESS_STREAMING, props->texture_width_,
          //     props->texture_height_);

          SDL_PropertiesID nvProps = SDL_CreateProperties();
          SDL_SetNumberProperty(nvProps, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,
                                props->texture_width_);
          SDL_SetNumberProperty(nvProps, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,
                                props->texture_height_);
          SDL_SetNumberProperty(nvProps, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
                                SDL_PIXELFORMAT_NV12);
          SDL_SetNumberProperty(nvProps,
                                SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER,
                                SDL_COLORSPACE_BT601_LIMITED);
          props->stream_texture_ =
              SDL_CreateTextureWithProperties(stream_renderer_, nvProps);
          SDL_DestroyProperties(nvProps);
        }

        SDL_UpdateTexture(props->stream_texture_, NULL, props->dst_buffer_,
                          props->texture_width_);
      }
      break;
  }
}