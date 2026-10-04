#include <Windows.h>
#include <Aclapi.h>
#include <sddl.h>

#include <filesystem>
#include <fstream>
#include <iostream>

#include "unattended_config.h"

namespace {
bool Check(bool condition, const char* message) {
  if (!condition) std::cerr << message << ", Win32=" << GetLastError() << '\n';
  return condition;
}
}  // namespace

int main() {
  using crossdesk::unattended_detail::PreparePrivateDirectory;
  if (!Check(crossdesk::IsAdministratorProcess(), "Run this test elevated"))
    return 1;
  const auto root =
      std::filesystem::temp_directory_path() /
      (L"crossdesk-unattended-test-" + std::to_wstring(GetCurrentProcessId()));
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }
  } cleanup{root};
  // Never reuse/remove a directory we did not create for this test.
  if (std::filesystem::exists(root)) {
    cleanup.path.clear();
    return 1;
  }
  if (!Check(PreparePrivateDirectory(root, true), "create private profile") ||
      !Check(PreparePrivateDirectory(root, true),
             "idempotent profile validation"))
    return 1;
  const auto secret = root / L"credential.txt";
  {
    std::ofstream output(secret);
    output << "test credential";
  }

  HANDLE token = nullptr, restricted = nullptr;
  BYTE admin_sid[SECURITY_MAX_SID_SIZE];
  DWORD sid_size = sizeof(admin_sid);
  if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admin_sid,
                          &sid_size) ||
      !OpenProcessToken(GetCurrentProcess(),
                        TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY |
                            TOKEN_IMPERSONATE,
                        &token))
    return 1;
  SID_AND_ATTRIBUTES disabled{admin_sid, 0};
  const bool restricted_ok =
      CreateRestrictedToken(token, DISABLE_MAX_PRIVILEGE, 1, &disabled, 0,
                            nullptr, 0, nullptr, &restricted) != FALSE;
  CloseHandle(token);
  if (!Check(restricted_ok, "create unprivileged token")) return 1;
  const bool impersonated = ImpersonateLoggedOnUser(restricted) != FALSE;
  bool denied = false;
  if (impersonated) {
    HANDLE file = CreateFileW(secret.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    denied =
        file == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!RevertToSelf()) std::terminate();
  }
  CloseHandle(restricted);
  if (!Check(impersonated && denied,
             "ordinary token must not read credentials"))
    return 1;

  const auto link = root / L"link";
  if (!Check(CreateSymbolicLinkW(link.c_str(), root.c_str(),
                                 SYMBOLIC_LINK_FLAG_DIRECTORY) != FALSE,
             "create directory link") ||
      !Check(!PreparePrivateDirectory(link, false),
             "reject directory reparse points"))
    return 1;
  std::filesystem::remove(link);

  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GR;;;WD)",
          SDDL_REVISION_1, &descriptor, nullptr))
    return 1;
  BOOL present = FALSE, defaulted = FALSE;
  PACL dacl = nullptr;
  GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted);
  const auto result = SetNamedSecurityInfoW(
      const_cast<wchar_t*>(root.c_str()), SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
      nullptr, dacl, nullptr);
  LocalFree(descriptor);
  if (!Check(result == ERROR_SUCCESS, "make profile permissive") ||
      !Check(!PreparePrivateDirectory(root, false),
             "reject exposed credentials") ||
      !Check(!PreparePrivateDirectory(root, true),
             "never silently adopt an unsafe profile"))
    return 1;
  std::cout << "Unattended profile ACL and reparse-point checks passed\n";
}
