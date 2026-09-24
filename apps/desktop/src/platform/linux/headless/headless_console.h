/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-25
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _HEADLESS_CONSOLE_H_
#define _HEADLESS_CONSOLE_H_

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

#include "headless_settings.h"

namespace crossdesk {

// Enabled before display discovery and fork, with no input thread. The GUI
// thread polls commands; transport callbacks only publish credential snapshots.
class HeadlessConsole {
 public:
  static HeadlessConsole& Instance();
  ~HeadlessConsole();
  bool Enable(const std::filesystem::path& log_directory,
              const std::filesystem::path& config_file = {});
  bool active() const { return output_ >= 0; }
  const std::string& diagnostic_path() const { return diagnostic_path_; }
  void SetIdentity(const std::string& identity);
  void Notify(const std::string& message);
  const std::string& Text(const char* key) const;
  void NotifyKey(const char* key);
  void SetLanguage(int language);
  void BindSettings(ConfigCenter* config, std::function<bool()> connection_busy,
                    std::function<void(SettingEffect)> applied);
  void Poll(bool connected, const std::function<void(const std::string&)>& password,
            const std::function<void()>& random_password,
            const std::function<void()>& quit);

 private:
  HeadlessConsole() = default;
  void Write(const std::string& text);
  void ShowStatus(bool connected);
  void Help();
  void ShowSettings();
  void ChangeSetting(const std::string& key, const std::string& value);
  bool SettingsCommand(const std::string& line);
  void Command(const std::string& line, bool connected,
               const std::function<void(const std::string&)>& password,
               const std::function<void()>& random_password,
               const std::function<void()>& quit);

  int output_ = -1;
  int error_ = -1;
  bool interactive_ = false;
  bool input_closed_ = false;
  bool waiting_password_ = false;
  bool discarding_line_ = false;
  std::string input_;
  std::string diagnostic_path_;
  std::mutex identity_mutex_;
  std::string id_;
  std::string password_;
  std::string displayed_id_;
  std::string displayed_password_;
  bool displayed_connected_ = false;
  bool displayed_ = false;
  int language_ = 0;
  ConfigCenter* config_ = nullptr;
  std::function<bool()> connection_busy_;
  std::function<void(SettingEffect)> settings_applied_;
  bool settings_menu_ = false;
  std::string selected_setting_;
};

}  // namespace crossdesk

#endif