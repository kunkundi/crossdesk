#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <Windows.h>
#include <Aclapi.h>
#include <sddl.h>
#include <winioctl.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "unattended_security.h"

namespace {

using LocalBuffer = std::unique_ptr<void, decltype(&LocalFree)>;

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

struct MountPointReparseBuffer {
  ULONG ReparseTag;
  USHORT ReparseDataLength;
  USHORT Reserved;
  USHORT SubstituteNameOffset;
  USHORT SubstituteNameLength;
  USHORT PrintNameOffset;
  USHORT PrintNameLength;
  WCHAR PathBuffer[4096];
};

bool Check(bool condition, const char* message,
           const std::string& detail = {}) {
  if (condition) return true;
  std::cerr << "FAIL: " << message;
  if (!detail.empty()) std::cerr << " (" << detail << ")";
  std::cerr << ", Win32=" << GetLastError() << '\n';
  return false;
}

std::wstring CurrentUserSidString() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<BYTE> data(size);
  const BOOL ok =
      GetTokenInformation(token, TokenUser, data.data(), size, &size);
  CloseHandle(token);
  if (!ok) return {};
  LPWSTR sid_text = nullptr;
  if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,
                              &sid_text)) {
    return {};
  }
  LocalBuffer guard(sid_text, LocalFree);
  return sid_text;
}

bool ApplyDaclSddl(const std::filesystem::path& path, const std::wstring& sddl,
                   DWORD extra_flags = PROTECTED_DACL_SECURITY_INFORMATION) {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
    return false;
  }
  LocalBuffer guard(descriptor, LocalFree);
  PACL dacl = nullptr;
  BOOL present = FALSE;
  BOOL defaulted = FALSE;
  if (!GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) ||
      !present) {
    return false;
  }
  ScopedHandle handle{CreateFileW(
      path.c_str(), READ_CONTROL | WRITE_DAC,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
      nullptr)};
  if (handle.value == INVALID_HANDLE_VALUE) return false;
  return SetSecurityInfo(handle.value, SE_FILE_OBJECT,
                         DACL_SECURITY_INFORMATION | extra_flags, nullptr,
                         nullptr, dacl, nullptr) == ERROR_SUCCESS;
}

bool HasSidInDacl(const std::filesystem::path& path,
                  const wchar_t* expected_sid_sddl,
                  bool* dacl_protected_out = nullptr) {
  PSID target_sid = nullptr;
  if (!ConvertStringSidToSidW(expected_sid_sddl, &target_sid)) return false;
  LocalBuffer sid_guard(target_sid, LocalFree);

  ScopedHandle handle{CreateFileW(
      path.c_str(), READ_CONTROL,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
      nullptr)};
  if (handle.value == INVALID_HANDLE_VALUE) return false;
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (GetSecurityInfo(handle.value, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                      nullptr, nullptr, &dacl, nullptr,
                      &descriptor) != ERROR_SUCCESS) {
    return false;
  }
  LocalBuffer sd_guard(descriptor, LocalFree);
  if (dacl_protected_out) {
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    *dacl_protected_out =
        GetSecurityDescriptorControl(descriptor, &control, &revision) &&
        (control & SE_DACL_PROTECTED) != 0;
  }
  if (!dacl) return false;
  for (DWORD i = 0; i < dacl->AceCount; ++i) {
    void* raw = nullptr;
    if (!GetAce(dacl, i, &raw)) continue;
    const auto* header = static_cast<const ACE_HEADER*>(raw);
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) continue;
    const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
    if (EqualSid(const_cast<DWORD*>(&ace->SidStart), target_sid)) return true;
  }
  return false;
}

bool CreateDirectoryJunction(const std::filesystem::path& junction_path,
                             const std::filesystem::path& target_path) {
  if (!CreateDirectoryW(junction_path.c_str(), nullptr)) return false;
  ScopedHandle handle{CreateFileW(
      junction_path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
  if (handle.value == INVALID_HANDLE_VALUE) {
    RemoveDirectoryW(junction_path.c_str());
    return false;
  }

  const auto canonical_target = std::filesystem::canonical(target_path).wstring();
  const std::wstring substitute = L"\\??\\" + canonical_target;
  const std::wstring& print = canonical_target;
  if (substitute.size() + print.size() + 4 >= 4096) {
    RemoveDirectoryW(junction_path.c_str());
    return false;
  }

  MountPointReparseBuffer reparse{};
  reparse.ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
  reparse.SubstituteNameOffset = 0;
  reparse.SubstituteNameLength =
      static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
  reparse.PrintNameOffset =
      static_cast<USHORT>(reparse.SubstituteNameLength + sizeof(wchar_t));
  reparse.PrintNameLength = static_cast<USHORT>(print.size() * sizeof(wchar_t));
  std::memcpy(reparse.PathBuffer, substitute.c_str(),
              reparse.SubstituteNameLength + sizeof(wchar_t));
  std::memcpy(reinterpret_cast<BYTE*>(reparse.PathBuffer) +
                  reparse.PrintNameOffset,
              print.c_str(), reparse.PrintNameLength + sizeof(wchar_t));
  reparse.ReparseDataLength = static_cast<USHORT>(
      4 * sizeof(USHORT) + reparse.SubstituteNameLength + sizeof(wchar_t) +
      reparse.PrintNameLength + sizeof(wchar_t));

  const DWORD total_bytes = static_cast<DWORD>(
      offsetof(MountPointReparseBuffer, SubstituteNameOffset) +
      reparse.ReparseDataLength);
  DWORD returned = 0;
  if (!DeviceIoControl(handle.value, FSCTL_SET_REPARSE_POINT, &reparse,
                       total_bytes, nullptr, 0, &returned, nullptr)) {
    handle.Reset();
    RemoveDirectoryW(junction_path.c_str());
    return false;
  }
  return true;
}

std::string ReadMachineFile(
    const std::filesystem::path& root, const std::filesystem::path& path,
    const crossdesk::unattended_detail::SecurityPolicy& policy) {
  ScopedHandle handle{crossdesk::unattended_detail::OpenPrivateMachineFile(
      root, path, GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, policy)};
  if (handle.value == INVALID_HANDLE_VALUE) return {};
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(handle.value, &size) || size.QuadPart < 0 ||
      size.QuadPart > 65536) {
    return {};
  }
  std::string buffer(static_cast<size_t>(size.QuadPart), '\0');
  DWORD read = 0;
  if (!ReadFile(handle.value, buffer.data(), static_cast<DWORD>(buffer.size()),
                &read, nullptr) ||
      read != buffer.size()) {
    return {};
  }
  return buffer;
}

}  // namespace

int main() {
  using crossdesk::unattended_detail::DefaultMachineSecurityPolicy;
  using crossdesk::unattended_detail::OpenPrivateMachineFile;
  using crossdesk::unattended_detail::PreparePrivateDirectory;
  using crossdesk::unattended_detail::RepairPrivateDirectory;
  using crossdesk::unattended_detail::SecurityPolicy;
  using crossdesk::unattended_detail::WritePrivateMachineFile;

  const std::wstring user_sid = CurrentUserSidString();
  if (!Check(!user_sid.empty(), "query current user SID")) return 1;

  const bool elevated = crossdesk::IsAdministratorProcess();
  const std::wstring admin_like_sid = elevated ? L"S-1-5-32-544" : user_sid;
  const std::wstring owner_prefix =
      elevated ? L"O:BAG:BA" : (L"O:" + user_sid);
  const SecurityPolicy policy =
      elevated
          ? DefaultMachineSecurityPolicy()
          : SecurityPolicy{
                owner_prefix + L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" +
                    admin_like_sid + L")",
                L"O:SYG:SYD:P(A;;FA;;;SY)(A;;FA;;;" + admin_like_sid + L")",
                owner_prefix + L"D:P(A;;FA;;;SY)(A;;FA;;;" + admin_like_sid +
                    L")",
                {L"S-1-5-18", admin_like_sid},
            };

  const auto workspace =
      std::filesystem::temp_directory_path() /
      (L"crossdesk-dacl-recovery-test-" +
       std::to_wstring(GetCurrentProcessId()) + L"-" +
       std::to_wstring(GetTickCount64()));
  struct WorkspaceCleanup {
    std::filesystem::path dir;
    std::vector<std::filesystem::path> junctions;
    ~WorkspaceCleanup() {
      for (auto it = junctions.rbegin(); it != junctions.rend(); ++it) {
        RemoveDirectoryW(it->c_str());
      }
      if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
      }
    }
  } cleanup;

  if (std::filesystem::exists(workspace) ||
      !CreateDirectoryW(workspace.c_str(), nullptr)) {
    std::cerr << "FAIL: create isolated test workspace\n";
    return 1;
  }
  cleanup.dir = workspace;

  const auto root = workspace / L"CrossDeskUnattended";
  std::string diagnostic;
  bool repaired = false;

  // 1. Create private directory, populate identity files and hold an exclusive
  // lease handle (dwShareMode = 0) just like the running SYSTEM host.
  if (!Check(PreparePrivateDirectory(root, true, policy, &diagnostic),
             "create initial private directory", diagnostic)) {
    return 1;
  }
  const std::string snapshot_payload =
      R"({"version":1,"device_id":"123456789","host_config":"[Settings]\n"})";
  const auto snapshot_path = root / L"shared-identity.json";
  const auto owner_path = root / L"gui-owner.json";
  if (!Check(WritePrivateMachineFile(root, snapshot_path, snapshot_payload,
                                     policy),
             "write shared-identity.json") ||
      !Check(WritePrivateMachineFile(root, owner_path, R"({"pid":42})", policy),
             "write gui-owner.json")) {
    return 1;
  }
  ScopedHandle lease{OpenPrivateMachineFile(
      root, root / L"identity-owner.lock", GENERIC_READ | GENERIC_WRITE, 0,
      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, policy)};
  if (!Check(lease.value != INVALID_HANDLE_VALUE,
             "acquire exclusive identity-owner.lock")) {
    return 1;
  }

  // 2. Poison both the directory and shared-identity.json with an extra World
  // (S-1-1-0) full-access ACE, reproducing Issue #277 + recursive icacls /T.
  const std::wstring poisoned_dir_sddl =
      L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + admin_like_sid +
      L")(A;OICI;FA;;;WD)";
  const std::wstring poisoned_file_sddl =
      L"D:P(A;;FA;;;SY)(A;;FA;;;" + admin_like_sid + L")(A;;FA;;;WD)";
  if (!Check(ApplyDaclSddl(root, poisoned_dir_sddl),
             "poison directory DACL with World ACE") ||
      !Check(ApplyDaclSddl(snapshot_path, poisoned_file_sddl),
             "poison shared-identity.json DACL with World ACE")) {
    return 1;
  }

  if (!Check(!PreparePrivateDirectory(root, false, policy, &diagnostic),
             "PreparePrivateDirectory(false) rejects poisoned DACL") ||
      !Check(diagnostic.find("S-1-1-0") != std::string::npos,
             "diagnostic identifies offending S-1-1-0 ACE", diagnostic) ||
      !Check(!PreparePrivateDirectory(root, true, policy, &diagnostic),
             "PreparePrivateDirectory(true) never silently adopts")) {
    return 1;
  }
  {
    ScopedHandle rejected{OpenPrivateMachineFile(
        root, snapshot_path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, policy)};
    if (!Check(rejected.value == INVALID_HANDLE_VALUE &&
                   GetLastError() == ERROR_ACCESS_DENIED,
               "OpenPrivateMachineFile rejects poisoned directory/file")) {
      return 1;
    }
  }

  // 3. RepairPrivateDirectory heals both the directory and child identity file
  // while preserving file contents and tolerating the active lock handle.
  if (!Check(RepairPrivateDirectory(root, true, policy, &diagnostic, &repaired),
             "RepairPrivateDirectory heals poisoned DACL", diagnostic) ||
      !Check(repaired, "RepairPrivateDirectory reports repaired=true") ||
      !Check(diagnostic.find("S-1-1-0") != std::string::npos,
             "repair summary records offending S-1-1-0 ACE", diagnostic)) {
    return 1;
  }
  bool dir_protected = false;
  bool file_protected = false;
  if (!Check(!HasSidInDacl(root, L"S-1-1-0", &dir_protected) && dir_protected,
             "directory World ACE removed and SE_DACL_PROTECTED set") ||
      !Check(!HasSidInDacl(snapshot_path, L"S-1-1-0", &file_protected) &&
                 file_protected,
             "child file World ACE removed and SE_DACL_PROTECTED set") ||
      !Check(PreparePrivateDirectory(root, false, policy, &diagnostic),
             "PreparePrivateDirectory succeeds after repair", diagnostic) ||
      !Check(ReadMachineFile(root, snapshot_path, policy) == snapshot_payload,
             "shared-identity.json contents preserved after repair")) {
    return 1;
  }

  // Idempotent second repair must be a no-op (repaired == false).
  if (!Check(
          RepairPrivateDirectory(root, false, policy, &diagnostic, &repaired) &&
              !repaired,
          "idempotent RepairPrivateDirectory reports repaired=false",
          diagnostic)) {
    return 1;
  }

  // 4. Stripped SE_DACL_PROTECTED (re-enabled inheritance) is detected and
  // restored to a protected canonical DACL.
  const std::wstring unprotected_sddl =
      L"D:(A;OICI;FA;;;SY)(A;OICI;FA;;;" + admin_like_sid + L")";
  if (!Check(ApplyDaclSddl(root, unprotected_sddl,
                           UNPROTECTED_DACL_SECURITY_INFORMATION),
             "strip SE_DACL_PROTECTED on directory") ||
      !Check(
          RepairPrivateDirectory(root, false, policy, &diagnostic, &repaired) &&
              repaired,
          "RepairPrivateDirectory restores SE_DACL_PROTECTED", diagnostic) ||
      !Check(!HasSidInDacl(root, L"S-1-1-0", &dir_protected) && dir_protected,
             "SE_DACL_PROTECTED restored after repair")) {
    return 1;
  }

  // 5. Incomplete single-principal DACL (missing SYSTEM ACE) is upgraded to
  // grant full access to all required principals in the policy.
  const std::wstring incomplete_sddl =
      L"D:P(A;OICI;FA;;;" + admin_like_sid + L")";
  if (!Check(ApplyDaclSddl(root, incomplete_sddl),
             "apply single-principal DACL") ||
      !Check(!HasSidInDacl(root, L"S-1-5-18"),
             "SYSTEM ACE absent before repair") ||
      !Check(
          RepairPrivateDirectory(root, false, policy, &diagnostic, &repaired) &&
              repaired,
          "RepairPrivateDirectory restores missing SYSTEM ACE", diagnostic) ||
      !Check(HasSidInDacl(root, L"S-1-5-18") &&
                 HasSidInDacl(root, admin_like_sid.c_str()),
             "both SYSTEM and admin principals restored")) {
    return 1;
  }

  // 6. Reparse-point / NTFS directory junction protection: never follow a
  // junction or modify the target directory's DACL (zero TOCTOU / escalation).
  const auto victim = workspace / L"victim-target";
  const auto junction = workspace / L"junction-attack";
  if (!Check(CreateDirectoryW(victim.c_str(), nullptr) != FALSE,
             "create victim directory") ||
      !Check(ApplyDaclSddl(victim,
                           L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" +
                               admin_like_sid + L")(A;OICI;GR;;;WD)"),
             "set sentinel World read ACE on victim directory") ||
      !Check(CreateDirectoryJunction(junction, victim),
             "create NTFS directory junction to victim")) {
    return 1;
  }
  cleanup.junctions.push_back(junction);

  if (!Check(!PreparePrivateDirectory(junction, false, policy, &diagnostic),
             "PreparePrivateDirectory rejects NTFS junction") ||
      !Check(diagnostic.find("reparse point") != std::string::npos,
             "PreparePrivateDirectory reports reparse point", diagnostic) ||
      !Check(!RepairPrivateDirectory(junction, false, policy, &diagnostic,
                                     &repaired) &&
                 !repaired,
             "RepairPrivateDirectory(false) refuses NTFS junction") ||
      !Check(diagnostic.find("reparse point") != std::string::npos,
             "RepairPrivateDirectory reports reparse point", diagnostic) ||
      !Check(!RepairPrivateDirectory(junction, true, policy, &diagnostic,
                                     &repaired) &&
                 !repaired,
             "RepairPrivateDirectory(true) refuses NTFS junction") ||
      !Check(HasSidInDacl(victim, L"S-1-1-0"),
             "victim directory DACL untouched after junction refusal")) {
    return 1;
  }

  // 7. Parent directory junction refusal: never traverse a reparse parent.
  const auto through_junction = junction / L"CrossDeskUnattended";
  if (!Check(!PreparePrivateDirectory(through_junction, true, policy,
                                      &diagnostic),
             "PreparePrivateDirectory rejects reparse parent") ||
      !Check(!RepairPrivateDirectory(through_junction, true, policy,
                                     &diagnostic, &repaired),
             "RepairPrivateDirectory rejects reparse parent") ||
      !Check(diagnostic.find("parent directory reparse point") !=
                 std::string::npos,
             "diagnostic reports parent reparse point", diagnostic) ||
      !Check(!std::filesystem::exists(victim / L"CrossDeskUnattended"),
             "target directory under junction was never created")) {
    return 1;
  }

  // 8. Untrusted owner refusal: never repair/adopt a directory owned by a
  // principal outside policy.trusted_sids.
  const auto untrusted_dir = workspace / L"untrusted-owner";
  const SecurityPolicy system_only_policy{
      L"O:SYG:SYD:P(A;OICI;FA;;;SY)",
      L"O:SYG:SYD:P(A;;FA;;;SY)",
      L"O:SYG:SYD:P(A;;FA;;;SY)",
      {L"S-1-5-18"},
  };
  if (!Check(CreateDirectoryW(untrusted_dir.c_str(), nullptr) != FALSE,
             "create untrusted-owner directory") ||
      !Check(ApplyDaclSddl(untrusted_dir,
                           L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" +
                               admin_like_sid + L")(A;OICI;GR;;;WD)"),
             "seed sentinel World ACE on untrusted-owner directory") ||
      !Check(!RepairPrivateDirectory(untrusted_dir, false, system_only_policy,
                                     &diagnostic, &repaired) &&
                 !repaired,
             "RepairPrivateDirectory refuses untrusted owner") ||
      !Check(diagnostic.find("untrusted owner") != std::string::npos,
             "diagnostic reports untrusted owner", diagnostic) ||
      !Check(HasSidInDacl(untrusted_dir, L"S-1-1-0"),
             "untrusted-owner directory DACL left untouched")) {
    return 1;
  }
  if (!elevated) {
    if (!Check(!RepairPrivateDirectory(untrusted_dir, false, &diagnostic,
                                       &repaired) &&
                   !repaired,
               "production policy refuses ordinary-user-owned directory") ||
        !Check(diagnostic.find("untrusted owner") != std::string::npos,
               "production policy reports untrusted owner", diagnostic)) {
      return 1;
    }
  }

  // 9. Hardlinked child files and child junctions inside root are never
  // modified during directory repair and remain rejected by OpenPrivateMachineFile.
  const auto outside_file = workspace / L"outside-target.txt";
  {
    std::ofstream out(outside_file);
    out << "outside content";
  }
  const auto hardlink_in_root = root / L"hardlinked.json";
  const auto child_junction = root / L"child-junction";
  if (!Check(ApplyDaclSddl(outside_file,
                           L"D:P(A;;FA;;;SY)(A;;FA;;;" + admin_like_sid +
                               L")(A;;GR;;;WD)"),
             "seed World read ACE on outside file") ||
      !Check(CreateHardLinkW(hardlink_in_root.c_str(), outside_file.c_str(),
                             nullptr) != FALSE,
             "create hardlink inside root") ||
      !Check(CreateDirectoryJunction(child_junction, victim),
             "create child junction inside root")) {
    return 1;
  }
  cleanup.junctions.push_back(child_junction);

  if (!Check(RepairPrivateDirectory(root, false, policy, &diagnostic, &repaired),
             "RepairPrivateDirectory succeeds with hardlink/junction children",
             diagnostic) ||
      !Check(HasSidInDacl(outside_file, L"S-1-1-0"),
             "hardlinked outside file DACL never modified") ||
      !Check(HasSidInDacl(victim, L"S-1-1-0"),
             "child junction target DACL never modified")) {
    return 1;
  }
  {
    ScopedHandle hardlink_handle{OpenPrivateMachineFile(
        root, hardlink_in_root, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, policy)};
    if (!Check(hardlink_handle.value == INVALID_HANDLE_VALUE &&
                   GetLastError() == ERROR_ACCESS_DENIED,
               "OpenPrivateMachineFile rejects hardlinked file")) {
      return 1;
    }
  }

  // 10. When elevated, exercise the zero-policy production overloads directly.
  if (elevated) {
    const auto prod_root = workspace / L"prod-default";
    if (!Check(PreparePrivateDirectory(prod_root, true, &diagnostic),
               "create production default directory", diagnostic) ||
        !Check(ApplyDaclSddl(
                   prod_root,
                   L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;WD)"),
               "poison production default directory") ||
        !Check(!PreparePrivateDirectory(prod_root, false, &diagnostic),
               "production PreparePrivateDirectory rejects World ACE") ||
        !Check(
            RepairPrivateDirectory(prod_root, true, &diagnostic, &repaired) &&
                repaired,
            "production RepairPrivateDirectory heals World ACE", diagnostic) ||
        !Check(PreparePrivateDirectory(prod_root, false, &diagnostic),
               "production PreparePrivateDirectory succeeds after repair",
               diagnostic)) {
      return 1;
    }
  }

  lease.Reset();
  std::cout << "Unattended DACL recovery and reparse-point safety checks passed\n";
  return 0;
}
