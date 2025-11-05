#include "path_manager.h"

#include <cstdlib>

namespace crossdesk {

PathManager::PathManager(const std::string& app_name) : app_name_(app_name) {}

std::filesystem::path PathManager::GetConfigPath() {
#ifdef _WIN32
  return GetKnownFolder(FOLDERID_RoamingAppData) / app_name_;
#elif __APPLE__
  return GetEnvOrDefault("XDG_CONFIG_HOME", GetHome() + "/.config") / app_name_;
#else
  return GetEnvOrDefault("XDG_CONFIG_HOME", GetHome() + "/.config") / app_name_;
#endif
}

std::filesystem::path PathManager::GetCachePath() {
#ifdef _WIN32
#ifdef CROSSDESK_DEBUG
  return "cache";
#else
  return GetKnownFolder(FOLDERID_LocalAppData) / app_name_ / "cache";
#endif
#elif __APPLE__
  return GetEnvOrDefault("XDG_CACHE_HOME", GetHome() + "/.cache") / app_name_;
#else
  return GetEnvOrDefault("XDG_CACHE_HOME", GetHome() + "/.cache") / app_name_;
#endif
}

std::filesystem::path PathManager::GetLogPath() {
#ifdef _WIN32
  return GetKnownFolder(FOLDERID_LocalAppData) / app_name_ / "logs";
#elif __APPLE__
  return GetHome() + "/Library/Logs/" + app_name_;
#else
  return GetCachePath() / app_name_ / "logs";
#endif
}

std::filesystem::path PathManager::GetCertPath() {
#ifdef _WIN32
  // %APPDATA%\AppName\Certs
  return GetKnownFolder(FOLDERID_RoamingAppData) / app_name_ / "certs";
#elif __APPLE__
  // $HOME/Library/Application Support/AppName/certs
  return GetHome() + "/Library/Application Support/" + app_name_ + "/certs";
#else
  // $XDG_CONFIG_HOME/AppName/certs
  return GetEnvOrDefault("XDG_CONFIG_HOME", GetHome() + "/.config") /
         app_name_ / "certs";
#endif
}

bool PathManager::CreateDirectories(const std::filesystem::path& p) {
  std::error_code ec;
  bool created = std::filesystem::create_directories(p, ec);
  if (ec) {
    return false;
  }
  return created || std::filesystem::exists(p);
}

#ifdef _WIN32
std::filesystem::path PathManager::GetKnownFolder(REFKNOWNFOLDERID id) {
  PWSTR path = NULL;
  if (SUCCEEDED(SHGetKnownFolderPath(id, 0, NULL, &path))) {
    std::wstring wpath(path);
    CoTaskMemFree(path);
    return std::filesystem::path(wpath);
  }
  return {};
}
#endif

std::string PathManager::GetHome() {
#ifdef _WIN32
  char path[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path)))
    return std::string(path);
#else
  if (const char* home = getenv("HOME")) {
    return std::string(home);
  }
#endif
  return {};
}

std::filesystem::path PathManager::GetEnvOrDefault(const char* env_var,
                                                   const std::string& def) {
#ifdef _WIN32
  char* buffer = nullptr;
  size_t size = 0;

  if (_dupenv_s(&buffer, &size, env_var) == 0 && buffer != nullptr) {
    std::filesystem::path result(buffer);
    free(buffer);
    return result;
  }
#else
  if (const char* val = getenv(env_var)) {
    return std::filesystem::path(val);
  }
#endif

  return std::filesystem::path(def);
}
}  // namespace crossdesk