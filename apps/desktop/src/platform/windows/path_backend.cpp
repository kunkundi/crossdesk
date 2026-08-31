#include "platform/path_backend.h"

#include <shlobj.h>
#include <windows.h>

#include <vector>

namespace crossdesk::platform {
namespace {

std::filesystem::path GetKnownFolder(REFKNOWNFOLDERID id) {
  PWSTR value = nullptr;
  if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &value))) return {};
  const std::filesystem::path result(value);
  CoTaskMemFree(value);
  return result;
}

}  // namespace

std::filesystem::path GetExecutableDirectory() {
  std::vector<wchar_t> buffer(MAX_PATH);
  while (true) {
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || buffer.size() >= 32768) return {};
    if (length < buffer.size()) {
      return std::filesystem::path(buffer.data(), buffer.data() + length)
          .parent_path();
    }
    buffer.resize(buffer.size() * 2);
  }
}

std::filesystem::path GetConfigPath(const std::string& app_name) {
  return GetKnownFolder(FOLDERID_RoamingAppData) / app_name;
}

std::filesystem::path GetCachePath(const std::string& app_name) {
#ifdef CROSSDESK_DEBUG
  return "cache";
#else
  return GetKnownFolder(FOLDERID_LocalAppData) / app_name / "cache";
#endif
}

std::filesystem::path GetLogPath(const std::string& app_name) {
  return GetKnownFolder(FOLDERID_LocalAppData) / app_name / "logs";
}

}  // namespace crossdesk::platform
