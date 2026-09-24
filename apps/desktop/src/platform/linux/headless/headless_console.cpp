#include "headless_console.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "rd_log.h"
#include "config_center.h"
#include "localization.h"
#include "spdlog/sinks/rotating_file_sink.h"

namespace crossdesk {
namespace {
bool ValidPassword(const std::string& password) {
  if (password.size() != 6) return false;
  for (unsigned char c : password) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9'))) return false;
  }
  return true;
}
}  // namespace

HeadlessConsole& HeadlessConsole::Instance() {
  static HeadlessConsole console;
  return console;
}

HeadlessConsole::~HeadlessConsole() {
  if (!active()) return;
  std::cout.flush();
  std::cerr.flush();
  fflush(nullptr);
  dup2(output_, STDOUT_FILENO);
  dup2(error_, STDERR_FILENO);
  close(output_);
  close(error_);
}

bool HeadlessConsole::Enable(const std::filesystem::path& log_directory,
                             const std::filesystem::path& config_file) {
  if (active()) return true;
  std::error_code error;
  std::filesystem::create_directories(log_directory, error);
  if (error) return false;
  // Existing loggers keep their own rotating files and filenames. Suppress
  // only stdout/stderr copies; do not create a second diagnostic log stream.
  const int quiet = open("/dev/null", O_WRONLY | O_CLOEXEC);
  if (quiet < 0) return false;
  const int out = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
  const int err = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 3);
  if (out < 0 || err < 0) {
    if (out >= 0) close(out);
    if (err >= 0) close(err);
    close(quiet);
    return false;
  }
  std::cout.flush();
  std::cerr.flush();
  fflush(nullptr);
  if (dup2(quiet, STDOUT_FILENO) < 0 || dup2(quiet, STDERR_FILENO) < 0) {
    dup2(out, STDOUT_FILENO);
    dup2(err, STDERR_FILENO);
    close(out);
    close(err);
    close(quiet);
    return false;
  }
  close(quiet);
  output_ = out;
  error_ = err;
  interactive_ = isatty(STDIN_FILENO) && isatty(output_);
  // Initialize after redirecting output so even the logger's first message
  // stays in its original crossdesk-YYYYMMDD-HHMMSS.log file.
  try {
    InitLogger(log_directory.string());
    for (const auto& sink : get_logger()->sinks()) {
      if (const auto file =
              std::dynamic_pointer_cast<spdlog::sinks::rotating_file_sink_mt>(sink)) {
        diagnostic_path_ = file->filename();
        break;
      }
    }
  } catch (const std::exception&) {
    dup2(output_, STDOUT_FILENO);
    dup2(error_, STDERR_FILENO);
    close(output_);
    close(error_);
    output_ = error_ = -1;
    return false;
  }
  if (!config_file.empty()) {
    ConfigCenter config(config_file.string());
    SetLanguage(static_cast<int>(config.GetLanguage()));
  }
  Write(Text("console_title") + "\n" + Text("console_log_file") + ": " + diagnostic_path_ + "\n");
  Help();
  return true;
}

void HeadlessConsole::Write(const std::string& text) {
  if (!active()) return;
  size_t offset = 0;
  while (offset < text.size()) {
    const ssize_t count = write(output_, text.data() + offset, text.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
    offset += static_cast<size_t>(count);
  }
}

void HeadlessConsole::Notify(const std::string& message) {
  if (active()) Write(message + "\n");
}

void HeadlessConsole::SetIdentity(const std::string& identity) {
  if (!active()) return;
  const auto separator = identity.find('@');
  const std::string id = identity.substr(0, separator);
  const std::string password = separator == std::string::npos
                                   ? "" : identity.substr(separator + 1);
  // Credentials are rendered to a terminal, never interpreted as terminal
  // escape sequences and never forwarded to the diagnostic log.
  if (id.find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_") !=
          std::string::npos || (!password.empty() && !ValidPassword(password))) return;
  std::lock_guard<std::mutex> lock(identity_mutex_);
  id_ = id;
  password_ = password;
}

const std::string& HeadlessConsole::Text(const char* key) const {
  return localization::detail::GetTranslatedText(key, language_);
}

void HeadlessConsole::NotifyKey(const char* key) { Notify(Text(key)); }

void HeadlessConsole::SetLanguage(int language) {
  language = localization::detail::ClampLanguageIndex(language);
  if (language_ != language) {
    language_ = language;
    displayed_ = false;
  }
}

void HeadlessConsole::BindSettings(ConfigCenter* config,
                                   std::function<bool()> connection_busy,
                                   std::function<void(SettingEffect)> applied) {
  config_ = config;
  connection_busy_ = std::move(connection_busy);
  settings_applied_ = std::move(applied);
  if (config_) SetLanguage(static_cast<int>(config_->GetLanguage()));
}

void HeadlessConsole::Help() { NotifyKey("console_help"); }

void HeadlessConsole::ShowSettings() {
  if (!config_) { NotifyKey("console_settings_unavailable"); return; }
  Write("\n" + Text("console_settings_title") + "\n");
  const auto settings = GetConsoleSettings(*config_);
  for (size_t i = 0; i < settings.size(); ++i) {
    const auto& item = settings[i];
    std::string value = item.value;
    for (auto& c : value) if (static_cast<unsigned char>(c) < 32 || c == 127) c = '?';
    Write(std::to_string(i + 1) + ". " + Text(item.label) + " [" + item.key +
          "] = " + value + " (" + item.choices + ")\n");
  }
  NotifyKey("console_settings_prompt");
}

void HeadlessConsole::ChangeSetting(const std::string& key, const std::string& value) {
  if (!config_) { NotifyKey("console_settings_unavailable"); return; }
  const auto result = ApplyConsoleSetting(*config_, key, value,
                                         connection_busy_ && connection_busy_());
  SetLanguage(static_cast<int>(config_->GetLanguage()));
  if (result.saved && result.effect != SettingEffect::none && settings_applied_) {
    settings_applied_(result.effect);
  }
  NotifyKey(result.message);
}

bool HeadlessConsole::SettingsCommand(const std::string& line) {
  if (line == "settings" || line == "settings show") {
    waiting_password_ = false;
    selected_setting_.clear();
    settings_menu_ = line == "settings";
    ShowSettings();
    return true;
  }
  if (line.compare(0, 13, "settings set ") == 0) {
    waiting_password_ = false;
    selected_setting_.clear();
    const auto separator = line.find(' ', 13);
    if (separator == std::string::npos) NotifyKey("console_settings_invalid");
    else ChangeSetting(line.substr(13, separator - 13), line.substr(separator + 1));
    return true;
  }
  if (!settings_menu_) return false;
  if (line == "back" || (line == "cancel" && selected_setting_.empty())) {
    settings_menu_ = false;
    selected_setting_.clear();
    Help();
    return true;
  }
  if (line == "cancel") {
    selected_setting_.clear();
    NotifyKey("console_cancelled");
    ShowSettings();
    return true;
  }
  // Status and quit remain available while navigating the menu.
  if (line == "status" || line == "help" || line == "?" || line == "quit" || line == "exit") return false;
  if (!selected_setting_.empty()) {
    ChangeSetting(selected_setting_, line);
    selected_setting_.clear();
    ShowSettings();
    return true;
  }
  if (!config_) { NotifyKey("console_settings_unavailable"); return true; }
  const auto settings = GetConsoleSettings(*config_);
  size_t index = 0;
  const auto parsed = std::from_chars(line.data(), line.data() + line.size(), index);
  if (parsed.ec == std::errc{} && parsed.ptr == line.data() + line.size() &&
      index > 0 && index <= settings.size()) {
    selected_setting_ = settings[index - 1].key;
  } else {
    for (const auto& setting : settings) if (setting.key == line) selected_setting_ = setting.key;
  }
  if (selected_setting_.empty()) {
    NotifyKey("console_settings_invalid");
  } else {
    const auto item = std::find_if(settings.begin(), settings.end(),
        [&](const auto& setting) { return setting.key == selected_setting_; });
    Write(Text(item->label) + " [" + item->choices + "]\n" + Text("console_settings_value") + ":\n");
  }
  return true;
}

void HeadlessConsole::ShowStatus(bool connected) {
  {
    std::lock_guard<std::mutex> lock(identity_mutex_);
    displayed_id_ = id_;
    displayed_password_ = password_;
  }
  displayed_connected_ = connected;
  displayed_ = true;
  Write("\n" + Text("console_status") + ": " + Text(connected ? "console_online" : "console_offline") +
        "\n" + Text("console_id") + ": " + (displayed_id_.empty() ? Text("console_waiting") : displayed_id_) +
        "\n" + Text("console_password") + ": " +
        (!interactive_ ? Text("console_password_hidden")
         : displayed_password_.empty() ? Text("console_waiting") : displayed_password_) + "\n");
}

void HeadlessConsole::Command(
    const std::string& line, bool connected,
    const std::function<void(const std::string&)>& password,
    const std::function<void()>& random_password,
    const std::function<void()>& quit) {
  if (line == "quit" || line == "exit") {
    input_closed_ = true;
    NotifyKey("console_quitting");
    quit();
    return;
  }
  if (SettingsCommand(line)) return;
  if (waiting_password_) {
    waiting_password_ = false;
    if (line.empty() || line == "cancel") {
      NotifyKey("console_cancelled");
    } else if (!ValidPassword(line)) {
      NotifyKey("console_password_invalid");
    } else {
      password(line);
    }
  } else if (line == "status") {
    ShowStatus(connected);
  } else if (line == "help" || line == "?") {
    Help();
  } else if (line == "password" || line == "passwd") {
    if (!connected) {
      NotifyKey("console_password_offline");
    } else {
      waiting_password_ = true;
      NotifyKey("console_password_prompt");
    }
  } else if (line.compare(0, 9, "password ") == 0 ||
             line.compare(0, 7, "passwd ") == 0) {
    const std::string value = line.substr(line.find(' ') + 1);
    if (!ValidPassword(value)) {
      NotifyKey("console_password_invalid");
    } else if (!connected) {
      NotifyKey("console_password_offline");
    } else {
      password(value);
    }
  } else if (line == "random") {
    if (connected) random_password();
    else NotifyKey("console_password_offline");
  } else if (!line.empty()) {
    NotifyKey("console_unknown_command");
  }
}

void HeadlessConsole::Poll(
    bool connected, const std::function<void(const std::string&)>& password,
    const std::function<void()>& random_password,
    const std::function<void()>& quit) {
  if (!active()) return;
  if (config_) SetLanguage(static_cast<int>(config_->GetLanguage()));
  bool changed = !displayed_ || displayed_connected_ != connected;
  {
    std::lock_guard<std::mutex> lock(identity_mutex_);
    changed |= displayed_id_ != id_ || displayed_password_ != password_;
  }
  if (changed) ShowStatus(connected);
  if (input_closed_) return;
  // A job launched in the background must not steal input or receive SIGTTIN.
  if (isatty(STDIN_FILENO) && tcgetpgrp(STDIN_FILENO) != getpgrp()) return;
  struct pollfd input { STDIN_FILENO, POLLIN, 0 };
  if (poll(&input, 1, 0) <= 0) return;
  if (!(input.revents & (POLLIN | POLLHUP))) {
    if (input.revents & (POLLERR | POLLNVAL)) input_closed_ = true;
    return;
  }
  char buffer[256];
  const ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
  if (count <= 0) {
    if (count == 0 || (errno != EINTR && errno != EAGAIN)) input_closed_ = true;
    return;
  }
  for (ssize_t i = 0; i < count && !input_closed_; ++i) {
    const char c = buffer[i];
    if (c == '\n') {
      if (!discarding_line_) {
        if (!input_.empty() && input_.back() == '\r') input_.pop_back();
        Command(input_, connected, password, random_password, quit);
      }
      input_.clear();
      discarding_line_ = false;
    } else if (!discarding_line_) {
      if (input_.size() == 1024) {
        input_.clear();
        discarding_line_ = true;
        waiting_password_ = false;
        NotifyKey("console_line_too_long");
      } else {
        input_ += c;
      }
    }
  }
}
}  // namespace crossdesk
