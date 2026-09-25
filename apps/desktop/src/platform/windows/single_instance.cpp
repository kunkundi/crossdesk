#include "platform/single_instance.h"

#include <windows.h>
#include <sddl.h>

#include <utility>
#include <vector>

namespace crossdesk::platform {

SingleInstanceGuard::SingleInstanceGuard(std::string application)
    : application_(std::move(application)) {}

SingleInstanceGuard::~SingleInstanceGuard() { Release(); }

InstanceResult SingleInstanceGuard::TryAcquire(InstanceRole role,
                                               std::string& error) {
  error.clear();
  if (handle_ != -1) return InstanceResult::kAcquired;
  if (!ValidInstanceApplication(application_)) {
    error = "Invalid single-instance application name";
    return InstanceResult::kError;
  }
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    error = "Cannot read current user token: " + std::to_string(GetLastError());
    return InstanceResult::kError;
  }
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<unsigned char> buffer(size);
  if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
    const DWORD code = GetLastError();
    CloseHandle(token);
    error = "Cannot read current user SID: " + std::to_string(code);
    return InstanceResult::kError;
  }
  CloseHandle(token);
  wchar_t* sid = nullptr;
  if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,
                             &sid)) {
    error = "Cannot format current user SID: " + std::to_string(GetLastError());
    return InstanceResult::kError;
  }
  const std::string role_name = InstanceRoleName(role);
  const std::wstring name = L"Global\\" +
      std::wstring(application_.begin(), application_.end()) + L"." + sid + L"." +
      std::wstring(role_name.begin(), role_name.end());
  LocalFree(sid);
  // Object lifetime is the lease. No thread-owned mutex to transfer to a daemon.
  HANDLE mutex = CreateMutexW(nullptr, FALSE, name.c_str());
  const DWORD code = GetLastError();
  if (!mutex) {
    error = "Cannot create single-instance mutex: " + std::to_string(code);
    return InstanceResult::kError;
  }
  if (code == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    return InstanceResult::kAlreadyRunning;
  }
  handle_ = reinterpret_cast<std::intptr_t>(mutex);
  return InstanceResult::kAcquired;
}

void SingleInstanceGuard::Release() {
  if (handle_ == -1) return;
  CloseHandle(reinterpret_cast<HANDLE>(handle_));
  handle_ = -1;
}

}  // namespace crossdesk::platform
