#include "platform/path_backend.h"

#include <limits.h>
#include <unistd.h>

#include <cstdlib>
#include <vector>

namespace crossdesk::platform {
namespace {

std::filesystem::path Home() {
  const char* value = std::getenv("HOME");
  return value ? std::filesystem::path(value) : std::filesystem::path();
}

std::filesystem::path EnvOrDefault(const char* name,
                                   const std::filesystem::path& fallback) {
  const char* value = std::getenv(name);
  return value ? std::filesystem::path(value) : fallback;
}

}  // namespace

std::filesystem::path GetExecutableDirectory() {
  std::vector<char> buffer(PATH_MAX);
  while (true) {
    const ssize_t length =
        readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0) return {};
    if (static_cast<size_t>(length) < buffer.size() - 1) {
      buffer[static_cast<size_t>(length)] = '\0';
      return std::filesystem::path(buffer.data()).parent_path();
    }
    buffer.resize(buffer.size() * 2);
  }
}

std::filesystem::path GetConfigPath(const std::string& app_name) {
  return EnvOrDefault("XDG_CONFIG_HOME", Home() / ".config") / app_name;
}

std::filesystem::path GetCachePath(const std::string& app_name) {
  return EnvOrDefault("XDG_CACHE_HOME", Home() / ".cache") / app_name;
}

std::filesystem::path GetLogPath(const std::string& app_name) {
  return GetCachePath(app_name) / "logs";
}

}  // namespace crossdesk::platform
