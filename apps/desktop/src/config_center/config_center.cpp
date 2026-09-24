#include "config_center.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <optional>
#include <vector>

#include "SimpleIni.h"
#include "autostart.h"
#include "rd_log.h"

namespace crossdesk {

struct ConfigCenter::IniStorage : CSimpleIniA {};

namespace {

bool IsValidTurnModeValue(long value) {
  return value >= static_cast<long>(ConfigCenter::TURN_MODE::DISABLED) &&
         value <= static_cast<long>(ConfigCenter::TURN_MODE::FORCE_TCP);
}

}  // namespace

ConfigCenter::ConfigCenter(const std::string& config_path)
    : config_path_(config_path), ini_(std::make_unique<IniStorage>()) {
  ini_->SetUnicode(true);
  Load();
}

ConfigCenter::~ConfigCenter() {}

int ConfigCenter::Load() {
  SI_Error rc = ini_->LoadFile(config_path_.c_str());
  if (rc < 0) {
    Save();
    return -1;
  }

  bool persist_config_migration = false;
  // These options now belong only to the active remote connection. Discard
  // legacy local defaults so they cannot affect this host or new connections.
  persist_config_migration |= ini_->Delete(section_, "video_quality");
  persist_config_migration |= ini_->Delete(section_, "video_frame_rate");
  persist_config_migration |= ini_->Delete(section_, "video_adaptation_policy");
  persist_config_migration |= ini_->Delete(section_, "video_content_type");
  persist_config_migration |= ini_->Delete(section_, "screen_content");
  persist_config_migration |=
      ini_->Delete(section_, "enable_desktop_quality_optimization");
  persist_config_migration |=
      ini_->Delete(section_, "enable_minimize_to_tray");

  const long language_value =
      ini_->GetLongValue(section_, "language", static_cast<long>(language_));
  if (language_value < static_cast<long>(LANGUAGE::CHINESE) ||
      language_value > static_cast<long>(LANGUAGE::RUSSIAN)) {
    language_ = LANGUAGE::ENGLISH;
  } else {
    language_ = static_cast<LANGUAGE>(language_value);
  }

  const long screen_capture_method_value = ini_->GetLongValue(
      section_, "screen_capture_method",
      static_cast<long>(ScreenCaptureMethod::Auto));
  screen_capture_method_ = IsValidScreenCaptureMethod(screen_capture_method_value)
                               ? static_cast<ScreenCaptureMethod>(
                                     screen_capture_method_value)
                               : ScreenCaptureMethod::Auto;

  video_encode_format_ = static_cast<VIDEO_ENCODE_FORMAT>(
      ini_->GetLongValue(section_, "video_encode_format",
                        static_cast<long>(video_encode_format_)));

  hardware_video_codec_ = ini_->GetBoolValue(section_, "hardware_video_codec",
                                            hardware_video_codec_);
  const char* turn_mode_value = ini_->GetValue(section_, "turn_mode", nullptr);
  if (turn_mode_value != nullptr && strlen(turn_mode_value) > 0) {
    const long parsed_turn_mode = ini_->GetLongValue(
        section_, "turn_mode", static_cast<long>(turn_mode_));
    if (IsValidTurnModeValue(parsed_turn_mode)) {
      turn_mode_ = static_cast<TURN_MODE>(parsed_turn_mode);
    } else {
      LOG_WARN("Invalid TURN mode [{}], using auto UDP/TCP",
               parsed_turn_mode);
      turn_mode_ = TURN_MODE::AUTO_UDP_TCP;
    }
  } else {
    const bool legacy_enable_turn = ini_->GetBoolValue(
        section_, "enable_turn", turn_mode_ != TURN_MODE::DISABLED);
    turn_mode_ = legacy_enable_turn ? TURN_MODE::AUTO_UDP_TCP
                                    : TURN_MODE::DISABLED;
    ini_->SetLongValue(section_, "turn_mode", static_cast<long>(turn_mode_));
    persist_config_migration = true;
  }
  // Migrate the former user switch. Keep true in the file for older builds,
  // while the current application always requests encrypted native media.
  if (!ini_->GetBoolValue(section_, "enable_srtp", false)) {
    ini_->SetBoolValue(section_, "enable_srtp", true);
    persist_config_migration = true;
  }
  enable_self_hosted_ =
      ini_->GetBoolValue(section_, "enable_self_hosted", enable_self_hosted_);

  const char* signal_server_host_value =
      ini_->GetValue(section_, "signal_server_host", nullptr);
  if (signal_server_host_value != nullptr &&
      strlen(signal_server_host_value) > 0) {
    signal_server_host_ = signal_server_host_value;
  } else {
    signal_server_host_ = "";
  }
  const char* signal_server_port_value =
      ini_->GetValue(section_, "signal_server_port", nullptr);
  if (signal_server_port_value != nullptr &&
      strlen(signal_server_port_value) > 0) {
    signal_server_port_ =
        static_cast<int>(ini_->GetLongValue(section_, "signal_server_port", 0));
  } else {
    signal_server_port_ = 0;
  }
  enable_autostart_ =
      ini_->GetBoolValue(section_, "enable_autostart", enable_autostart_);
  enable_daemon_ = ini_->GetBoolValue(section_, "enable_daemon", enable_daemon_);
  enable_privacy_screen_.store(
      ini_->GetBoolValue(section_, "enable_privacy_screen", true));
  portable_service_prompt_suppressed_ =
      ini_->GetBoolValue(section_, "portable_service_prompt_suppressed",
                        portable_service_prompt_suppressed_);

  const char* file_transfer_save_path_value =
      ini_->GetValue(section_, "file_transfer_save_path", nullptr);
  if (file_transfer_save_path_value != nullptr &&
      strlen(file_transfer_save_path_value) > 0) {
    file_transfer_save_path_ = file_transfer_save_path_value;
  } else {
    file_transfer_save_path_ = "";
  }

  if (persist_config_migration && CommitIni() < 0) {
    return -1;
  }

  return 0;
}

int ConfigCenter::CommitIni() {
  // Write beside the destination and rename only after a complete save. A
  // failed write must not truncate the last usable configuration.
  static std::atomic<unsigned long> sequence{0};
  const std::string temporary = config_path_ + ".tmp-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
      "-" + std::to_string(sequence++);
  std::error_code error;
  if (ini_->SaveFile(temporary.c_str()) < 0) {
    LOG_ERROR("Failed to write configuration: {}", config_path_);
    std::filesystem::remove(temporary, error);
    return -1;
  }
  const auto previous = std::filesystem::status(config_path_, error);
  if (!error && std::filesystem::is_regular_file(previous)) {
    std::filesystem::permissions(temporary, previous.permissions(), error);
    if (error) {
      LOG_ERROR("Failed to preserve configuration permissions: {}", error.message());
      std::filesystem::remove(temporary, error);
      return -1;
    }
  }
  std::filesystem::rename(temporary, config_path_, error);
  if (!error) return 0;
  LOG_ERROR("Failed to replace configuration {}: {}", config_path_, error.message());
  std::filesystem::remove(temporary, error);
  return -1;
}

int ConfigCenter::StoreValues(
    std::initializer_list<std::pair<const char*, std::string>> values) {
  std::vector<std::pair<const char*, std::optional<std::string>>> previous;
  for (const auto& value : values) {
    const char* old = ini_->GetValue(section_, value.first, nullptr);
    previous.emplace_back(value.first, old ? std::optional<std::string>(old) : std::nullopt);
    ini_->SetValue(section_, value.first, value.second.c_str());
  }
  if (CommitIni() == 0) return 0;
  for (const auto& value : previous) {
    if (value.second) ini_->SetValue(section_, value.first, value.second->c_str());
    else ini_->Delete(section_, value.first);
  }
  return -1;
}

int ConfigCenter::Save() {
  ini_->SetLongValue(section_, "language", static_cast<long>(language_));
  ini_->SetLongValue(section_, "screen_capture_method",
                    static_cast<long>(screen_capture_method_));
  ini_->SetLongValue(section_, "video_encode_format",
                    static_cast<long>(video_encode_format_));
  ini_->SetBoolValue(section_, "hardware_video_codec", hardware_video_codec_);
  ini_->SetLongValue(section_, "turn_mode", static_cast<long>(turn_mode_));
  ini_->SetBoolValue(section_, "enable_turn",
                    turn_mode_ != TURN_MODE::DISABLED);
  ini_->SetBoolValue(section_, "enable_srtp", true);
  ini_->SetBoolValue(section_, "enable_self_hosted", enable_self_hosted_);

  // only save when self hosted
  if (enable_self_hosted_) {
    ini_->SetValue(section_, "signal_server_host", signal_server_host_.c_str());
    ini_->SetLongValue(section_, "signal_server_port",
                      static_cast<long>(signal_server_port_));
  }

  ini_->SetBoolValue(section_, "enable_autostart", enable_autostart_);
  ini_->SetBoolValue(section_, "enable_daemon", enable_daemon_);
  ini_->SetBoolValue(section_, "enable_privacy_screen",
                    enable_privacy_screen_.load());
  ini_->SetBoolValue(section_, "portable_service_prompt_suppressed",
                    portable_service_prompt_suppressed_);

  ini_->SetValue(section_, "file_transfer_save_path",
                file_transfer_save_path_.c_str());

  SI_Error rc = CommitIni();
  if (rc < 0) {
    return -1;
  }

  return 0;
}

// setters

int ConfigCenter::SetLanguage(LANGUAGE language) {
  const int value = static_cast<int>(language);
  if (value < 0 || value > 2 || StoreValues({{"language", std::to_string(value)}}) != 0) return -1;
  language_ = language;
  return 0;
}

int ConfigCenter::SetScreenCaptureMethod(ScreenCaptureMethod method) {
  if (!IsValidScreenCaptureMethod(static_cast<long>(method))) {
    return -1;
  }
  ini_->SetLongValue(section_, "screen_capture_method", static_cast<long>(method));
  if (CommitIni() < 0) {
    ini_->SetLongValue(section_, "screen_capture_method",
                      static_cast<long>(screen_capture_method_));
    return -1;
  }
  screen_capture_method_ = method;
  return 0;
}

int ConfigCenter::SetVideoEncodeFormat(VIDEO_ENCODE_FORMAT format) {
  if (format != VIDEO_ENCODE_FORMAT::H264 && format != VIDEO_ENCODE_FORMAT::AV1) return -1;
  if (StoreValues({{"video_encode_format", std::to_string(static_cast<int>(format))}}) != 0) return -1;
  video_encode_format_ = format;
  return 0;
}

int ConfigCenter::SetHardwareVideoCodec(bool enabled) {
  if (enabled && !IsHardwareVideoCodecAvailable()) return -1;
  if (StoreValues({{"hardware_video_codec", enabled ? "true" : "false"}}) != 0) return -1;
  hardware_video_codec_ = enabled;
  return 0;
}

int ConfigCenter::SetTurnMode(TURN_MODE mode) {
  if (!IsValidTurnModeValue(static_cast<long>(mode))) return -1;
  if (StoreValues({{"turn_mode", std::to_string(static_cast<int>(mode))},
                   {"enable_turn", mode == TURN_MODE::DISABLED ? "false" : "true"}}) != 0) return -1;
  turn_mode_ = mode;
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

int ConfigCenter::SetServerHost(const std::string& host) {
  if (host.empty() || host.size() > 253 || host.find_first_of("/\\@?#") != std::string::npos ||
      std::any_of(host.begin(), host.end(), [](unsigned char c) { return c <= 32 || c == 127; })) return -1;
  if (StoreValues({{"signal_server_host", host}}) != 0) return -1;
  signal_server_host_ = host;
  return 0;
}

int ConfigCenter::SetServerPort(int port) {
  if (port < 1 || port > 65535 || StoreValues({{"signal_server_port", std::to_string(port)}}) != 0) return -1;
  signal_server_port_ = port;
  return 0;
}

int ConfigCenter::SetSelfHosted(bool enabled) {
  if (enabled && (signal_server_host_.empty() || signal_server_port_ < 1 || signal_server_port_ > 65535)) return -1;
  if (StoreValues({{"enable_self_hosted", enabled ? "true" : "false"}}) != 0) return -1;
  enable_self_hosted_ = enabled;
  return 0;
}

int ConfigCenter::SetAutostart(bool enabled) {
  const bool previous = IsAutostartEnabled("CrossDesk");
  if (enabled != previous && !(enabled ? EnableAutostart("CrossDesk") : DisableAutostart("CrossDesk"))) {
    LOG_ERROR("SetAutostart failed");
    return -1;
  }
  if (StoreValues({{"enable_autostart", enabled ? "true" : "false"}}) != 0) {
    if (enabled != previous) {
      if (previous) EnableAutostart("CrossDesk");
      else DisableAutostart("CrossDesk");
    }
    return -1;
  }
  enable_autostart_ = enabled;
  return 0;
}

int ConfigCenter::SetDaemon(bool enabled) {
  if (StoreValues({{"enable_daemon", enabled ? "true" : "false"}}) != 0) return -1;
  enable_daemon_ = enabled;
  return 0;
}

int ConfigCenter::SetPrivacyScreen(bool enable_privacy_screen) {
  const bool previous = enable_privacy_screen_.load();
  if (previous == enable_privacy_screen) return 0;
  ini_->SetBoolValue(section_, "enable_privacy_screen", enable_privacy_screen);
  if (CommitIni() < 0) {
    ini_->SetBoolValue(section_, "enable_privacy_screen", previous);
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
  ini_->SetBoolValue(section_, "portable_service_prompt_suppressed",
                    portable_service_prompt_suppressed_);
  SI_Error rc = CommitIni();
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
  if (path.size() >= 512 || std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32 || c == 127; })) return -1;
  std::lock_guard<std::mutex> lock(file_path_mutex_);
  if (StoreValues({{"file_transfer_save_path", path}}) != 0) return -1;
  file_transfer_save_path_ = path;
  return 0;
}

std::string ConfigCenter::GetFileTransferSavePath() const {
  std::lock_guard<std::mutex> lock(file_path_mutex_);
  return file_transfer_save_path_;
}
}  // namespace crossdesk
