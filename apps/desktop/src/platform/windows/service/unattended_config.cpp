#include "unattended_config.h"

#include <Aclapi.h>
#include <ShlObj.h>
#include <sddl.h>

#include <ctime>
#include <atomic>
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

bool PrivateAccess(PSID owner, PACL acl, BYTE required_flags = 0) {
  if (!TrustedSid(owner) || !acl || !acl->AceCount) return false;
  for (DWORD i = 0; i < acl->AceCount; ++i) {
    void* raw = nullptr;
    if (!GetAce(acl, i, &raw)) return false;
    auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
    // Credentials and SYSTEM configuration are restricted to SYSTEM/admins.
    if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE ||
        !TrustedSid(&ace->SidStart) ||
        (ace->Header.AceFlags & required_flags) != required_flags) return false;
  }
  return true;
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
  const bool safe = PrivateAccess(owner, dacl, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE);
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
  auto result =
      RegSetValueExW(key, L"Enabled", 0, REG_DWORD,
                     reinterpret_cast<const BYTE*>(&value), sizeof(value));
  if (result == ERROR_SUCCESS && enabled) {
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

bool PrepareUnattendedDirectoryForService() {
  if (!IsLocalSystemProcess()) return false;
  const auto root = UnattendedDataPath();
  // Only adopt an already trusted SYSTEM/Administrators directory. Never
  // repair a permissive, linked, or ordinary-user-owned machine profile.
  if (!unattended_detail::PreparePrivateDirectory(root, true)) return false;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          kDirectorySecurity, SDDL_REVISION_1, &descriptor, nullptr)) return false;
  PACL acl = nullptr;
  BOOL present = FALSE, defaulted = FALSE;
  const bool updated = GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) &&
      present && SetNamedSecurityInfoW(const_cast<wchar_t*>(root.c_str()), SE_FILE_OBJECT,
          DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
          nullptr, nullptr, acl, nullptr) == ERROR_SUCCESS;
  LocalFree(descriptor);
  return updated && PrivateDirectory(root);
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

HANDLE unattended_detail::OpenPrivateMachineFile(const std::filesystem::path& root,
    const std::filesystem::path& path, DWORD access, DWORD sharing,
    DWORD creation, DWORD flags) {
  if (path.parent_path() != root || !PreparePrivateDirectory(root, false)) {
    SetLastError(ERROR_ACCESS_DENIED);
    return INVALID_HANDLE_VALUE;
  }
  PSECURITY_DESCRIPTOR security = nullptr;
  const auto sddl = IsLocalSystemProcess()
      ? L"O:SYG:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)"
      : L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)";
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          sddl, SDDL_REVISION_1, &security, nullptr)) return INVALID_HANDLE_VALUE;
  SECURITY_ATTRIBUTES attributes{sizeof(attributes), security, FALSE};
  HANDLE file = CreateFileW(path.c_str(), access | READ_CONTROL, sharing,
      &attributes, creation, flags | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  const DWORD error = GetLastError();
  LocalFree(security);
  if (file == INVALID_HANDLE_VALUE) { SetLastError(error); return file; }
  BY_HANDLE_FILE_INFORMATION info{};
  PSID owner = nullptr;
  PACL acl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const bool safe = GetFileInformationByHandle(file, &info) && info.nNumberOfLinks == 1 &&
      !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
      GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                      &owner, nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS &&
      PrivateAccess(owner, acl);
  if (descriptor) LocalFree(descriptor);
  if (safe) return file;
  CloseHandle(file);
  SetLastError(ERROR_ACCESS_DENIED);
  return INVALID_HANDLE_VALUE;
}

bool unattended_detail::WritePrivateMachineFile(const std::filesystem::path& root,
    const std::filesystem::path& path, const std::string& contents) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES &&
      (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) return false;
  static std::atomic<unsigned long> sequence{0};
  const auto temporary = path.wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId()) +
      L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(sequence++);
  HANDLE output = OpenPrivateMachineFile(
      root, temporary, GENERIC_WRITE, 0, CREATE_NEW);
  if (output == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool saved = WriteFile(output, contents.data(), static_cast<DWORD>(contents.size()),
      &written, nullptr) && written == contents.size() && FlushFileBuffers(output);
  CloseHandle(output);
  if (saved && MoveFileExW(temporary.c_str(), path.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
  DeleteFileW(temporary.c_str());
  return false;
}

bool WriteUnattendedStatus(const std::string& identity, bool online,
                           DWORD session_id) {
  const auto root = UnattendedDataPath();
  if (root.empty()) return false;
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
  return unattended_detail::WritePrivateMachineFile(root, root / L"status.json", status.dump());
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
