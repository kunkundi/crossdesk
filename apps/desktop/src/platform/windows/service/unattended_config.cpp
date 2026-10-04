#include "unattended_config.h"

#include <Aclapi.h>
#include <ShlObj.h>
#include <sddl.h>

#include <ctime>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>

namespace crossdesk {
namespace {
constexpr wchar_t kRegistryKey[] = L"SOFTWARE\\CrossDesk\\Unattended";
constexpr wchar_t kDirectorySecurity[] =
    L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";

bool TrustedSid(PSID sid) {
  return sid && (IsWellKnownSid(sid, WinLocalSystemSid) ||
                 IsWellKnownSid(sid, WinBuiltinAdministratorsSid));
}

bool PrivateDirectory(const std::filesystem::path& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
    return false;
  PSID owner = nullptr;
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (GetNamedSecurityInfoW(
          const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
          OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
          nullptr, &dacl, nullptr, &descriptor) != ERROR_SUCCESS)
    return false;
  bool safe = TrustedSid(owner) && dacl && dacl->AceCount > 0;
  for (DWORD index = 0; safe && index < dacl->AceCount; ++index) {
    void* raw = nullptr;
    if (!GetAce(dacl, index, &raw)) {
      safe = false;
      break;
    }
    auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
    // Only SYSTEM and elevated administrators can read credentials or write
    // configuration that the service will consume as SYSTEM.
    safe =
        ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
        TrustedSid(&ace->SidStart) &&
        (ace->Header.AceFlags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) ==
            (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE);
  }
  LocalFree(descriptor);
  return safe;
}
}  // namespace

std::filesystem::path UnattendedDataPath() {
  PWSTR value = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DEFAULT,
                                  nullptr, &value)))
    return {};
  const auto path = std::filesystem::path(value) / L"CrossDeskUnattended";
  CoTaskMemFree(value);
  return path;
}

bool IsUnattendedEnabled() {
  DWORD enabled = 0;
  DWORD size = sizeof(enabled);
  return RegGetValueW(HKEY_LOCAL_MACHINE, kRegistryKey, L"Enabled",
                      RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr,
                      &enabled, &size) == ERROR_SUCCESS &&
         enabled == 1;
}

bool SetUnattendedEnabled(bool enabled) {
  if (!IsAdministratorProcess()) return false;
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kRegistryKey, 0, nullptr, 0,
                      KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key,
                      nullptr) != ERROR_SUCCESS)
    return false;
  const DWORD value = enabled ? 1 : 0;
  const auto result =
      RegSetValueExW(key, L"Enabled", 0, REG_DWORD,
                     reinterpret_cast<const BYTE*>(&value), sizeof(value));
  RegCloseKey(key);
  return result == ERROR_SUCCESS;
}

bool IsAdministratorProcess() {
  BYTE sid[SECURITY_MAX_SID_SIZE];
  DWORD size = sizeof(sid);
  BOOL member = FALSE;
  return CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sid, &size) &&
         CheckTokenMembership(nullptr, sid, &member) && member;
}

bool IsLocalSystemProcess() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<BYTE> buffer(size);
  const bool system =
      GetTokenInformation(token, TokenUser, buffer.data(), size, &size) &&
      IsWellKnownSid(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,
                     WinLocalSystemSid);
  CloseHandle(token);
  return system;
}

bool PrepareUnattendedDirectory(bool create) {
  return unattended_detail::PreparePrivateDirectory(UnattendedDataPath(),
                                                    create);
}

bool unattended_detail::PreparePrivateDirectory(
    const std::filesystem::path& root, bool create) {
  if (root.empty()) return false;
  if (create) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kDirectorySecurity, SDDL_REVISION_1, &descriptor, nullptr))
      return false;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor,
                                   FALSE};
    const bool created = CreateDirectoryW(root.c_str(), &attributes) != FALSE;
    const DWORD error = created ? ERROR_SUCCESS : GetLastError();
    LocalFree(descriptor);
    if (!created && error != ERROR_ALREADY_EXISTS) return false;
  }
  return PrivateDirectory(root);
}

bool WriteUnattendedStatus(const std::string& identity, bool online,
                           DWORD session_id) {
  const auto root = UnattendedDataPath();
  if (root.empty()) return false;
  const auto separator = identity.find('@');
  nlohmann::json status = {
      {"online", online},
      {"process_id", GetCurrentProcessId()},
      {"session_id", session_id},
      {"updated_at", std::time(nullptr)},
      {"device_id",
       separator == std::string::npos ? "" : identity.substr(0, separator)},
      {"password",
       separator == std::string::npos ? "" : identity.substr(separator + 1)}};
  const auto temporary = root / L"status.tmp";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  output << status.dump(2);
  output.close();
  return !output.fail() &&
         MoveFileExW(temporary.c_str(), (root / L"status.json").c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

std::string ReadUnattendedStatus() {
  nlohmann::json status = {{"enabled", IsUnattendedEnabled()},
                           {"online", false}};
  if (!IsAdministratorProcess() || !PrepareUnattendedDirectory(false)) {
    status["error"] = "administrator_required_or_private_directory_unavailable";
    return status.dump(2);
  }
  std::ifstream input(UnattendedDataPath() / L"status.json", std::ios::binary);
  auto saved = nlohmann::json::parse(input, nullptr, false);
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
