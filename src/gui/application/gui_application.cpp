#include "application/gui_application.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "localization.h"
#include "rd_log.h"
#include "version_checker.h"

namespace crossdesk {

GuiApplication::GuiApplication() = default;

GuiApplication::~GuiApplication() = default;

int GuiApplication::Run() {
  path_manager_ = std::make_unique<PathManager>("CrossDesk");
  if (path_manager_) {
    exec_log_path_ = path_manager_->GetLogPath().string();
    dll_log_path_ = path_manager_->GetLogPath().string();
    cache_path_ = path_manager_->GetCachePath().string();
    config_center_ =
        std::make_unique<ConfigCenter>(cache_path_ + "/config.ini");
    strncpy(signal_server_ip_self_,
            config_center_->GetSignalServerHost().c_str(),
            sizeof(signal_server_ip_self_) - 1);
    signal_server_ip_self_[sizeof(signal_server_ip_self_) - 1] = '\0';
    int signal_port_init = config_center_->GetSignalServerPort();
    if (signal_port_init > 0) {
      strncpy(signal_server_port_self_,
              std::to_string(signal_port_init).c_str(),
              sizeof(signal_server_port_self_) - 1);
      signal_server_port_self_[sizeof(signal_server_port_self_) - 1] = '\0';
    } else {
      signal_server_port_self_[0] = '\0';
    }
  } else {
    std::cerr << "Failed to create PathManager" << std::endl;
    return -1;
  }

  InitializeLogger();
  LOG_INFO("CrossDesk version: {}", CROSSDESK_VERSION);

  latest_version_info_ = CheckUpdate();
  if (!latest_version_info_.empty()) {
    std::string version;
    if (latest_version_info_.contains("latest_version") &&
        latest_version_info_["latest_version"].is_string()) {
      version = latest_version_info_["latest_version"].get<std::string>();
    } else if (latest_version_info_.contains("version") &&
               latest_version_info_["version"].is_string()) {
      version = latest_version_info_["version"].get<std::string>();
    }

    if (!version.empty()) {
      latest_version_ = 'v' + version;
    } else {
      latest_version_ = "";
    }
    if (latest_version_info_.contains("releaseNotes") &&
        latest_version_info_["releaseNotes"].is_string()) {
      release_notes_ = latest_version_info_["releaseNotes"].get<std::string>();
    } else {
      release_notes_ = "";
    }
    update_available_ =
        !version.empty() && IsNewerVersion(CROSSDESK_VERSION, latest_version_);
    LOG_INFO("Update check: current={}, latest={}, available={}",
             CROSSDESK_VERSION, latest_version_, update_available_);
    if (update_available_) {
      show_update_notification_window_ = true;
    }
  } else {
    latest_version_ = "";
    update_available_ = false;
    LOG_WARN("Update check skipped: version.json is empty or missing "
             "latest_version");
  }

  InitializeSettings();
  InitializeSDL();
  InitializeModules();
  InitializeMainWindow();

#if _WIN32 && CROSSDESK_PORTABLE
  CheckPortableWindowsService();
#endif

  const int scaled_video_width_ = 160;
  const int scaled_video_height_ = 90;

  MainLoop();

  Cleanup();

  return 0;
}

void GuiApplication::InitializeLogger() { InitLogger(exec_log_path_); }

void GuiApplication::InitializeSettings() {
  settings_.Load();
  settings_.LoadRecentConnectionAliases();

  localization_language_index_ =
      localization::detail::ClampLanguageIndex(language_button_value_);
  language_button_value_ = localization_language_index_;

  if (localization_language_index_ == 0) {
    localization_language_ = ConfigCenter::LANGUAGE::CHINESE;
  } else if (localization_language_index_ == 1) {
    localization_language_ = ConfigCenter::LANGUAGE::ENGLISH;
  } else {
    localization_language_ = ConfigCenter::LANGUAGE::RUSSIAN;
  }
}

void GuiApplication::InitializeSDL() {
#if defined(__linux__) && !defined(__APPLE__)
  if (!getenv("SDL_AUDIODRIVER")) {
    // Prefer PulseAudio first on Linux to avoid hard ALSA plugin dependency.
    setenv("SDL_AUDIODRIVER", "pulseaudio", 0);
  }
#endif

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    LOG_ERROR("Error: {}", SDL_GetError());
    return;
  }

  const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(0);
  if (dm) {
    screen_width_ = dm->w;
    screen_height_ = dm->h;
  }

  const uint32_t custom_event_base = SDL_RegisterEvents(3);
  if (custom_event_base == static_cast<uint32_t>(-1)) {
    LOG_ERROR("Failed to register custom SDL events");
  } else {
    STREAM_REFRESH_EVENT = custom_event_base;
    APP_EXIT_EVENT = custom_event_base + 1;
    clipboard_.SetEventType(custom_event_base + 2);
  }

  LOG_INFO("Screen resolution: [{}x{}]", screen_width_, screen_height_);
}

void GuiApplication::InitializeModules() {
  if (!modules_inited_) {
    devices_.Initialize();
    CreateConnectionPeer();

    modules_inited_ = true;
  }
}

void GuiApplication::InitializeMainWindow() {
  CreateMainWindow();
  clipboard_.Initialize();
  if (SDL_WINDOW_HIDDEN & SDL_GetWindowFlags(main_window_)) {
    SDL_ShowWindow(main_window_);
  }
}

void GuiApplication::MainLoop() {
  while (!exit_) {
    if (!peer_) {
      CreateConnectionPeer();
    }

    SDL_Event event;
    if (SDL_WaitEventTimeout(&event, sdl_refresh_ms_)) {
      ProcessSdlEvent(event);
    }
    // Also drain the pending value after a timeout so a full SDL event queue
    // cannot indefinitely delay a clipboard update received from the network.
    clipboard_.ApplyPendingRemoteText();

#if _WIN32
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
#elif defined(__linux__) && !defined(__APPLE__)
    if (tray_) {
      tray_->ProcessEvents();
    }
#endif

    UpdateLabels();
    HandleRecentConnections();
    HandleConnectionStatusChange();
    HandlePendingPresenceProbe();
    HandleConnectionTimeouts();
    HandleStreamWindow();
    HandleServerWindow();
    HandleWindowsServiceIntegration();

    const bool main_window_visible =
        main_window_ && !(SDL_GetWindowFlags(main_window_) & SDL_WINDOW_HIDDEN);
    if (main_window_visible) {
      DrawMainWindow();
    }
    if (stream_window_inited_) {
      DrawStreamWindow();
    }

    if (is_server_mode_) {
      DrawServerWindow();
    }

    devices_.UpdateInteractions();
  }
}

void GuiApplication::HandleStreamWindow() {
  if (need_to_create_stream_window_) {
    CreateStreamWindow();
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

void GuiApplication::HandleServerWindow() {
  if (need_to_create_server_window_) {
    CreateServerWindow();
    need_to_create_server_window_ = false;
  }

  if (need_to_destroy_server_window_) {
    DestroyServerWindow();
    DestroyServerWindowContext();
    need_to_destroy_server_window_ = false;
  }
}

void GuiApplication::Cleanup() {
  clipboard_.Shutdown();

  devices_.DestroyDevices();
  devices_.DestroyFactories();
  CloseAllRemoteSessions();

#if _WIN32 && CROSSDESK_PORTABLE
  JoinPortableWindowsServiceInstallThread();
#endif

  WaitForThumbnailSaveTasks();

  devices_.DestroyAudioOutput();
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
  tray_.reset();
#endif

  if (stream_window_created_) {
    if (stream_window_) {
      SDL_SetWindowMouseGrab(stream_window_, false);
    }
    DestroyStreamWindow();
  }
  if (stream_ctx_) {
    DestroyStreamWindowContext();
  }

  if (server_window_created_) {
    DestroyServerWindow();
  }
  if (server_ctx_) {
    DestroyServerWindowContext();
  }

  DestroyMainWindowContext();
  DestroyMainWindow();
  SDL_Quit();
}


} // namespace crossdesk
