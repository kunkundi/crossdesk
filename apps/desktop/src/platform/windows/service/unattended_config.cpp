#include "unattended_config.h"

#include <ShlObj.h>

#include <ctime>
#include <nlohmann/json.hpp>

namespace crossdesk {
namespace {
constexpr wchar_t kRegistryKey[] = L"SOFTWARE\\CrossDesk\\Unattended";
std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
  PWSTR value = nullptr;
  if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &value)))
    return {};
  const std::filesystem::path path(value);
  CoTaskMemFree(value);
  return path;
}
}  // namespace

std::filesystem::path UnattendedDataPath() {
  const auto base = KnownFolder(FOLDERID_ProgramData);
  return base.empty() ? base : base / L"CrossDeskUnattended";
}

std::filesystem::path UnattendedLogPath() {
  // The logger opens and rotates files by name. Keep these writes in the
  // SYSTEM profile even when the machine identity directory needs recovery.
  const auto base = KnownFolder(FOLDERID_LocalAppData);
  return base.empty() ? base : base / L"CrossDesk" / L"logs" / L"unattended";
}

bool QueryUnattendedPreference(std::optional<bool>& enabled) {
  enabled.reset();
  DWORD value = 0;
  DWORD size = sizeof(value);
  const LONG result = RegGetValueW(HKEY_LOCAL_MACHINE, kRegistryKey, L"Enabled",
      RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &value, &size);
  if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) return true;
  if (result != ERROR_SUCCESS || value > 1) return false;
  enabled = value == 1;
  return true;
}

bool QueryUnattendedEnabled(bool& enabled) {
  std::optional<bool> preference;
  const bool ok = QueryUnattendedPreference(preference);
  enabled = preference.value_or(false);
  return ok;
}

bool IsUnattendedEnabled() {
  bool enabled = false;
  return QueryUnattendedEnabled(enabled) && enabled;
}

bool SetUnattendedEnabled(std::optional<bool> enabled) {
  if (!IsAdministratorProcess()) return false;
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kRegistryKey, 0, nullptr, 0,
                      KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key,
                      nullptr) != ERROR_SUCCESS)
    return false;
  if (!enabled.has_value()) {
    const LONG result = RegDeleteValueW(key, L"Enabled");
    RegCloseKey(key);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
  }
  const DWORD value = *enabled ? 1 : 0;
  auto result =
      RegSetValueExW(key, L"Enabled", 0, REG_DWORD,
                     reinterpret_cast<const BYTE*>(&value), sizeof(value));
  if (result == ERROR_SUCCESS && *enabled) {
    const DWORD version = 1;
    result = RegSetValueExW(key, L"SharedIdentityVersion", 0, REG_DWORD,
                            reinterpret_cast<const BYTE*>(&version), sizeof(version));
  }
  RegCloseKey(key);
  return result == ERROR_SUCCESS;
}

bool IsSharedIdentityServiceConfigured() {
  DWORD version = 0;
  DWORD size = sizeof(version);
  return RegGetValueW(HKEY_LOCAL_MACHINE, kRegistryKey, L"SharedIdentityVersion",
      RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &version, &size) == ERROR_SUCCESS && version == 1;
}

bool PrepareUnattendedDirectory(bool create, std::string* error) {
  return unattended_detail::PreparePrivateDirectory(UnattendedDataPath(),
                                                    create, error);
}

bool RepairUnattendedDirectory(bool create, std::string* error,
                               bool* repaired) {
  if (!IsLocalSystemProcess() && !IsAdministratorProcess()) {
    if (error) *error = "administrator or SYSTEM privileges required";
    if (repaired) *repaired = false;
    return false;
  }
  return unattended_detail::RepairPrivateDirectory(UnattendedDataPath(), create,
                                                   error, repaired);
}

bool PrepareUnattendedDirectoryForService(std::string* error, bool* repaired) {
  if (!IsLocalSystemProcess()) {
    if (error) *error = "SYSTEM privileges required";
    if (repaired) *repaired = false;
    return false;
  }
  return unattended_detail::RepairPrivateDirectory(UnattendedDataPath(), true,
                                                   error, repaired);
}

bool WriteUnattendedStatus(const std::string& identity, bool online,
                           DWORD session_id, std::string* diagnostic) {
  if (diagnostic) diagnostic->clear();
  const auto root = UnattendedDataPath();
  if (root.empty()) return false;
  if (IsLocalSystemProcess() &&
      !unattended_detail::PreparePrivateDirectory(root, false) &&
      !unattended_detail::RepairPrivateDirectory(root, false, diagnostic)) return false;
  const auto separator = identity.find('@');
  nlohmann::json status = {
      {"online", online},
      {"mode", "service"},
      {"process_id", GetCurrentProcessId()},
      {"session_id", session_id},
      {"updated_at", std::time(nullptr)},
      {"device_id",
       separator == std::string::npos ? "" : identity.substr(0, separator)},
      {"password",
       separator == std::string::npos ? "" : identity.substr(separator + 1)}};
  if (unattended_detail::WritePrivateMachineFile(root, root / L"status.json", status.dump()))
    return true;
  const DWORD code = GetLastError();
  if (diagnostic) *diagnostic = "Cannot write status.json (Win32=" + std::to_string(code) + ")";
  return false;
}

std::string ReadUnattendedStatus() {
  nlohmann::json status = {{"enabled", IsUnattendedEnabled()},
                           {"online", false}};
  std::string detail;
  if (!IsAdministratorProcess() || !PrepareUnattendedDirectory(false, &detail)) {
    status["error"] = "administrator_required_or_private_directory_unavailable";
    if (!detail.empty()) status["detail"] = detail;
    return status.dump(2);
  }
  const auto root = UnattendedDataPath();
  HANDLE input = unattended_detail::OpenPrivateMachineFile(
      root, root / L"status.json", GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, &detail);
  if (input == INVALID_HANDLE_VALUE) {
    if (!detail.empty()) status["error"] = detail;
    return status.dump(2);
  }
  // A status heartbeat contains just the current PID, session and identity.
  char buffer[4096];
  DWORD size = 0;
  const bool read = ReadFile(input, buffer, sizeof(buffer), &size, nullptr);
  CloseHandle(input);
  if (!read || size == sizeof(buffer)) return status.dump(2);
  auto saved = nlohmann::json::parse(buffer, buffer + size, nullptr, false);
  if (saved.is_object()) {
    status.update(saved);
    const auto updated = status.value("updated_at", std::time_t{0});
    const auto now = std::time(nullptr);
    HANDLE process =
        OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                    status.value("process_id", 0u));
    const bool running =
        process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    if (process) CloseHandle(process);
    status["online"] = IsUnattendedEnabled() && running && updated <= now &&
                       now - updated < 10 && status.value("online", false);
  }
  return status.dump(2);
}
}  // namespace crossdesk
