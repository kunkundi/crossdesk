#include "path_manager.h"

#include <cstdint>
#include <cstdlib>
#include <vector>

#ifndef CROSSDESK_PORTABLE
#define CROSSDESK_PORTABLE 0
#endif

#if CROSSDESK_PORTABLE
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif !defined(_WIN32)
#include <limits.h>
#include <unistd.h>
#endif
#endif

namespace {

#if CROSSDESK_PORTABLE
std::filesystem::path GetExecutableDirectory() {
#ifdef _WIN32
  std::vector<wchar_t> buffer(MAX_PATH);
  while (true) {
    DWORD length =
        GetModuleFileNameW(nullptr, buffer.data(),
                           static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return {};
    }
    if (length < buffer.size()) {
      return std::filesystem::path(buffer.data(), buffer.data() + length)
          .parent_path();
    }
    if (buffer.size() >= 32768) {
      return {};
    }
    buffer.resize(buffer.size() * 2);
  }
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size + 1);
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return {};
  }

  std::error_code ec;
  std::filesystem::path executable =
      std::filesystem::weakly_canonical(buffer.data(), ec);
  if (ec) {
    executable = buffer.data();
  }
  return executable.parent_path();
#else
  std::vector<char> buffer(PATH_MAX);
  while (true) {
    ssize_t length = readlink("/proc/self/exe", buffer.data(),
                              buffer.size() - 1);
    if (length <= 0) {
      return {};
    }
    if (static_cast<size_t>(length) < buffer.size() - 1) {
      buffer[static_cast<size_t>(length)] = '\0';
      return std::filesystem::path(buffer.data()).parent_path();
    }
    buffer.resize(buffer.size() * 2);
  }
#endif
}

std::filesystem::path GetPortableRootPath() {
  std::filesystem::path executable_dir = GetExecutableDirectory();
  if (!executable_dir.empty()) {
    return executable_dir;
  }

  std::error_code ec;
  std::filesystem::path current = std::filesystem::current_path(ec);
  return ec ? std::filesystem::path(".") : current;
}
#endif

}  // namespace

namespace crossdesk {

PathManager::PathManager(const std::string& app_name) : app_name_(app_name) {}

std::filesystem::path PathManager::GetConfigPath() {
#if CROSSDESK_PORTABLE
  return GetPortableRootPath() / "data";
#else
#ifdef _WIN32
  return GetKnownFolder(FOLDERID_RoamingAppData) / app_name_;
#elif __APPLE__
  return GetEnvOrDefault("XDG_CONFIG_HOME", GetHome() + "/.config") / app_name_;
#else
  return GetEnvOrDefault("XDG_CONFIG_HOME", GetHome() + "/.config") / app_name_;
#endif
#endif
}

std::filesystem::path PathManager::GetCachePath() {
#if CROSSDESK_PORTABLE
  return GetPortableRootPath() / "data";
#else
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
#endif
}

std::filesystem::path PathManager::GetLogPath() {
#if CROSSDESK_PORTABLE
  return GetPortableRootPath() / "logs";
#else
#ifdef _WIN32
  return GetKnownFolder(FOLDERID_LocalAppData) / app_name_ / "logs";
#elif __APPLE__
  return GetHome() + "/Library/Logs/" + app_name_;
#else
  return GetCachePath() / "logs";
#endif
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
