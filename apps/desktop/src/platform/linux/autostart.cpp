#include "platform/autostart_backend.h"

#include <limits.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace crossdesk::platform {
namespace {

std::filesystem::path AutostartPath(const std::string& app_name) {
  const char* home = std::getenv("HOME");
  if (!home || !home[0]) return {};
  return std::filesystem::path(home) / ".config" / "autostart" /
         (app_name + ".desktop");
}

}  // namespace

std::string GetAutostartExecutablePath() {
  char path[PATH_MAX] = {};
  const ssize_t count = readlink("/proc/self/exe", path, sizeof(path) - 1);
  return count > 0 ? std::string(path, static_cast<size_t>(count))
                   : std::string();
}

bool EnableAutostart(const std::string& app_name,
                     const std::string& executable_path) {
  const std::filesystem::path path = AutostartPath(app_name);
  if (path.empty()) return false;

  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) return false;

  std::ofstream file(path);
  if (!file.is_open()) return false;
  file << "[Desktop Entry]\n"
       << "Type=Application\n"
       << "Exec=" << executable_path << "\n"
       << "Hidden=false\n"
       << "NoDisplay=false\n"
       << "X-GNOME-Autostart-enabled=true\n"
       << "Terminal=false\n"
       << "StartupNotify=false\n"
       << "Name=" << app_name << "\n";
  file.close();
  return file.good();
}

bool DisableAutostart(const std::string& app_name) {
  const std::filesystem::path path = AutostartPath(app_name);
  if (path.empty()) return false;
  std::error_code error;
  return std::filesystem::remove(path, error) && !error;
}

bool IsAutostartEnabled(const std::string& app_name) {
  const std::filesystem::path path = AutostartPath(app_name);
  return !path.empty() && std::filesystem::is_regular_file(path);
}

}  // namespace crossdesk::platform
