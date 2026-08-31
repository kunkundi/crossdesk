#include "platform/autostart_backend.h"

#include <windows.h>

#include <filesystem>
#include <string>

namespace crossdesk::platform {
namespace {

constexpr const char* kRunKey =
    "Software\\Microsoft\\Windows\\CurrentVersion\\Run";

}  // namespace

std::string GetAutostartExecutablePath() {
  char path[32768] = {};
  const DWORD length = GetModuleFileNameA(nullptr, path, sizeof(path));
  return length > 0 && length < sizeof(path) ? std::string(path, length)
                                             : std::string();
}

bool EnableAutostart(const std::string& app_name,
                     const std::string& executable_path) {
  if (!std::filesystem::exists(executable_path)) return false;

  HKEY key = nullptr;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &key) !=
      ERROR_SUCCESS) {
    return false;
  }

  std::string value = executable_path;
  if (value.find(' ') != std::string::npos &&
      (value.front() != '"' || value.back() != '"')) {
    value = "\"" + value + "\"";
  }
  const LONG result = RegSetValueExA(
      key, app_name.c_str(), 0, REG_SZ,
      reinterpret_cast<const BYTE*>(value.c_str()),
      static_cast<DWORD>(value.size() + 1));
  RegCloseKey(key);
  return result == ERROR_SUCCESS;
}

bool DisableAutostart(const std::string& app_name) {
  HKEY key = nullptr;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &key) !=
      ERROR_SUCCESS) {
    return false;
  }
  const LONG result = RegDeleteValueA(key, app_name.c_str());
  RegCloseKey(key);
  return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool IsAutostartEnabled(const std::string& app_name) {
  HKEY key = nullptr;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) !=
      ERROR_SUCCESS) {
    return false;
  }
  const LONG result =
      RegQueryValueExA(key, app_name.c_str(), nullptr, nullptr, nullptr, nullptr);
  RegCloseKey(key);
  return result == ERROR_SUCCESS;
}

}  // namespace crossdesk::platform
