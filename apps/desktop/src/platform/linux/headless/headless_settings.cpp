#include "headless_settings.h"

#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>

#include "config_center.h"

namespace crossdesk {
namespace {
std::string Lower(std::string value) {
  for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return value;
}
std::optional<bool> Boolean(const std::string& value) {
  if (value == "on" || value == "true" || value == "1") return true;
  if (value == "off" || value == "false" || value == "0") return false;
  return std::nullopt;
}
const char* OnOff(bool value) { return value ? "on" : "off"; }
const char* languages[] = {"zh-CN", "en-US", "ru-RU"};
const char* turn_modes[] = {"off", "auto", "udp", "tcp"};
}  // namespace

std::vector<ConsoleSetting> GetConsoleSettings(const ConfigCenter& config) {
  const auto language = std::clamp(static_cast<int>(config.GetLanguage()), 0, 2);
  const auto turn = std::clamp(static_cast<int>(config.GetTurnMode()), 0, 3);
  return {
      {"language", "console_setting_language", languages[language], "zh-CN / en-US / ru-RU"},
      {"codec", "console_setting_codec", config.GetVideoEncodeFormat() == ConfigCenter::VIDEO_ENCODE_FORMAT::AV1 ? "av1" : "h264", "h264 / av1"},
      {"hardware", "console_setting_hardware", OnOff(config.IsHardwareVideoCodec()), "on / off"},
      {"turn", "console_setting_turn", turn_modes[turn], "off / auto / udp / tcp"},
      {"self_hosted", "console_setting_self_hosted", OnOff(config.IsSelfHosted()), "on / off"},
      {"server_host", "console_setting_host", config.GetSignalServerHost(), "example.com"},
      {"server_port", "console_setting_port", std::to_string(config.GetSignalServerPort()), "1..65535"},
      {"privacy", "console_setting_privacy", OnOff(config.IsEnablePrivacyScreen()), "on / off"},
      {"file_path", "console_setting_files", config.GetFileTransferSavePath().empty() ? "default" : config.GetFileTransferSavePath(), "/path/to/directory / default"},
      {"autostart", "console_setting_autostart", OnOff(config.IsEnableAutostart()), "on / off"},
      {"daemon", "console_setting_daemon", OnOff(config.IsEnableDaemon()), "on / off"},
  };
}

SettingResult ApplyConsoleSetting(ConfigCenter& config, const std::string& key,
                                  const std::string& input, bool connection_busy) {
  SettingResult result;
  if (key == "capture_method" || key == "fps" || key == "quality") {
    result.message = "console_settings_readonly";
    return result;
  }
  const bool network = key == "codec" || key == "hardware" || key == "turn" ||
                       key == "self_hosted" || key == "server_host" || key == "server_port";
  if (network && connection_busy) {
    result.message = "console_settings_busy";
    return result;
  }
  const std::string value = Lower(input);
  int status = -1;
  SettingEffect effect = SettingEffect::immediate;
  const char* message = "console_settings_saved";
  std::string canonical = input;
  std::function<int()> save;
  if (key == "language") {
    int language = value == "zh-cn" || value == "zh" || value == "0" ? 0
                  : value == "en-us" || value == "en" || value == "1" ? 1
                  : value == "ru-ru" || value == "ru" || value == "2" ? 2 : -1;
    if (language < 0) return result;
    canonical = languages[language];
    save = [&config, language] { return config.SetLanguage(static_cast<ConfigCenter::LANGUAGE>(language)); };
  } else if (key == "codec") {
    if (value != "h264" && value != "av1") return result;
    canonical = value;
    save = [&] { return config.SetVideoEncodeFormat(value == "h264" ? ConfigCenter::VIDEO_ENCODE_FORMAT::H264 : ConfigCenter::VIDEO_ENCODE_FORMAT::AV1); };
  } else if (key == "turn") {
    const auto found = std::find(std::begin(turn_modes), std::end(turn_modes), value);
    if (found == std::end(turn_modes)) return result;
    const int mode = static_cast<int>(found - std::begin(turn_modes));
    canonical = value;
    save = [&config, mode] { return config.SetTurnMode(static_cast<ConfigCenter::TURN_MODE>(mode)); };
  } else if (key == "server_host") {
    if (input.empty() || input.size() > 253 ||
        input.find_first_of("/\\@?#") != std::string::npos ||
        std::any_of(input.begin(), input.end(), [](unsigned char c) { return c <= 32 || c == 127; })) return result;
    save = [&] { return config.SetServerHost(input); };
  } else if (key == "server_port") {
    int port = 0;
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() || port < 1 || port > 65535) return result;
    canonical = std::to_string(port);
    save = [&config, port] { return config.SetServerPort(port); };
  } else if (key == "file_path") {
    std::string path = input == "default" ? "" : input;
    if (path.compare(0, 2, "~/") == 0 && std::getenv("HOME")) path.replace(0, 1, std::getenv("HOME"));
    std::error_code error;
    if (!path.empty() && (path.size() >= 512 || !std::filesystem::path(path).is_absolute() ||
        !std::filesystem::is_directory(path, error) || access(path.c_str(), W_OK | X_OK) != 0 ||
        std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32 || c == 127; }))) {
      result.message = "console_settings_path_invalid";
      return result;
    }
    canonical = path.empty() ? "default" : path;
    save = [&config, path] { return config.SetFileTransferSavePath(path); };
    effect = SettingEffect::next_file;
    message = "console_settings_next_file";
  } else {
    const auto boolean = Boolean(value);
    if (!boolean) return result;
    const bool enabled = *boolean;
    canonical = OnOff(enabled);
    if (key == "hardware") {
      if (enabled && !ConfigCenter::IsHardwareVideoCodecAvailable()) {
        result.message = "console_settings_hardware_unavailable";
        return result;
      }
      save = [&config, enabled] { return config.SetHardwareVideoCodec(enabled); };
    } else if (key == "self_hosted") {
      if (enabled && (config.GetSignalServerHost().empty() || config.GetSignalServerPort() <= 0 || config.GetSignalServerPort() > 65535)) {
        result.message = "console_settings_server_required";
        return result;
      }
      save = [&config, enabled] { return config.SetSelfHosted(enabled); };
    } else if (key == "privacy") {
      save = [&config, enabled] { return config.SetPrivacyScreen(enabled); };
      effect = SettingEffect::next_connection;
      message = "console_settings_next_connection";
    } else if (key == "autostart") {
      save = [&config, enabled] { return config.SetAutostart(enabled); };
      effect = SettingEffect::next_login;
      message = "console_settings_autostart";
    } else if (key == "daemon") {
      save = [&config, enabled] { return config.SetDaemon(enabled); };
      effect = SettingEffect::normal_restart;
      message = "console_settings_daemon";
    } else return result;
  }
  for (const auto& setting : GetConsoleSettings(config)) {
    if (key != "autostart" && key != "hardware" && setting.key == key && setting.value == canonical) {
      return {true, SettingEffect::none, "console_settings_unchanged"};
    }
  }
  status = save();
  if (status != 0) return {false, SettingEffect::none, "console_settings_save_failed"};
  if (network && ((key != "server_host" && key != "server_port") || config.IsSelfHosted())) {
    effect = SettingEffect::reconnect;
    message = "console_settings_reconnect";
  }
  return {true, effect, message};
}
}  // namespace crossdesk
