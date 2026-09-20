/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-10
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#include "privacy_band_guard.h"

#include <objbase.h>

#include <algorithm>
#include <iterator>
#include <new>

#include "privacy_backend.h"
#include "rd_log.h"

namespace crossdesk {
namespace {
struct ScopedHandle {
  HANDLE value = nullptr;
  ~ScopedHandle() {
    if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
  }
};
bool Failed(const char* operation, std::string& error,
            DWORD code = GetLastError()) {
  error =
      std::string(operation) + " (Windows error " + std::to_string(code) + ")";
  LOG_ERROR("Privacy high-band: {}", error);
  return false;
}

std::filesystem::path ModuleDirectory() {
  std::vector<wchar_t> path(32768);
  const DWORD length = GetModuleFileNameW(nullptr, path.data(), path.size());
  if (!length || length >= path.size()) return {};
  return std::filesystem::path(path.data()).parent_path();
}

std::vector<wchar_t> ChildEnvironment(const std::wstring& bootstrap) {
  std::vector<std::wstring> entries;
  LPWCH environment = GetEnvironmentStringsW();
  if (!environment) return {};
  const std::wstring prefix = std::wstring(kPrivacyBandBootstrap) + L"=";
  for (const wchar_t* item = environment; *item; item += wcslen(item) + 1) {
    if (_wcsnicmp(item, prefix.c_str(), prefix.size()) != 0)
      entries.emplace_back(item);
  }
  FreeEnvironmentStringsW(environment);
  entries.push_back(prefix + bootstrap);
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
    return _wcsicmp(a.c_str(), b.c_str()) < 0;
  });
  std::vector<wchar_t> result;
  for (const auto& entry : entries) {
    result.insert(result.end(), entry.begin(), entry.end());
    result.push_back(0);
  }
  result.push_back(0);
  return result;
}
}  // namespace

PrivacyBandGuard::~PrivacyBandGuard() {
  std::string error;
  if (!Stop(error)) {
    // Last-resort teardown still targets only this object's dedicated job.
    if (job_) CloseHandle(job_);
    job_ = nullptr;
    Stop(error);
  }
}

bool PrivacyBandGuard::Available(std::string& error) {
#if !defined(_M_X64) && !defined(__x86_64__)
  error = "High-band privacy currently requires an x64 Windows process";
  return false;
#else
  const HMODULE user = GetModuleHandleW(L"user32.dll");
  if (!GetProcAddress(user, "CreateWindowInBand") ||
      !GetProcAddress(user, "GetWindowBand")) {
    error = "Windows high-band window APIs are unavailable";
    return false;
  }
  const auto directory = ModuleDirectory();
  if (directory.empty())
    return Failed("Resolve privacy module directory", error);
  const auto dll = directory / kPrivacyBandDll;
  if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES)
    return Failed("Missing crossdesk_privacy_window.dll", error);
  return true;
#endif
}

bool PrivacyBandGuard::Start(const PrivacyScreenText& text,
                             std::string& error) {
  if (process_)
    return Failed("High-band broker is already running", error, ERROR_BUSY);
  if (!Available(error)) return false;
  const auto directory = ModuleDirectory();
  if (directory.empty())
    return Failed("Resolve privacy module directory", error);
  const auto dll = directory / kPrivacyBandDll;
  wchar_t system[MAX_PATH]{}, temporary[MAX_PATH]{}, guid_text[40]{};
  GUID guid{};
  if (!GetSystemDirectoryW(system, MAX_PATH) ||
      !GetTempPathW(MAX_PATH, temporary) || FAILED(CoCreateGuid(&guid)) ||
      !StringFromGUID2(guid, guid_text, 40))
    return Failed("Resolve high-band broker paths", error);
  broker_directory_ = std::filesystem::path(temporary) /
                      (std::wstring(L"CrossDesk-privacy-") + guid_text);
  if (!CreateDirectoryW(broker_directory_.c_str(), nullptr))
    return Failed("Create private broker directory", error);
  broker_path_ = broker_directory_ / L"RuntimeBroker_crossdesk.exe";
  if (!CopyFileW((std::filesystem::path(system) / L"RuntimeBroker.exe").c_str(),
                 broker_path_.c_str(), TRUE))
    return Failed("Copy system RuntimeBroker for this session", error);
  // Keep both images immutable throughout creation/injection and the session.
  image_lock_ = CreateFileW(broker_path_.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, 0, nullptr);
  if (image_lock_ == INVALID_HANDLE_VALUE)
    return Failed("Lock privacy broker image", error);
  dll_lock_ = CreateFileW(dll.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                          OPEN_EXISTING, 0, nullptr);
  if (dll_lock_ == INVALID_HANDLE_VALUE)
    return Failed("Lock privacy window module", error);

  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  ScopedHandle parent;
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(),
                       GetCurrentProcess(), &parent.value,
                       SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE,
                       0))
    return Failed("Duplicate privacy parent process handle", error);
  // Check each object as it is created so the reported error is its own.
  stop_ = CreateEventW(&inherit, TRUE, FALSE, nullptr);
  if (!stop_) return Failed("Create high-band broker stop event", error);
  ready_ = CreateEventW(&inherit, TRUE, FALSE, nullptr);
  if (!ready_) return Failed("Create high-band broker ready event", error);
  mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &inherit, PAGE_READWRITE,
                                0, sizeof(PrivacyBandShared), nullptr);
  if (!mapping_) return Failed("Create high-band broker shared memory", error);
  shared_ = static_cast<PrivacyBandShared*>(MapViewOfFile(
      mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(PrivacyBandShared)));
  if (!shared_) {
    return Failed("Map high-band broker IPC", error);
  }
  new (shared_) PrivacyBandShared{};
  // Immutable after process startup; no cross-process string objects or
  // concurrent writes while the window thread paints.
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                           text.unlock_hint.c_str(), -1, shared_->unlock_hint,
                           static_cast<int>(std::size(shared_->unlock_hint))))
    return Failed("Encode privacy screen text", error);
  shared_->parent_pid = GetCurrentProcessId();
  HANDLE handles[] = {parent.value, stop_, ready_, mapping_};
  std::wstring bootstrap;
  for (HANDLE handle : handles) {
    if (!bootstrap.empty()) bootstrap += L",";
    bootstrap += std::to_wstring(reinterpret_cast<uintptr_t>(handle));
  }
  auto environment = ChildEnvironment(bootstrap);
  if (environment.empty()) {
    return Failed("Build broker environment", error);
  }

  job_ = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job_ || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation,
                                        &limits, sizeof(limits))) {
    return Failed("Bind high-band broker lifetime", error);
  }
  SIZE_T bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
  std::vector<unsigned char> attributes(bytes);
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.lpAttributeList =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
  if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0,
                                         &bytes)) {
    return Failed("Initialize broker handle list", error);
  }
  bool ok = UpdateProcThreadAttribute(
                startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                handles, sizeof(handles), nullptr, nullptr) != FALSE;
  PROCESS_INFORMATION child{};
  std::wstring command = L"\"" + broker_path_.wstring() + L"\"";
  if (ok) {
    ok = CreateProcessW(
             broker_path_.c_str(), command.data(), nullptr, nullptr, TRUE,
             CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT |
                 EXTENDED_STARTUPINFO_PRESENT,
             environment.data(), broker_directory_.c_str(),
             &startup.StartupInfo, &child) != FALSE;
  }
  const DWORD create_error = GetLastError();
  DeleteProcThreadAttributeList(startup.lpAttributeList);
  if (!ok)
    return Failed("Create owned RuntimeBroker process", error, create_error);
  process_ = child.hProcess;
  shared_->broker_pid = child.dwProcessId;
  if (!AssignProcessToJobObject(job_, process_)) {
    const DWORD code = GetLastError();
    TerminateProcess(process_, code);
    CloseHandle(child.hThread);
    return Failed("Assign owned RuntimeBroker to privacy job", error, code);
  }
  const auto path = dll.wstring();
  const auto size = (path.size() + 1) * sizeof(wchar_t);
  void* remote_path = VirtualAllocEx(process_, nullptr, size,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  SIZE_T written = 0;
  const auto loader =
      GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
  ok =
      remote_path && loader &&
      WriteProcessMemory(process_, remote_path, path.c_str(), size, &written) &&
      written == size &&
      QueueUserAPC(reinterpret_cast<PAPCFUNC>(loader), child.hThread,
                   reinterpret_cast<ULONG_PTR>(remote_path));
  DWORD injection_error = GetLastError();
  if (ok && ResumeThread(child.hThread) == DWORD(-1)) {
    ok = false;
    injection_error = GetLastError();
  }
  CloseHandle(child.hThread);
  if (!ok)
    return Failed("Load high-band window module into owned broker", error,
                  injection_error);
  HANDLE startup_events[] = {ready_, process_};
  const DWORD result = WaitForMultipleObjects(2, startup_events, FALSE, 5000);
  if (result != WAIT_OBJECT_0) {
    DWORD code = ERROR_TIMEOUT;
    if (result == WAIT_OBJECT_0 + 1) GetExitCodeProcess(process_, &code);
    return Failed("High-band broker did not become ready", error, code);
  }
  std::vector<PrivacyBandWindow> windows;
  if (!Windows(windows, error)) return false;
  LOG_INFO("Privacy high-band broker ready: process={}, band={}, monitors={}",
           process_id(), kPrivacyWindowBand, windows.size());
  return true;
}

bool PrivacyBandGuard::Healthy(std::string& error) {
  if (!process_ || !shared_)
    return Failed("High-band broker is not initialized", error,
                  ERROR_INVALID_HANDLE);
  const LONG code = InterlockedCompareExchange(&shared_->error, 0, 0);
  if (code != ERROR_SUCCESS) {
    char operation[256]{};
    WideCharToMultiByte(CP_UTF8, 0, shared_->operation, -1, operation,
                        sizeof(operation), nullptr, nullptr);
    return Failed(operation[0] ? operation : "High-band window startup failed",
                  error, code);
  }
  if (WaitForSingleObject(process_, 0) != WAIT_TIMEOUT) {
    DWORD exit_code = ERROR_PROCESS_ABORTED;
    GetExitCodeProcess(process_, &exit_code);
    return Failed("High-band window process exited", error, exit_code);
  }
  if (!InterlockedCompareExchange(&shared_->active, 0, 0) ||
      GetTickCount64() - static_cast<ULONGLONG>(InterlockedCompareExchange64(
                             &shared_->heartbeat, 0, 0)) >
          1500)
    return Failed("High-band window thread stopped responding", error,
                  ERROR_TIMEOUT);
  return true;
}

bool PrivacyBandGuard::Windows(std::vector<PrivacyBandWindow>& windows,
                               std::string& error) {
  if (!Healthy(error)) return false;
  const auto retry = [&] {
    error = "High-band monitor snapshot is being updated";
    SetLastError(ERROR_RETRY);
    return false;
  };
  if (InterlockedCompareExchange(&shared_->topology_pending, 0, 0)) return retry();
  for (int attempt = 0; attempt < 4; ++attempt) {
    const LONG before = InterlockedCompareExchange(&shared_->sequence, 0, 0);
    if (before & 1) continue;
    const auto count = shared_->count;
    if (count > kPrivacyMaxMonitors) break;
    windows.assign(shared_->windows, shared_->windows + count);
    if (before == InterlockedCompareExchange(&shared_->sequence, 0, 0) &&
        !InterlockedCompareExchange(&shared_->topology_pending, 0, 0)) {
      for (const auto& window : windows) {
        DWORD pid = 0;
        GetWindowThreadProcessId(reinterpret_cast<HWND>(window.hwnd), &pid);
        if (pid != process_id())
          return retry();
      }
      return true;
    }
  }
  return retry();
}

bool PrivacyBandGuard::Stop(std::string& error) {
  bool ok = true;
  if (process_) {
    if (stop_) SetEvent(stop_);
    if (WaitForSingleObject(process_, 1500) != WAIT_OBJECT_0) {
      // This handle came only from our CreateProcessW call, never discovery.
      if (!TerminateProcess(process_, ERROR_CANCELLED) ||
          WaitForSingleObject(process_, 1500) != WAIT_OBJECT_0)
        ok = Failed("Stop owned high-band broker", error);
    }
    if (!ok) return false;
    CloseHandle(process_);
    process_ = nullptr;
  }
  if (job_) CloseHandle(job_);
  job_ = nullptr;
  if (shared_) UnmapViewOfFile(shared_);
  shared_ = nullptr;
  for (HANDLE handle : {stop_, ready_, mapping_, image_lock_, dll_lock_})
    if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
  stop_ = ready_ = mapping_ = image_lock_ = dll_lock_ = nullptr;
  if (!broker_path_.empty()) DeleteFileW(broker_path_.c_str());
  if (!broker_directory_.empty()) RemoveDirectoryW(broker_directory_.c_str());
  broker_path_.clear();
  broker_directory_.clear();
  return true;
}

}  // namespace crossdesk
