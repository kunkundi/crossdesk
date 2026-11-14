#include "config_center.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <optional>
#include <vector>

#include "SimpleIni.h"
#include "autostart.h"
#include "rd_log.h"
#include <minirtc.h>

#ifdef _WIN32
#include "platform/windows/runtime_daemon.h"
#include "platform/windows/service/autostart_control.h"
#endif

#ifdef __APPLE__
#include "platform/macos/runtime_daemon.h"
#endif

#if defined(__linux__) && !defined(__OHOS__)
#include "platform/linux/autostart.h"
#include "platform/linux/runtime_daemon.h"
#endif

namespace crossdesk {

struct ConfigCenter::IniStorage : CSimpleIniA {};

namespace {

bool IsValidTurnModeValue(long value) {
  return value >= static_cast<long>(ConfigCenter::TURN_MODE::DISABLED) &&
         value <= static_cast<long>(ConfigCenter::TURN_MODE::FORCE_TCP);
}

int ReadStartingBitrate(const CSimpleIniA& ini, const char* section,
                        const char* key, int fallback) {
  const char* value = ini.GetValue(section, key, nullptr);
  if (!value) return fallback;
  const char* end = value + std::strlen(value);
  int bps = 0;
  const auto parsed = std::from_chars(value, end, bps);
  if (parsed.ec != std::errc{} || parsed.ptr != end ||
      bps < 300'000 || bps > 20'000'000) {
    LOG_WARN("Invalid {} configuration, using {} bps", key, fallback);
    return fallback;
  }
  return bps;
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
    std::error_code error;
    if (std::filesystem::exists(config_path_, error) || error) {
      // An unreadable existing configuration must not silently disable access
      // restrictions or overwrite the user's settings with default values.
      trusted_devices_only_.store(true);
      LOG_ERROR("Cannot read existing configuration; restricting incoming connections");
      return -1;
    }
    Save();
    return -1;
  }

  bool persist_config_migration = false;
  // Hardware codecs are always preferred, with software fallback in MiniRTC.
  persist_config_migration |= ini_->Delete(section_, "hardware_video_codec");
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
  // The old global flag has no version information. Prompt once for the
  // current release, then remember its version when the user dismisses it.
  persist_config_migration |=
      ini_->Delete(section_, "sponsorship_prompt_dismissed");

  const long language_value =
      ini_->GetLongValue(section_, "language", static_cast<long>(language_));
  if (language_value < static_cast<long>(LANGUAGE::CHINESE) ||
      language_value > static_cast<long>(LANGUAGE::SYSTEM)) {
    language_ = LANGUAGE::ENGLISH;
  } else {
    language_ = static_cast<LANGUAGE>(language_value);
  }

  enable_ip_direct_ = ini_->GetBoolValue(section_, "enable_ip_direct", true);
  connection_mode_ =
      enable_ip_direct_ &&
              std::strcmp(ini_->GetValue(section_, "connection_mode", "id"), "ip") == 0
          ? CONNECTION_MODE::DIRECT_IP
          : CONNECTION_MODE::DEVICE_ID;

  // Capture always follows DXGI -> WGC -> GDI; discard old forced choices.
  persist_config_migration |= ini_->Delete(section_, "screen_capture_method");

  video_encode_format_ = static_cast<VIDEO_ENCODE_FORMAT>(
      ini_->GetLongValue(section_, "video_encode_format",
                        static_cast<long>(video_encode_format_)));

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
  direct_start_bitrate_bps_ = ReadStartingBitrate(
      *ini_, section_, "direct_start_bitrate_bps", kDefaultDirectStartBitrateBps);
  relay_start_bitrate_bps_ = ReadStartingBitrate(
      *ini_, section_, "relay_start_bitrate_bps", kDefaultRelayStartBitrateBps);
  disable_receiver_padding_probing_ = ini_->GetBoolValue(
      section_, "disable_receiver_padding_probing",
      kDefaultDisableReceiverPaddingProbing);
  if (!ini_->GetValue(section_, "disable_receiver_padding_probing", nullptr)) {
    ini_->SetBoolValue(section_, "disable_receiver_padding_probing",
                      disable_receiver_padding_probing_);
    persist_config_migration = true;
  }
  // Populate old configurations and repair invalid values so the file shows
  // the estimates that will actually be used for subsequent connections.
  for (const auto& entry :
       {std::make_pair("direct_start_bitrate_bps", direct_start_bitrate_bps_),
        std::make_pair("relay_start_bitrate_bps", relay_start_bitrate_bps_)}) {
    if (std::to_string(entry.second) != ini_->GetValue(section_, entry.first, "")) {
      ini_->SetLongValue(section_, entry.first, entry.second);
      persist_config_migration = true;
    }
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
  privacy_local_unlock_allowed_.store(
      ini_->GetBoolValue(section_, "privacy_local_unlock_allowed", true));
  lock_on_disconnect_.store(
      ini_->GetBoolValue(section_, "lock_on_disconnect", true));
  trusted_devices_only_.store(
      ini_->GetValue(section_, "trusted_devices_only", nullptr) != nullptr &&
      ini_->GetBoolValue(section_, "trusted_devices_only", true));
  save_remote_previews_.store(
      ini_->GetBoolValue(section_, "save_remote_previews", true));
  pause_video_when_minimized_.store(
      ini_->GetBoolValue(section_, "pause_video_when_minimized", true));
  portable_service_prompt_suppressed_ =
      ini_->GetBoolValue(section_, "portable_service_prompt_suppressed",
                        portable_service_prompt_suppressed_);
  sponsorship_prompt_dismissed_version_ =
      ini_->GetValue(section_, "sponsorship_prompt_dismissed_version", "");

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

int ConfigCenter::CommitIni(bool warn_only) {
  // Write beside the destination and rename only after a complete save. A
  // failed write must not truncate the last usable configuration.
  const auto log_failure = [warn_only](const std::string& message) {
    if (warn_only) LOG_WARN("{}", message);
    else LOG_ERROR("{}", message);
  };
  static std::atomic<unsigned long> sequence{0};
  const std::string temporary = config_path_ + ".tmp-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
      "-" + std::to_string(sequence++);
  std::error_code error;
  if (ini_->SaveFile(temporary.c_str()) < 0) {
    log_failure("Failed to write configuration: " + config_path_);
    std::filesystem::remove(temporary, error);
    return -1;
  }
  const auto previous = std::filesystem::status(config_path_, error);
  if (!error && std::filesystem::is_regular_file(previous)) {
    std::filesystem::permissions(temporary, previous.permissions(), error);
    if (error) {
      log_failure("Failed to preserve configuration permissions: " + error.message());
      std::filesystem::remove(temporary, error);
      return -1;
    }
  }
  std::filesystem::rename(temporary, config_path_, error);
  if (!error) return 0;
  log_failure("Failed to replace configuration " + config_path_ + ": " + error.message());
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
  ini_->SetValue(section_, "connection_mode",
                connection_mode_ == CONNECTION_MODE::DIRECT_IP ? "ip" : "id");
  ini_->SetBoolValue(section_, "enable_ip_direct", enable_ip_direct_);
  ini_->SetLongValue(section_, "video_encode_format",
                    static_cast<long>(video_encode_format_));
  ini_->SetLongValue(section_, "turn_mode", static_cast<long>(turn_mode_));
  ini_->SetLongValue(section_, "direct_start_bitrate_bps", direct_start_bitrate_bps_);
  ini_->SetLongValue(section_, "relay_start_bitrate_bps", relay_start_bitrate_bps_);
  ini_->SetBoolValue(section_, "disable_receiver_padding_probing",
                    disable_receiver_padding_probing_);
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
  ini_->SetBoolValue(section_, "privacy_local_unlock_allowed",
                    privacy_local_unlock_allowed_.load());
  ini_->SetBoolValue(section_, "lock_on_disconnect", lock_on_disconnect_.load());
  ini_->SetBoolValue(section_, "trusted_devices_only", trusted_devices_only_.load());
  ini_->SetBoolValue(section_, "save_remote_previews",
                    save_remote_previews_.load());
  ini_->SetBoolValue(section_, "pause_video_when_minimized",
                    pause_video_when_minimized_.load());
  ini_->SetBoolValue(section_, "portable_service_prompt_suppressed",
                    portable_service_prompt_suppressed_);
  ini_->SetValue(section_, "sponsorship_prompt_dismissed_version",
                sponsorship_prompt_dismissed_version_.c_str());

  ini_->SetValue(section_, "file_transfer_save_path",
                file_transfer_save_path_.c_str());

  SI_Error rc = CommitIni();
  if (rc < 0) {
    return -1;
  }

  return 0;
}

std::string ConfigCenter::ExportSharedHostConfig() const {
  CSimpleIniA host;
  host.SetUnicode(true);
  host.SetLongValue(section_, "shared_identity_config_version", 1);
  host.SetBoolValue(section_, "enable_self_hosted", enable_self_hosted_);
  host.SetValue(section_, "signal_server_host", signal_server_host_.c_str());
  host.SetLongValue(section_, "signal_server_port", signal_server_port_);
  host.SetLongValue(section_, "turn_mode", static_cast<long>(turn_mode_));
  host.SetLongValue(section_, "direct_start_bitrate_bps", direct_start_bitrate_bps_);
  host.SetLongValue(section_, "relay_start_bitrate_bps", relay_start_bitrate_bps_);
  host.SetBoolValue(section_, "disable_receiver_padding_probing",
                    disable_receiver_padding_probing_);
  host.SetLongValue(section_, "video_encode_format", static_cast<long>(video_encode_format_));
  host.SetBoolValue(section_, "trusted_devices_only", trusted_devices_only_.load());
  host.SetBoolValue(section_, "lock_on_disconnect", lock_on_disconnect_.load());
  host.SetBoolValue(section_, "enable_privacy_screen", false);
  host.SetBoolValue(section_, "save_remote_previews", false);
  host.SetBoolValue(section_, "enable_ip_direct", false);
  std::string contents;
  host.Save(contents);
  return contents;
}

int ConfigCenter::ApplySharedHostConfig(const std::string& contents) {
  CSimpleIniA host;
  host.SetUnicode(true);
  if (host.LoadData(contents.c_str(), contents.size()) < 0 ||
      host.GetLongValue(section_, "shared_identity_config_version", 0) != 1) return -1;
  const bool self_hosted = host.GetBoolValue(section_, "enable_self_hosted", false);
  const std::string server = host.GetValue(section_, "signal_server_host", "");
  const long port = host.GetLongValue(section_, "signal_server_port", 0);
  const long turn = host.GetLongValue(section_, "turn_mode", static_cast<long>(turn_mode_));
  const long codec = host.GetLongValue(section_, "video_encode_format", static_cast<long>(video_encode_format_));
  const int direct_bps = ReadStartingBitrate(
      host, section_, "direct_start_bitrate_bps", kDefaultDirectStartBitrateBps);
  const int relay_bps = ReadStartingBitrate(
      host, section_, "relay_start_bitrate_bps", kDefaultRelayStartBitrateBps);
  const bool disable_receiver_padding_probing = host.GetBoolValue(
      section_, "disable_receiver_padding_probing",
      kDefaultDisableReceiverPaddingProbing);
  const bool trusted = host.GetBoolValue(section_, "trusted_devices_only", false);
  const bool lock = host.GetBoolValue(section_, "lock_on_disconnect", true);
  MiniRtcSignalServerAddress address{};
  if (!IsValidTurnModeValue(turn) || codec < 0 || codec > 1 ||
      (self_hosted && (port < 1 || port > 65535 ||
       MiniRtcParseSignalServerAddress(server.data(), server.size(), &address) != 0))) return -1;
  if (StoreValues({{"enable_self_hosted", self_hosted ? "true" : "false"},
                   {"signal_server_host", server}, {"signal_server_port", std::to_string(port)},
                   {"turn_mode", std::to_string(turn)}, {"video_encode_format", std::to_string(codec)},
                   {"direct_start_bitrate_bps", std::to_string(direct_bps)},
                   {"relay_start_bitrate_bps", std::to_string(relay_bps)},
                   {"disable_receiver_padding_probing",
                    disable_receiver_padding_probing ? "true" : "false"},
                   {"trusted_devices_only", trusted ? "true" : "false"},
                   {"lock_on_disconnect", lock ? "true" : "false"}}) != 0) return -1;
  enable_self_hosted_ = self_hosted;
  signal_server_host_ = server;
  signal_server_port_ = static_cast<int>(port);
  turn_mode_ = static_cast<TURN_MODE>(turn);
  direct_start_bitrate_bps_ = direct_bps;
  relay_start_bitrate_bps_ = relay_bps;
  disable_receiver_padding_probing_ = disable_receiver_padding_probing;
  video_encode_format_ = static_cast<VIDEO_ENCODE_FORMAT>(codec);
  trusted_devices_only_.store(trusted);
  lock_on_disconnect_.store(lock);
  return 0;
}

// setters

int ConfigCenter::SetLanguage(LANGUAGE language) {
  const int value = static_cast<int>(language);
  if (value < 0 || value > static_cast<int>(LANGUAGE::SYSTEM) || StoreValues({{"language", std::to_string(value)}}) != 0) return -1;
  language_ = language;
  return 0;
}

int ConfigCenter::SetConnectionMode(CONNECTION_MODE mode) {
  if (mode != CONNECTION_MODE::DEVICE_ID && mode != CONNECTION_MODE::DIRECT_IP) {
    return -1;
  }
  if (mode == CONNECTION_MODE::DIRECT_IP && !enable_ip_direct_) return -1;
  if (mode == connection_mode_) return 0;
  if (StoreValues({{"connection_mode",
                    mode == CONNECTION_MODE::DIRECT_IP ? "ip" : "id"}}) != 0) {
    return -1;
  }
  connection_mode_ = mode;
  return 0;
}

int ConfigCenter::SetIpDirectEnabled(bool enabled) {
  if (enabled == enable_ip_direct_) return 0;
  const auto mode = enabled ? connection_mode_ : CONNECTION_MODE::DEVICE_ID;
  // Persist availability and the fallback mode together, so a failed save
  // leaves both the current runtime and the next launch unchanged.
  if (StoreValues({{"enable_ip_direct", enabled ? "true" : "false"},
                   {"connection_mode", mode == CONNECTION_MODE::DIRECT_IP
                                           ? "ip" : "id"}}) != 0)
    return -1;
  enable_ip_direct_ = enabled;
  connection_mode_ = mode;
  return 0;
}

int ConfigCenter::SetVideoEncodeFormat(VIDEO_ENCODE_FORMAT format) {
  if (format != VIDEO_ENCODE_FORMAT::H264 && format != VIDEO_ENCODE_FORMAT::AV1) return -1;
  if (StoreValues({{"video_encode_format", std::to_string(static_cast<int>(format))}}) != 0) return -1;
  video_encode_format_ = format;
  return 0;
}

int ConfigCenter::SetTurnMode(TURN_MODE mode) {
  if (!IsValidTurnModeValue(static_cast<long>(mode))) return -1;
  if (StoreValues({{"turn_mode", std::to_string(static_cast<int>(mode))},
                   {"enable_turn", mode == TURN_MODE::DISABLED ? "false" : "true"}}) != 0) return -1;
  turn_mode_ = mode;
  return 0;
}

int ConfigCenter::SetForceRelay(bool force_relay) {
  if (!force_relay) {
    return SetTurnMode(TURN_MODE::AUTO_UDP_TCP);
  }
  // Use TURN/UDP by default, preserving an explicitly configured TCP relay.
  return SetTurnMode(turn_mode_ == TURN_MODE::FORCE_TCP ? TURN_MODE::FORCE_TCP
                                                     : TURN_MODE::FORCE_UDP);
}

int ConfigCenter::SetServerHost(const std::string& host) {
  MiniRtcSignalServerAddress address{};
  if (MiniRtcParseSignalServerAddress(host.data(), host.size(), &address) != 0) return -1;
  if (address.port) return SetServerEndpoint(host, address.port);
  const auto normalized = std::string(address.host) + address.target;
  if (StoreValues({{"signal_server_host", normalized}}) != 0) return -1;
  signal_server_host_ = normalized;
  return 0;
}

int ConfigCenter::SetServerEndpoint(const std::string& host, int port) {
  MiniRtcSignalServerAddress address{};
  if (MiniRtcParseSignalServerAddress(host.data(), host.size(), &address) != 0) return -1;
  if (address.port) port = address.port;
  if (port < 1 || port > 65535) return -1;
  const auto normalized = std::string(address.host) + address.target;
  if (StoreValues({{"signal_server_host", normalized},
                   {"signal_server_port", std::to_string(port)}}) != 0) return -1;
  signal_server_host_ = normalized;
  signal_server_port_ = port;
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
#ifdef _WIN32
  // System registration determines success; INI only mirrors the last
  // successfully applied preference. Unsupported builds never touch services.
  if (!SupportsServiceAutostart()) {
    if (enabled) return -1;
  } else if (!ServiceAutostartMatches(enabled)) {
    std::string error;
    if (!SetServiceAutostart(enabled, error)) {
      LOG_ERROR("Cannot configure autostart: {}", error);
      return -1;
    }
  }
  enable_autostart_ = enabled;
  ini_->SetBoolValue(section_, "enable_autostart", enabled);
  // Keep both in-memory copies even if the file is temporarily unwritable,
  // so a later settings save can persist them. Never roll back the service.
  CommitIni(true);
  return 0;
#else
  const bool previous = IsAutostartEnabled("CrossDesk");
  const auto configure = [](bool value) {
    return value ? EnableAutostart("CrossDesk") : DisableAutostart("CrossDesk");
  };
#if defined(__linux__) && !defined(__OHOS__)
  // Repair old deb entries even when the saved boolean is unchanged, keeping
  // desktop-managed disabled flags and custom commands intact.
  if (enabled && previous && !platform::MigrateLegacyAutostart("CrossDesk")) {
    LOG_ERROR("Failed to migrate Linux autostart");
    return -1;
  }
#endif
  if (enabled != previous && !configure(enabled)) {
    LOG_ERROR("Cannot configure autostart");
    return -1;
  }
  if (StoreValues({{"enable_autostart", enabled ? "true" : "false"}}) != 0) {
    if (enabled != previous && !configure(previous))
      LOG_ERROR("Cannot restore autostart");
    return -1;
  }
  enable_autostart_ = enabled;
  return 0;
#endif
}

int ConfigCenter::SetDaemon(bool enabled) {
#if defined(_WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(__OHOS__))
  const bool previous = enable_daemon_;
#endif
  if (StoreValues({{"enable_daemon", enabled ? "true" : "false"}}) != 0) return -1;
  enable_daemon_ = enabled;
#if defined(_WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(__OHOS__))
  std::string error;
#ifdef _WIN32
  auto& daemon = platform::WindowsRuntimeDaemon::Instance();
#elif defined(__APPLE__)
  auto& daemon = platform::MacRuntimeDaemon::Instance();
#else
  auto& daemon = platform::LinuxRuntimeDaemon::Instance();
#endif
  if (!daemon.SetEnabled(enabled, error)) {
    LOG_ERROR("Cannot apply daemon setting: {}", error);
    if (StoreValues({{"enable_daemon", previous ? "true" : "false"}}) == 0) {
      enable_daemon_ = previous;
    } else {
      LOG_ERROR("Cannot restore daemon preference after startup failure");
    }
    return -1;
  }
#endif
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

int ConfigCenter::SetPrivacyLocalUnlock(bool allowed) {
  if (privacy_local_unlock_allowed_.load() == allowed) return 0;
  if (StoreValues({{"privacy_local_unlock_allowed", allowed ? "true" : "false"}}) != 0)
    return -1;
  privacy_local_unlock_allowed_.store(allowed);
  return 0;
}

bool ConfigCenter::IsPrivacyLocalUnlockAllowed() const {
  return privacy_local_unlock_allowed_.load();
}

int ConfigCenter::SetTrustedDevicesOnly(bool enabled) {
  if (StoreValues({{"trusted_devices_only", enabled ? "true" : "false"}}) != 0) return -1;
  trusted_devices_only_.store(enabled);
  return 0;
}
bool ConfigCenter::IsTrustedDevicesOnly() const { return trusted_devices_only_.load(); }

int ConfigCenter::SetLockOnDisconnect(bool enabled) {
  if (StoreValues({{"lock_on_disconnect", enabled ? "true" : "false"}}) != 0)
    return -1;
  lock_on_disconnect_.store(enabled);
  return 0;
}

bool ConfigCenter::IsLockOnDisconnect() const {
  return lock_on_disconnect_.load();
}

int ConfigCenter::SetSaveRemotePreviews(bool enabled) {
  if (StoreValues({{"save_remote_previews", enabled ? "true" : "false"}}) != 0) {
    return -1;
  }
  save_remote_previews_.store(enabled);
  return 0;
}

bool ConfigCenter::IsSaveRemotePreviews() const {
  return save_remote_previews_.load();
}

int ConfigCenter::SetPauseVideoWhenMinimized(bool enabled) {
  if (StoreValues({{"pause_video_when_minimized", enabled ? "true" : "false"}}) != 0) {
    return -1;
  }
  pause_video_when_minimized_.store(enabled);
  return 0;
}

bool ConfigCenter::IsPauseVideoWhenMinimized() const {
  return pause_video_when_minimized_.load();
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

int ConfigCenter::SetSponsorshipPromptDismissedVersion(
    const std::string& version) {
  if (version.empty() ||
      StoreValues({{"sponsorship_prompt_dismissed_version", version}}) != 0) {
    return -1;
  }
  sponsorship_prompt_dismissed_version_ = version;
  return 0;
}

// getters

ConfigCenter::LANGUAGE ConfigCenter::GetLanguage() const { return language_; }

ConfigCenter::CONNECTION_MODE ConfigCenter::GetConnectionMode() const {
  return connection_mode_;
}

bool ConfigCenter::IsIpDirectEnabled() const { return enable_ip_direct_; }

ConfigCenter::VIDEO_ENCODE_FORMAT ConfigCenter::GetVideoEncodeFormat() const {
  return video_encode_format_;
}

ConfigCenter::TURN_MODE ConfigCenter::GetTurnMode() const {
  return turn_mode_;
}

int ConfigCenter::GetDirectStartBitrateBps() const {
  return direct_start_bitrate_bps_;
}

int ConfigCenter::GetRelayStartBitrateBps() const {
  return relay_start_bitrate_bps_;
}

bool ConfigCenter::IsReceiverPaddingProbingDisabled() const {
  return disable_receiver_padding_probing_;
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

bool ConfigCenter::IsEnableAutostart() const {
#ifdef _WIN32
  return IsServiceAutostartEnabled();
#elif defined(__linux__) && !defined(__OHOS__)
  // Uninstalling or desktop startup settings can remove the registration while
  // leaving config.ini intact. Report the actual entry, not the saved intent.
  return IsAutostartEnabled("CrossDesk");
#else
  return enable_autostart_;
#endif
}

bool ConfigCenter::IsEnableDaemon() const { return enable_daemon_; }

bool ConfigCenter::IsEnablePrivacyScreen() const {
  return enable_privacy_screen_.load();
}

bool ConfigCenter::IsPortableServicePromptSuppressed() const {
  return portable_service_prompt_suppressed_;
}

std::string ConfigCenter::GetSponsorshipPromptDismissedVersion() const {
  return sponsorship_prompt_dismissed_version_;
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
