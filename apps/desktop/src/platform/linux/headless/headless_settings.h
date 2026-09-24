/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-25
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _HEADLESS_SETTINGS_H_
#define _HEADLESS_SETTINGS_H_

#include <string>
#include <vector>

namespace crossdesk {
class ConfigCenter;

enum class SettingEffect { none, immediate, reconnect, next_connection, next_file,
                           next_login, normal_restart };
struct ConsoleSetting {
  std::string key;
  const char* label;
  std::string value;
  const char* choices;
};

struct SettingResult {
  bool saved = false;
  SettingEffect effect = SettingEffect::none;
  const char* message = "console_settings_invalid";
};

std::vector<ConsoleSetting> GetConsoleSettings(const ConfigCenter& config);
SettingResult ApplyConsoleSetting(ConfigCenter& config, const std::string& key,
                                  const std::string& value, bool connection_busy);
}  // namespace crossdesk

#endif