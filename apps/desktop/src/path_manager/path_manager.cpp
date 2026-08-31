#include "path_manager.h"

#include "platform/path_backend.h"

#ifndef CROSSDESK_PORTABLE
#define CROSSDESK_PORTABLE 0
#endif

namespace {

#if CROSSDESK_PORTABLE
std::filesystem::path GetPortableRootPath() {
  std::filesystem::path executable_dir =
      crossdesk::platform::GetExecutableDirectory();
  if (!executable_dir.empty()) return executable_dir;

  std::error_code error;
  const std::filesystem::path current =
      std::filesystem::current_path(error);
  return error ? std::filesystem::path(".") : current;
}
#endif

}  // namespace

namespace crossdesk {

PathManager::PathManager(const std::string& app_name) : app_name_(app_name) {}

std::filesystem::path PathManager::GetConfigPath() {
#if CROSSDESK_PORTABLE
  return GetPortableRootPath() / "data";
#else
  return platform::GetConfigPath(app_name_);
#endif
}

std::filesystem::path PathManager::GetCachePath() {
#if CROSSDESK_PORTABLE
  return GetPortableRootPath() / "data";
#else
  return platform::GetCachePath(app_name_);
#endif
}

std::filesystem::path PathManager::GetLogPath() {
#if CROSSDESK_PORTABLE
  return GetPortableRootPath() / "logs";
#else
  return platform::GetLogPath(app_name_);
#endif
}

bool PathManager::CreateDirectories(const std::filesystem::path& path) {
  std::error_code error;
  const bool created = std::filesystem::create_directories(path, error);
  return !error && (created || std::filesystem::exists(path));
}

}  // namespace crossdesk
