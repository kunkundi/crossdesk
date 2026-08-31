#include "platform/autostart_backend.h"

#include <limits.h>
#include <mach-o/dyld.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace crossdesk::platform {
namespace {

std::filesystem::path AutostartPath(const std::string& app_name) {
  const char* home = std::getenv("HOME");
  if (!home || !home[0]) return {};
  return std::filesystem::path(home) / "Library" / "LaunchAgents" /
         (app_name + ".plist");
}

}  // namespace

std::string GetAutostartExecutablePath() {
  char path[PATH_MAX] = {};
  uint32_t size = sizeof(path);
  if (_NSGetExecutablePath(path, &size) != 0) return {};
  char resolved[PATH_MAX] = {};
  return realpath(path, resolved) ? std::string(resolved) : std::string(path);
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
  file << R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
"http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>)"
       << app_name << R"(</string>
    <key>ProgramArguments</key>
    <array>
        <string>)"
       << executable_path << R"(</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
</dict>
</plist>)";
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
