#include "platform/autostart_backend.h"
#include "platform/linux/autostart.h"

#include <limits.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace crossdesk::platform {
namespace {

constexpr const char* kInstalledExecutable = "/usr/lib/crossdesk/crossdesk-bin";
constexpr const char* kInstalledLauncher = "/usr/bin/crossdesk";

std::filesystem::path AutostartPath(const std::string& app_name) {
  const char* config = std::getenv("XDG_CONFIG_HOME");
  if (config && *config) return std::filesystem::path(config) / "autostart" / (app_name + ".desktop");
  const char* home = std::getenv("HOME");
  if (!home || !home[0]) return {};
  return std::filesystem::path(home) / ".config" / "autostart" /
         (app_name + ".desktop");
}

bool WriteDesktopEntry(const std::filesystem::path& path,
                       const std::string& contents) {
  // In particular, a failed migration must leave the previous entry intact.
  std::string temporary = path.string() + ".XXXXXX";
  const int fd = mkstemp(temporary.data());
  if (fd < 0) return false;
  std::FILE* file = fdopen(fd, "w");
  if (!file) {
    close(fd);
    std::remove(temporary.c_str());
    return false;
  }
  const bool written =
      std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
  const bool closed = std::fclose(file) == 0;
  if (written && closed && std::rename(temporary.c_str(), path.c_str()) == 0)
    return true;
  std::remove(temporary.c_str());
  return false;
}

}  // namespace

std::string GetAutostartExecutablePath() {
  char path[PATH_MAX] = {};
  const ssize_t count = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (count <= 0 || count >= static_cast<ssize_t>(sizeof(path) - 1)) return {};
  const std::string executable(path, static_cast<size_t>(count));
  // The deb launcher sets LD_LIBRARY_PATH for the private Slint runtime.
  // /proc/self/exe resolves to its exec() target and loses that setup.
  if (executable == kInstalledExecutable && access(kInstalledLauncher, X_OK) == 0)
    return kInstalledLauncher;
  return executable;
}

bool EnableAutostart(const std::string& app_name,
                     const std::string& executable_path) {
  const std::filesystem::path path = AutostartPath(app_name);
  if (path.empty()) return false;

  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) return false;

  return WriteDesktopEntry(path,
      "[Desktop Entry]\n"
      "Type=Application\n"
      "Exec=" + executable_path + "\n"
      "Hidden=false\n"
      "NoDisplay=false\n"
      "X-GNOME-Autostart-enabled=true\n"
      "Terminal=false\n"
      "StartupNotify=false\n"
      "Name=" + app_name + "\n");
}

bool MigrateLegacyAutostart(const std::string& app_name) {
  // Development/standalone copies must not take over an installed entry.
  if (GetAutostartExecutablePath() != kInstalledLauncher) return true;
  const std::filesystem::path path = AutostartPath(app_name);
  if (path.empty()) return false;
  std::error_code error;
  const bool exists = std::filesystem::exists(path, error);
  if (error) return false;
  if (!exists) return true;  // Do not recreate entries removed by the user.
  if (!std::filesystem::is_regular_file(path, error) || error) return false;

  std::ifstream file(path);
  if (!file.is_open()) return false;
  std::string contents((std::istreambuf_iterator<char>(file)), {});
  if (file.bad()) return false;
  file.close();

  bool desktop_entry = false;
  for (size_t start = 0; start < contents.size();) {
    const size_t newline = contents.find('\n', start);
    const size_t end = newline == std::string::npos ? contents.size() : newline;
    size_t line_end = end;
    if (line_end > start && contents[line_end - 1] == '\r') --line_end;
    const std::string line = contents.substr(start, line_end - start);
    if (!line.empty() && line.front() == '[')
      desktop_entry = line == "[Desktop Entry]";
    if (desktop_entry && line == std::string("Exec=") + kInstalledExecutable) {
      // Preserve Hidden, desktop restrictions and all other user settings.
      contents.replace(start, line_end - start,
                       std::string("Exec=") + kInstalledLauncher);
      return WriteDesktopEntry(path, contents);
    }
    start = end == contents.size() ? end : end + 1;
  }
  return true;
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
