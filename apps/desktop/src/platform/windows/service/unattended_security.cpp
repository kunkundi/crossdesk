#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "unattended_security.h"
#include "private_file.h"

#include <Aclapi.h>
#include <sddl.h>
#include <winternl.h>

// The Windows SDK exposes the Ntdll import library, but declares this routine
// only in the WDK headers.
extern "C" NTSYSAPI NTSTATUS NTAPI NtSetSecurityObject(
    HANDLE handle, SECURITY_INFORMATION information,
    PSECURITY_DESCRIPTOR descriptor);

#include <atomic>
#include <cstdio>
#include <memory>
#include <vector>

namespace crossdesk {
namespace {

struct ScopedHandle {
  HANDLE value = INVALID_HANDLE_VALUE;
  ScopedHandle() = default;
  explicit ScopedHandle(HANDLE handle) : value(handle) {}
  ScopedHandle(const ScopedHandle&) = delete;
  ScopedHandle& operator=(const ScopedHandle&) = delete;
  ~ScopedHandle() { Reset(); }
  void Reset(HANDLE next = INVALID_HANDLE_VALUE) {
    if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
    value = next;
  }
};

using LocalBuffer = std::unique_ptr<void, decltype(&LocalFree)>;

std::string WideToUtf8(const wchar_t* value) {
  if (!value || !*value) return {};
  const int needed = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0,
                                         nullptr, nullptr);
  if (needed <= 1) return {};
  std::string out(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), needed, nullptr,
                      nullptr);
  out.pop_back();
  return out;
}

std::string FormatSid(PSID sid) {
  if (!sid || !IsValidSid(sid)) return "invalid-sid";
  LPWSTR raw = nullptr;
  if (!ConvertSidToStringSidW(sid, &raw)) return "unconvertible-sid";
  LocalBuffer guard(raw, LocalFree);
  return WideToUtf8(raw);
}

std::string FormatHex(DWORD value) {
  char buffer[16]{};
  std::snprintf(buffer, sizeof(buffer), "0x%lx",
                static_cast<unsigned long>(value));
  return buffer;
}

bool ParseTrustedSids(const unattended_detail::SecurityPolicy& policy,
                      std::vector<LocalBuffer>* out) {
  out->clear();
  if (policy.trusted_sids.empty()) return false;
  out->reserve(policy.trusted_sids.size());
  for (const auto& text : policy.trusted_sids) {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(text.c_str(), &sid)) return false;
    out->emplace_back(sid, LocalFree);
  }
  return true;
}

bool MatchesTrustedSid(PSID sid, const std::vector<LocalBuffer>& trusted) {
  if (!sid || !IsValidSid(sid)) return false;
  for (const auto& candidate : trusted) {
    if (EqualSid(sid, candidate.get())) return true;
  }
  return false;
}

bool ExtractAceSidAndMask(const ACE_HEADER* header, PSID* sid_out,
                          ACCESS_MASK* mask_out) {
  *sid_out = nullptr;
  *mask_out = 0;
  if (!header) return false;
  switch (header->AceType) {
    case ACCESS_ALLOWED_ACE_TYPE:
    case ACCESS_DENIED_ACE_TYPE:
    case SYSTEM_AUDIT_ACE_TYPE:
    case SYSTEM_ALARM_ACE_TYPE: {
      const auto* ace = reinterpret_cast<const ACCESS_ALLOWED_ACE*>(header);
      *mask_out = ace->Mask;
      *sid_out = const_cast<DWORD*>(&ace->SidStart);
      return true;
    }
    default:
      return false;
  }
}

bool ParentAllowsSafeTarget(const std::filesystem::path& target,
                            std::string* diagnostic) {
  const auto parent = target.parent_path();
  if (parent.empty()) {
    if (diagnostic) *diagnostic = "target path has no parent directory";
    return false;
  }
  ScopedHandle handle{CreateFileW(
      parent.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
      nullptr)};
  if (handle.value == INVALID_HANDLE_VALUE) {
    if (diagnostic) {
      *diagnostic = "cannot open parent directory (Win32=" +
                    std::to_string(GetLastError()) + ")";
    }
    return false;
  }
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(handle.value, &info)) {
    if (diagnostic) {
      *diagnostic = "cannot inspect parent directory (Win32=" +
                    std::to_string(GetLastError()) + ")";
    }
    return false;
  }
  FILE_ATTRIBUTE_TAG_INFO tag{};
  const bool has_tag = GetFileInformationByHandleEx(
                           handle.value, FileAttributeTagInfo, &tag,
                           sizeof(tag)) != FALSE;
  if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      (has_tag && ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
                   tag.ReparseTag != 0))) {
    if (diagnostic) *diagnostic = "parent directory reparse point rejected";
    return false;
  }
  if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    if (diagnostic) *diagnostic = "parent path is not a directory";
    return false;
  }
  return true;
}

bool VerifyHandleNotReparse(HANDLE handle, bool expect_directory,
                            BY_HANDLE_FILE_INFORMATION* info_out,
                            std::string* diagnostic) {
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(handle, &info)) {
    if (diagnostic) {
      *diagnostic = "GetFileInformationByHandle failed (Win32=" +
                    std::to_string(GetLastError()) + ")";
    }
    return false;
  }
  FILE_ATTRIBUTE_TAG_INFO tag{};
  const bool has_tag = GetFileInformationByHandleEx(
                           handle, FileAttributeTagInfo, &tag, sizeof(tag)) !=
                       FALSE;
  if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      (has_tag && ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
                   tag.ReparseTag != 0))) {
    if (diagnostic) *diagnostic = "reparse point rejected";
    return false;
  }
  if (expect_directory) {
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      if (diagnostic) *diagnostic = "path is not a directory";
      return false;
    }
  } else {
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      if (diagnostic) *diagnostic = "path is a directory";
      return false;
    }
    if (info.nNumberOfLinks != 1) {
      if (diagnostic) {
        *diagnostic = "hardlinked file rejected (links=" +
                      std::to_string(info.nNumberOfLinks) + ")";
      }
      return false;
    }
  }
  if (info_out) *info_out = info;
  return true;
}

struct SecurityEvaluation {
  bool owner_trusted = false;
  bool private_access = false;
  bool canonical_complete = false;
  std::string diagnostic;
};

bool EvaluateHandleSecurity(HANDLE handle, bool directory,
                            const std::vector<LocalBuffer>& trusted,
                            SecurityEvaluation* out) {
  *out = SecurityEvaluation{};
  PSID owner = nullptr;
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const DWORD status = GetSecurityInfo(
      handle, SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
      &dacl, nullptr, &descriptor);
  if (status != ERROR_SUCCESS) {
    out->diagnostic =
        "GetSecurityInfo failed (Win32=" + std::to_string(status) + ")";
    return false;
  }
  LocalBuffer guard(descriptor, LocalFree);
  if (!MatchesTrustedSid(owner, trusted)) {
    out->owner_trusted = false;
    out->diagnostic = "untrusted owner " + FormatSid(owner);
    return true;
  }
  out->owner_trusted = true;
  if (!dacl || dacl->AceCount == 0) {
    out->diagnostic = "empty or null DACL";
    return true;
  }

  const BYTE required_flags =
      directory ? static_cast<BYTE>(OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)
                : 0;
  std::vector<bool> explicit_full_access(trusted.size(), false);
  for (DWORD i = 0; i < dacl->AceCount; ++i) {
    void* raw = nullptr;
    if (!GetAce(dacl, i, &raw)) {
      out->diagnostic = "cannot read ACE at index " + std::to_string(i);
      return true;
    }
    const auto* header = static_cast<const ACE_HEADER*>(raw);
    PSID ace_sid = nullptr;
    ACCESS_MASK mask = 0;
    const bool known_layout = ExtractAceSidAndMask(header, &ace_sid, &mask);
    // Credentials and SYSTEM configuration are restricted to trusted SIDs.
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || !known_layout ||
        !MatchesTrustedSid(ace_sid, trusted) ||
        (header->AceFlags & required_flags) != required_flags) {
      out->diagnostic =
          "unexpected ACE type=" + std::to_string(header->AceType) +
          " flags=" + FormatHex(header->AceFlags) +
          " mask=" + FormatHex(mask) +
          " sid=" + (ace_sid ? FormatSid(ace_sid) : std::string("unknown"));
      return true;
    }
    if ((header->AceFlags & (INHERITED_ACE | INHERIT_ONLY_ACE |
                             NO_PROPAGATE_INHERIT_ACE)) == 0 &&
        (mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS) {
      for (size_t index = 0; index < trusted.size(); ++index) {
        if (EqualSid(ace_sid, trusted[index].get())) {
          explicit_full_access[index] = true;
        }
      }
    }
  }

  out->private_access = true;
  SECURITY_DESCRIPTOR_CONTROL control{};
  DWORD revision = 0;
  const bool dacl_protected =
      GetSecurityDescriptorControl(descriptor, &control, &revision) &&
      (control & SE_DACL_PROTECTED) != 0;
  bool all_trustees_covered = true;
  for (bool covered : explicit_full_access) {
    if (!covered) {
      all_trustees_covered = false;
      break;
    }
  }
  if (!dacl_protected) {
    out->diagnostic = "DACL inheritance protection missing";
    return true;
  }
  if (!all_trustees_covered) {
    out->diagnostic = "DACL missing full access for required principal";
    return true;
  }
  out->canonical_complete = true;
  return true;
}

bool ApplyProtectedDaclToHandle(HANDLE handle, const std::wstring& sddl,
                                std::string* diagnostic) {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
    if (diagnostic) {
      *diagnostic = "invalid security descriptor SDDL (Win32=" +
                    std::to_string(GetLastError()) + ")";
    }
    return false;
  }
  LocalBuffer guard(descriptor, LocalFree);
  PACL acl = nullptr;
  BOOL present = FALSE;
  BOOL defaulted = FALSE;
  if (!GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) ||
      !present || !acl) {
    if (diagnostic) *diagnostic = "security descriptor missing DACL";
    return false;
  }
  // SetSecurityInfo(SE_FILE_OBJECT) propagates inheritable ACEs to existing
  // children. Set only this directory's descriptor, leaving the original
  // security information on every child available for independent validation.
  const NTSTATUS status = NtSetSecurityObject(
      handle, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
      descriptor);
  if (status < 0) {
    if (diagnostic) {
      *diagnostic = "Cannot set directory DACL (Win32=" +
                    std::to_string(RtlNtStatusToDosError(status)) + ")";
    }
    return false;
  }
  return true;
}

bool CreateDirectoryIfMissing(const std::filesystem::path& root,
                              const std::wstring& directory_sddl,
                              std::string* diagnostic) {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          directory_sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
    if (diagnostic) {
      *diagnostic = "invalid directory security descriptor (Win32=" +
                    std::to_string(GetLastError()) + ")";
    }
    return false;
  }
  LocalBuffer guard(descriptor, LocalFree);
  SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor,
                                 FALSE};
  const bool created = CreateDirectoryW(root.c_str(), &attributes) != FALSE;
  const DWORD error = created ? ERROR_SUCCESS : GetLastError();
  if (!created && error != ERROR_ALREADY_EXISTS) {
    if (diagnostic) {
      *diagnostic =
          "CreateDirectoryW failed (Win32=" + std::to_string(error) + ")";
    }
    return false;
  }
  return true;
}

void DiagnoseUnwritableDirectory(const std::filesystem::path& root,
                                 DWORD open_error,
                                 const std::vector<LocalBuffer>& trusted,
                                 std::string* diagnostic) {
  if (!diagnostic) return;
  ScopedHandle inspect{CreateFileW(
      root.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
      nullptr)};
  if (inspect.value == INVALID_HANDLE_VALUE) {
    inspect.Reset(CreateFileW(
        root.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  }
  if (inspect.value != INVALID_HANDLE_VALUE) {
    std::string reason;
    if (!VerifyHandleNotReparse(inspect.value, true, nullptr, &reason)) {
      *diagnostic = reason;
      return;
    }
    SecurityEvaluation eval{};
    if (EvaluateHandleSecurity(inspect.value, true, trusted, &eval) &&
        !eval.diagnostic.empty()) {
      *diagnostic = eval.diagnostic + " (Win32=" + std::to_string(open_error) + ")";
      return;
    }
  }
  *diagnostic = "cannot open directory for DACL repair (Win32=" +
                std::to_string(open_error) + ")";
}

}  // namespace

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

namespace unattended_detail {

const SecurityPolicy& DefaultMachineSecurityPolicy() {
  static const SecurityPolicy kPolicy{
      L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",
      L"O:SYG:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)",
      L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",
      {L"S-1-5-18", L"S-1-5-32-544"},
  };
  return kPolicy;
}

bool PreparePrivateDirectory(const std::filesystem::path& root, bool create,
                             const SecurityPolicy& policy,
                             std::string* diagnostic) {
  if (diagnostic) diagnostic->clear();
  if (root.empty()) {
    if (diagnostic) *diagnostic = "empty directory path";
    return false;
  }
  std::vector<LocalBuffer> trusted;
  if (!ParseTrustedSids(policy, &trusted)) {
    if (diagnostic) *diagnostic = "invalid security policy";
    return false;
  }
  if (!ParentAllowsSafeTarget(root, diagnostic)) return false;
  if (create && !CreateDirectoryIfMissing(root, policy.directory_sddl,
                                          diagnostic)) {
    return false;
  }
  ScopedHandle handle{CreateFileW(
      root.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
      nullptr)};
  if (handle.value == INVALID_HANDLE_VALUE) {
    const DWORD open_error = GetLastError();
    ScopedHandle attr_only{CreateFileW(
        root.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (attr_only.value != INVALID_HANDLE_VALUE) {
      std::string reason;
      if (!VerifyHandleNotReparse(attr_only.value, true, nullptr, &reason)) {
        if (diagnostic) *diagnostic = reason;
        return false;
      }
    }
    if (diagnostic) {
      *diagnostic =
          "cannot open directory (Win32=" + std::to_string(open_error) + ")";
    }
    return false;
  }
  if (!VerifyHandleNotReparse(handle.value, true, nullptr, diagnostic)) {
    return false;
  }
  SecurityEvaluation eval{};
  if (!EvaluateHandleSecurity(handle.value, true, trusted, &eval)) {
    if (diagnostic) *diagnostic = eval.diagnostic;
    return false;
  }
  if (!eval.owner_trusted || !eval.private_access) {
    if (diagnostic) *diagnostic = eval.diagnostic;
    return false;
  }
  return true;
}

bool PreparePrivateDirectory(const std::filesystem::path& root, bool create,
                             std::string* diagnostic) {
  return PreparePrivateDirectory(root, create, DefaultMachineSecurityPolicy(),
                                 diagnostic);
}

bool RepairPrivateDirectory(const std::filesystem::path& root, bool create,
                            const SecurityPolicy& policy,
                            std::string* diagnostic, bool* repaired) {
  if (diagnostic) diagnostic->clear();
  if (repaired) *repaired = false;
  if (root.empty()) {
    if (diagnostic) *diagnostic = "empty directory path";
    return false;
  }
  std::vector<LocalBuffer> trusted;
  if (!ParseTrustedSids(policy, &trusted)) {
    if (diagnostic) *diagnostic = "invalid security policy";
    return false;
  }
  if (!ParentAllowsSafeTarget(root, diagnostic)) return false;
  if (create && !CreateDirectoryIfMissing(root, policy.directory_sddl,
                                          diagnostic)) {
    return false;
  }

  // Pin the verified directory while reading and updating its DACL.
  ScopedHandle directory;
  for (unsigned attempt = 0;; ++attempt) {
    directory.Reset(CreateFileW(
        root.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL | WRITE_DAC,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (directory.value != INVALID_HANDLE_VALUE) break;
    const DWORD code = GetLastError();
    if (code != ERROR_SHARING_VIOLATION || attempt == 3) {
      DiagnoseUnwritableDirectory(root, code, trusted, diagnostic);
      return false;
    }
    Sleep(250u << attempt);
  }
  if (!VerifyHandleNotReparse(directory.value, true, nullptr, diagnostic)) {
    return false;
  }
  SecurityEvaluation eval{};
  if (!EvaluateHandleSecurity(directory.value, true, trusted, &eval)) {
    if (diagnostic) *diagnostic = eval.diagnostic;
    return false;
  }
  if (!eval.owner_trusted) {
    if (diagnostic) *diagnostic = eval.diagnostic;
    return false;
  }

  if (eval.canonical_complete) return true;
  std::string apply_error;
  if (!ApplyProtectedDaclToHandle(directory.value, policy.directory_sddl,
                                  &apply_error)) {
    if (diagnostic) *diagnostic = eval.diagnostic + "; " + apply_error;
    return false;
  }

  SecurityEvaluation verified{};
  if (!EvaluateHandleSecurity(directory.value, true, trusted, &verified) ||
      !verified.owner_trusted || !verified.private_access ||
      !verified.canonical_complete) {
    if (diagnostic) {
      *diagnostic = verified.diagnostic.empty()
                        ? "post-repair directory verification failed"
                        : verified.diagnostic;
    }
    return false;
  }

  if (repaired) *repaired = true;
  if (diagnostic) *diagnostic = eval.diagnostic;
  return true;
}

bool RepairPrivateDirectory(const std::filesystem::path& root, bool create,
                            std::string* diagnostic, bool* repaired) {
  return RepairPrivateDirectory(root, create, DefaultMachineSecurityPolicy(),
                                diagnostic, repaired);
}

HANDLE OpenPrivateMachineFile(const std::filesystem::path& root,
                              const std::filesystem::path& path, DWORD access,
                              DWORD sharing, DWORD creation, DWORD flags,
                              const SecurityPolicy& policy,
                              std::string* diagnostic) {
  if (diagnostic) diagnostic->clear();
  std::vector<LocalBuffer> trusted;
  if (path.parent_path() != root || !ParseTrustedSids(policy, &trusted) ||
      !PreparePrivateDirectory(root, false, policy, diagnostic)) {
    SetLastError(ERROR_ACCESS_DENIED);
    return INVALID_HANDLE_VALUE;
  }
  PSECURITY_DESCRIPTOR security = nullptr;
  const auto& sddl =
      IsLocalSystemProcess() ? policy.system_file_sddl : policy.admin_file_sddl;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          sddl.c_str(), SDDL_REVISION_1, &security, nullptr)) {
    return INVALID_HANDLE_VALUE;
  }
  SECURITY_ATTRIBUTES attributes{sizeof(attributes), security, FALSE};
  HANDLE file = CreateFileW(
      path.c_str(), access | FILE_READ_ATTRIBUTES | READ_CONTROL, sharing,
      &attributes, creation, flags | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  const DWORD error = GetLastError();
  LocalFree(security);
  if (file == INVALID_HANDLE_VALUE) {
    SetLastError(error);
    return file;
  }
  SecurityEvaluation eval{};
  if (VerifyHandleNotReparse(file, false, nullptr, diagnostic)) {
    if (EvaluateHandleSecurity(file, false, trusted, &eval) &&
        eval.owner_trusted && eval.private_access) return file;
    if (diagnostic) *diagnostic = eval.diagnostic;
  }
  CloseHandle(file);
  SetLastError(ERROR_ACCESS_DENIED);
  return INVALID_HANDLE_VALUE;
}

HANDLE OpenPrivateMachineFile(const std::filesystem::path& root,
                              const std::filesystem::path& path, DWORD access,
                              DWORD sharing, DWORD creation, DWORD flags,
                              std::string* diagnostic) {
  return OpenPrivateMachineFile(root, path, access, sharing, creation, flags,
                                DefaultMachineSecurityPolicy(), diagnostic);
}

bool WritePrivateMachineFile(const std::filesystem::path& root,
                             const std::filesystem::path& path,
                             const std::string& contents,
                             const SecurityPolicy& policy) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES &&
      (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
    SetLastError(ERROR_ACCESS_DENIED);
    return false;
  }
  static std::atomic<unsigned long> sequence{0};
  const auto temporary = path.wstring() + L".tmp-" +
                         std::to_wstring(GetCurrentProcessId()) + L"-" +
                         std::to_wstring(GetTickCount64()) + L"-" +
                         std::to_wstring(sequence++);
  HANDLE output = OpenPrivateMachineFile(root, temporary, GENERIC_WRITE, 0,
                                         CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                         policy);
  if (output == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool saved =
      WriteFile(output, contents.data(), static_cast<DWORD>(contents.size()),
                &written, nullptr) &&
      written == contents.size() && FlushFileBuffers(output);
  DWORD error = saved ? ERROR_SUCCESS : GetLastError();
  CloseHandle(output);
  if (saved) {
    error = private_file::ReplaceTemporaryFile(temporary, path);
    if (error == ERROR_SUCCESS) return true;
  }
  DeleteFileW(temporary.c_str());
  SetLastError(error ? error : ERROR_WRITE_FAULT);
  return false;
}

bool WritePrivateMachineFile(const std::filesystem::path& root,
                             const std::filesystem::path& path,
                             const std::string& contents) {
  return WritePrivateMachineFile(root, path, contents,
                                 DefaultMachineSecurityPolicy());
}

}  // namespace unattended_detail
}  // namespace crossdesk
