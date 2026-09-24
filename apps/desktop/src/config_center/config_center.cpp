#include "config_center.h"

#include "autostart.h"
#include "rd_log.h"

namespace crossdesk {

namespace {

bool IsValidTurnModeValue(long value) {
  return value >= static_cast<long>(ConfigCenter::TURN_MODE::DISABLED) &&
         value <= static_cast<long>(ConfigCenter::TURN_MODE::FORCE_TCP);
}

}  // namespace

ConfigCenter::ConfigCenter(const std::string& config_path)
    : config_path_(config_path) {
  ini_.SetUnicode(true);
  Load();
}

ConfigCenter::~ConfigCenter() {}

int ConfigCenter::Load() {
  SI_Error rc = ini_.LoadFile(config_path_.c_str());
  if (rc < 0) {
    Save();
    return -1;
  }

  bool persist_config_migration = false;
  // These options now belong only to the active remote connection. Discard
  // legacy local defaults so they cannot affect this host or new connections.
  persist_config_migration |= ini_.Delete(section_, "video_quality");
  persist_config_migration |= ini_.Delete(section_, "video_frame_rate");
  persist_config_migration |= ini_.Delete(section_, "video_adaptation_policy");
  persist_config_migration |= ini_.Delete(section_, "video_content_type");
  persist_config_migration |= ini_.Delete(section_, "screen_content");
  persist_config_migration |=
      ini_.Delete(section_, "enable_desktop_quality_optimization");
  persist_config_migration |=
      ini_.Delete(section_, "enable_minimize_to_tray");

  const long language_value =
      ini_.GetLongValue(section_, "language", static_cast<long>(language_));
  if (language_value < static_cast<long>(LANGUAGE::CHINESE) ||
      language_value > static_cast<long>(LANGUAGE::RUSSIAN)) {
    language_ = LANGUAGE::ENGLISH;
  } else {
    language_ = static_cast<LANGUAGE>(language_value);
  }

  const long screen_capture_method_value = ini_.GetLongValue(
      section_, "screen_capture_method",
      static_cast<long>(ScreenCaptureMethod::Auto));
  screen_capture_method_ = IsValidScreenCaptureMethod(screen_capture_method_value)
                               ? static_cast<ScreenCaptureMethod>(
                                     screen_capture_method_value)
                               : ScreenCaptureMethod::Auto;

  video_encode_format_ = static_cast<VIDEO_ENCODE_FORMAT>(
      ini_.GetLongValue(section_, "video_encode_format",
                        static_cast<long>(video_encode_format_)));

  hardware_video_codec_ = ini_.GetBoolValue(section_, "hardware_video_codec",
                                            hardware_video_codec_);
  const char* turn_mode_value = ini_.GetValue(section_, "turn_mode", nullptr);
  if (turn_mode_value != nullptr && strlen(turn_mode_value) > 0) {
    const long parsed_turn_mode = ini_.GetLongValue(
        section_, "turn_mode", static_cast<long>(turn_mode_));
    if (IsValidTurnModeValue(parsed_turn_mode)) {
      turn_mode_ = static_cast<TURN_MODE>(parsed_turn_mode);
    } else {
      LOG_WARN("Invalid TURN mode [{}], using auto UDP/TCP",
               parsed_turn_mode);
      turn_mode_ = TURN_MODE::AUTO_UDP_TCP;
    }
  } else {
    const bool legacy_enable_turn = ini_.GetBoolValue(
        section_, "enable_turn", turn_mode_ != TURN_MODE::DISABLED);
    turn_mode_ = legacy_enable_turn ? TURN_MODE::AUTO_UDP_TCP
                                    : TURN_MODE::DISABLED;
    ini_.SetLongValue(section_, "turn_mode", static_cast<long>(turn_mode_));
    persist_config_migration = true;
  }
  // Migrate the former user switch. Keep true in the file for older builds,
  // while the current application always requests encrypted native media.
  if (!ini_.GetBoolValue(section_, "enable_srtp", false)) {
    ini_.SetBoolValue(section_, "enable_srtp", true);
    persist_config_migration = true;
  }
  enable_self_hosted_ =
      ini_.GetBoolValue(section_, "enable_self_hosted", enable_self_hosted_);

  const char* signal_server_host_value =
      ini_.GetValue(section_, "signal_server_host", nullptr);
  if (signal_server_host_value != nullptr &&
      strlen(signal_server_host_value) > 0) {
    signal_server_host_ = signal_server_host_value;
  } else {
    signal_server_host_ = "";
  }
  const char* signal_server_port_value =
      ini_.GetValue(section_, "signal_server_port", nullptr);
  if (signal_server_port_value != nullptr &&
      strlen(signal_server_port_value) > 0) {
    signal_server_port_ =
        static_cast<int>(ini_.GetLongValue(section_, "signal_server_port", 0));
  } else {
    signal_server_port_ = 0;
  }
  enable_autostart_ =
      ini_.GetBoolValue(section_, "enable_autostart", enable_autostart_);
  enable_daemon_ = ini_.GetBoolValue(section_, "enable_daemon", enable_daemon_);
  enable_privacy_screen_.store(
      ini_.GetBoolValue(section_, "enable_privacy_screen", true));
  portable_service_prompt_suppressed_ =
      ini_.GetBoolValue(section_, "portable_service_prompt_suppressed",
                        portable_service_prompt_suppressed_);

  const char* file_transfer_save_path_value =
      ini_.GetValue(section_, "file_transfer_save_path", nullptr);
  if (file_transfer_save_path_value != nullptr &&
      strlen(file_transfer_save_path_value) > 0) {
    file_transfer_save_path_ = file_transfer_save_path_value;
  } else {
    file_transfer_save_path_ = "";
  }

  if (persist_config_migration && ini_.SaveFile(config_path_.c_str()) < 0) {
    return -1;
  }

  return 0;
}

int ConfigCenter::Save() {
  ini_.SetLongValue(section_, "language", static_cast<long>(language_));
  ini_.SetLongValue(section_, "screen_capture_method",
                    static_cast<long>(screen_capture_method_));
  ini_.SetLongValue(section_, "video_encode_format",
                    static_cast<long>(video_encode_format_));
  ini_.SetBoolValue(section_, "hardware_video_codec", hardware_video_codec_);
  ini_.SetLongValue(section_, "turn_mode", static_cast<long>(turn_mode_));
  ini_.SetBoolValue(section_, "enable_turn",
                    turn_mode_ != TURN_MODE::DISABLED);
  ini_.SetBoolValue(section_, "enable_srtp", true);
  ini_.SetBoolValue(section_, "enable_self_hosted", enable_self_hosted_);

  // only save when self hosted
  if (enable_self_hosted_) {
    ini_.SetValue(section_, "signal_server_host", signal_server_host_.c_str());
    ini_.SetLongValue(section_, "signal_server_port",
                      static_cast<long>(signal_server_port_));
  }

  ini_.SetBoolValue(section_, "enable_autostart", enable_autostart_);
  ini_.SetBoolValue(section_, "enable_daemon", enable_daemon_);
  ini_.SetBoolValue(section_, "enable_privacy_screen",
                    enable_privacy_screen_.load());
  ini_.SetBoolValue(section_, "portable_service_prompt_suppressed",
                    portable_service_prompt_suppressed_);

  ini_.SetValue(section_, "file_transfer_save_path",
                file_transfer_save_path_.c_str());

  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }

  return 0;
}

// setters

int ConfigCenter::SetLanguage(LANGUAGE language) {
  language_ = language;
  ini_.SetLongValue(section_, "language", static_cast<long>(language_));
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetScreenCaptureMethod(ScreenCaptureMethod method) {
  if (!IsValidScreenCaptureMethod(static_cast<long>(method))) {
    return -1;
  }
  ini_.SetLongValue(section_, "screen_capture_method", static_cast<long>(method));
  if (ini_.SaveFile(config_path_.c_str()) < 0) {
    ini_.SetLongValue(section_, "screen_capture_method",
                      static_cast<long>(screen_capture_method_));
    return -1;
  }
  screen_capture_method_ = method;
  return 0;
}

int ConfigCenter::SetVideoEncodeFormat(
    VIDEO_ENCODE_FORMAT video_encode_format) {
  video_encode_format_ = video_encode_format;
  ini_.SetLongValue(section_, "video_encode_format",
                    static_cast<long>(video_encode_format_));
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetHardwareVideoCodec(bool hardware_video_codec) {
  hardware_video_codec_ = hardware_video_codec;
  ini_.SetBoolValue(section_, "hardware_video_codec", hardware_video_codec_);
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetTurnMode(TURN_MODE turn_mode) {
  if (!IsValidTurnModeValue(static_cast<long>(turn_mode))) {
    return -1;
  }

  turn_mode_ = turn_mode;
  ini_.SetLongValue(section_, "turn_mode", static_cast<long>(turn_mode_));
  ini_.SetBoolValue(section_, "enable_turn",
                    turn_mode_ != TURN_MODE::DISABLED);
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetTurn(bool enable_turn, bool force_relay) {
  if (!enable_turn) {
    return SetTurnMode(TURN_MODE::DISABLED);
  }
  if (!force_relay) {
    return SetTurnMode(TURN_MODE::AUTO_UDP_TCP);
  }
  // Use TURN/UDP by default, preserving an explicitly configured TCP relay.
  return SetTurnMode(turn_mode_ == TURN_MODE::FORCE_TCP ? TURN_MODE::FORCE_TCP
                                                     : TURN_MODE::FORCE_UDP);
}

int ConfigCenter::SetServerHost(const std::string& signal_server_host) {
  signal_server_host_ = signal_server_host;
  ini_.SetValue(section_, "signal_server_host", signal_server_host_.c_str());
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetServerPort(int signal_server_port) {
  signal_server_port_ = signal_server_port;
  ini_.SetLongValue(section_, "signal_server_port",
                    static_cast<long>(signal_server_port_));
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetSelfHosted(bool enable_self_hosted) {
  enable_self_hosted_ = enable_self_hosted;
  ini_.SetBoolValue(section_, "enable_self_hosted", enable_self_hosted_);

  // load from config if self hosted is enabled
  if (enable_self_hosted_) {
    const char* signal_server_host_value =
        ini_.GetValue(section_, "signal_server_host", nullptr);
    if (signal_server_host_value != nullptr &&
        strlen(signal_server_host_value) > 0) {
      signal_server_host_ = signal_server_host_value;
    }
    const char* signal_server_port_value =
        ini_.GetValue(section_, "signal_server_port", nullptr);
    if (signal_server_port_value != nullptr &&
        strlen(signal_server_port_value) > 0) {
      signal_server_port_ = static_cast<int>(
          ini_.GetLongValue(section_, "signal_server_port", 0));
    }
    ini_.SetValue(section_, "signal_server_host", signal_server_host_.c_str());
    ini_.SetLongValue(section_, "signal_server_port",
                      static_cast<long>(signal_server_port_));
  }

  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetAutostart(bool enable_autostart) {
  enable_autostart_ = enable_autostart;
  bool success = false;
  if (enable_autostart) {
    success = EnableAutostart("CrossDesk");
  } else {
    success = DisableAutostart("CrossDesk");
  }

  ini_.SetBoolValue(section_, "enable_autostart", enable_autostart_);
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }

  if (!success) {
    LOG_ERROR("SetAutostart failed");
    return -1;
  }

  return 0;
}

int ConfigCenter::SetDaemon(bool enable_daemon) {
  enable_daemon_ = enable_daemon;

  ini_.SetBoolValue(section_, "enable_daemon", enable_daemon_);
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }

  return 0;
}

int ConfigCenter::SetPrivacyScreen(bool enable_privacy_screen) {
  const bool previous = enable_privacy_screen_.load();
  if (previous == enable_privacy_screen) return 0;
  ini_.SetBoolValue(section_, "enable_privacy_screen", enable_privacy_screen);
  if (ini_.SaveFile(config_path_.c_str()) < 0) {
    ini_.SetBoolValue(section_, "enable_privacy_screen", previous);
    LOG_ERROR("Failed to save automatic privacy screen preference");
    return -1;
  }
  // RTC callbacks read only the value committed by the settings thread.
  enable_privacy_screen_.store(enable_privacy_screen);
  LOG_INFO("Privacy screen on incoming connection: {}", enable_privacy_screen);
  return 0;
}

int ConfigCenter::SetPortableServicePromptSuppressed(bool suppressed) {
  portable_service_prompt_suppressed_ = suppressed;
  ini_.SetBoolValue(section_, "portable_service_prompt_suppressed",
                    portable_service_prompt_suppressed_);
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }

  return 0;
}

// getters

ConfigCenter::LANGUAGE ConfigCenter::GetLanguage() const { return language_; }

ScreenCaptureMethod ConfigCenter::GetScreenCaptureMethod() const {
  return screen_capture_method_;
}

ConfigCenter::VIDEO_ENCODE_FORMAT ConfigCenter::GetVideoEncodeFormat() const {
  return video_encode_format_;
}

bool ConfigCenter::IsHardwareVideoCodecAvailable() {
#if (((defined(_WIN32) || defined(__linux__)) && !defined(__aarch64__) && \
      !defined(__arm__) && USE_CUDA) ||                                   \
     defined(__APPLE__))
  return true;
#else
  return false;
#endif
}

bool ConfigCenter::IsHardwareVideoCodec() const {
  return IsHardwareVideoCodecAvailable() && hardware_video_codec_;
}

ConfigCenter::TURN_MODE ConfigCenter::GetTurnMode() const {
  return turn_mode_;
}

bool ConfigCenter::IsEnableTurn() const {
  return turn_mode_ != TURN_MODE::DISABLED;
}

bool ConfigCenter::IsForceRelay() const {
  return turn_mode_ == TURN_MODE::FORCE_UDP || turn_mode_ == TURN_MODE::FORCE_TCP;
}

bool ConfigCenter::IsEnableSrtp() const { return true; }

std::string ConfigCenter::GetSignalServerHost() const {
  return signal_server_host_;
}

int ConfigCenter::GetSignalServerPort() const { return signal_server_port_; }

std::string ConfigCenter::GetDefaultServerHost() const {
  return signal_server_host_default_;
}

int ConfigCenter::GetDefaultSignalServerPort() const {
  return server_port_default_;
}

bool ConfigCenter::IsSelfHosted() const { return enable_self_hosted_; }

bool ConfigCenter::IsEnableAutostart() const { return enable_autostart_; }

bool ConfigCenter::IsEnableDaemon() const { return enable_daemon_; }

bool ConfigCenter::IsEnablePrivacyScreen() const {
  return enable_privacy_screen_.load();
}

bool ConfigCenter::IsPortableServicePromptSuppressed() const {
  return portable_service_prompt_suppressed_;
}

int ConfigCenter::SetFileTransferSavePath(const std::string& path) {
  file_transfer_save_path_ = path;
  ini_.SetValue(section_, "file_transfer_save_path",
                file_transfer_save_path_.c_str());
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

std::string ConfigCenter::GetFileTransferSavePath() const {
  return file_transfer_save_path_;
}
}  // namespace crossdesk
