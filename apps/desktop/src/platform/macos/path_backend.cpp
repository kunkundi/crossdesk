#include "platform/path_backend.h"

#include <mach-o/dyld.h>

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
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size + 1);
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
  std::error_code error;
  std::filesystem::path executable =
      std::filesystem::weakly_canonical(buffer.data(), error);
  if (error) executable = buffer.data();
  return executable.parent_path();
}

std::filesystem::path GetConfigPath(const std::string& app_name) {
  return EnvOrDefault("XDG_CONFIG_HOME", Home() / ".config") / app_name;
}

std::filesystem::path GetCachePath(const std::string& app_name) {
  return EnvOrDefault("XDG_CACHE_HOME", Home() / ".cache") / app_name;
}

std::filesystem::path GetLogPath(const std::string& app_name) {
  return Home() / "Library" / "Logs" / app_name;
}

}  // namespace crossdesk::platform
