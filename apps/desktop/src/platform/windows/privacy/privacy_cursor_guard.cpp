/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#include "privacy_cursor_guard.h"

#include <filesystem>
#include <vector>

#include "../screen_capturer/dxgi_cursor_state.h"
#include "privacy_cursor_api.h"
#include "privacy_cursor_process.h"
#include "rd_log.h"

namespace crossdesk {
namespace {
struct Handle {
  HANDLE value = nullptr;
  ~Handle() {
    if (value) CloseHandle(value);
  }
};
bool Failed(const char* operation, std::string& error,
            DWORD code = GetLastError()) {
  error = std::string(operation) + " failed (Windows error " +
          std::to_string(code) + ")";
  LOG_ERROR("Privacy cursor: {}", error);
  return false;
}
}  // namespace

PrivacyCursorGuard::~PrivacyCursorGuard() {
  std::string error;
  Stop(error);
  // The inherited parent handle still lets a slow helper restore on exit.
  if (process_) CloseHandle(process_);
  if (stop_event_) CloseHandle(stop_event_);
}

bool PrivacyCursorGuard::Start(std::string& error, const std::wstring& desktop,
                               HANDLE capture_owner) {
  if (process_ || restore_pending_ || delegated_)
    return Failed("Cursor guard already active", error, ERROR_BUSY);
  desktop_ = desktop;
  if (!capture_owner && SharedDxgiCursorState().ExternalControllerAvailable()) {
    // The managed GUI's privacy request reaches the host on its next tick.
    // Give that owner time to become ready before falling back to a local
    // helper (e.g. a GUI-only LAN session). Keep the privacy window responsive.
    const auto deadline = GetTickCount64() + 1000;
    while (!SharedDxgiCursorState().ExternalCursorHidden() &&
           SharedDxgiCursorState().ExternalControllerAvailable() &&
           GetTickCount64() < deadline) {
      MSG message{};
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      Sleep(10);
    }
  }
  // A managed GUI can reuse the persistent host's confirmed cursor guard.
  // It must not start a second helper that restores the shared cursor later.
  // Secure capture with its own client lifetime always owns its watchdog.
  if (!capture_owner && SharedDxgiCursorState().ExternalCursorHidden()) {
    delegated_ = true;
    return true;
  }
  helper_failure_reported_ = false;
  std::vector<wchar_t> module(32768);
  const DWORD count = GetModuleFileNameW(nullptr, module.data(),
                                         static_cast<DWORD>(module.size()));
  if (!count || count >= module.size())
    return Failed("GetModuleFileNameW", error);
  const auto helper = std::filesystem::path(module.data()).parent_path() /
                      L"crossdesk_privacy_cursor_helper.exe";
  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  Handle parent;
  Handle owner;
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(),
                       GetCurrentProcess(), &parent.value, SYNCHRONIZE, TRUE,
                       0))
    return Failed("Duplicate parent process handle", error);
  if (capture_owner && !DuplicateHandle(GetCurrentProcess(), capture_owner,
      GetCurrentProcess(), &owner.value, SYNCHRONIZE, TRUE, 0))
    return Failed("Duplicate capture owner process handle", error);
  Handle stop{CreateEventW(&security, TRUE, FALSE, nullptr)};
  Handle ready{CreateEventW(&security, TRUE, FALSE, nullptr)};
  Handle mapping{CreateFileMappingW(INVALID_HANDLE_VALUE, &security,
                                    PAGE_READWRITE, 0,
                                    sizeof(PrivacyCursorReady), nullptr)};
  if (!stop.value || !ready.value || !mapping.value)
    return Failed("Create cursor guard IPC", error);
  auto shared = static_cast<PrivacyCursorReady*>(MapViewOfFile(
      mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(PrivacyCursorReady)));
  if (!shared) return Failed("Map cursor guard readiness", error);
  InterlockedExchange(&shared->error, ERROR_IO_PENDING);

  SIZE_T bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
  std::vector<unsigned char> attributes(bytes);
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.lpDesktop = desktop.empty()
      ? nullptr : const_cast<wchar_t*>(desktop.c_str());
  startup.lpAttributeList =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
  if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0,
                                         &bytes)) {
    const DWORD code = GetLastError();
    UnmapViewOfFile(shared);
    return Failed("Initialize cursor guard handle list", error, code);
  }
  std::vector<HANDLE> inherited{parent.value, stop.value, ready.value, mapping.value};
  if (owner.value) inherited.push_back(owner.value);
  bool ok = UpdateProcThreadAttribute(
                startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                inherited.data(), inherited.size() * sizeof(HANDLE), nullptr, nullptr) != FALSE;
  PROCESS_INFORMATION child{};
  bool used_job_fallback = false;
  if (ok) {
    std::wstring command = L"\"" + helper.wstring() + L"\"";
    for (HANDLE handle : inherited)
      command += L" " + std::to_wstring(reinterpret_cast<uintptr_t>(handle));
    ok = CreatePrivacyCursorProcess(helper.c_str(), command, startup, child,
                                    used_job_fallback);
  }
  const DWORD start_error = GetLastError();
  DeleteProcThreadAttributeList(startup.lpAttributeList);
  if (!ok) {
    UnmapViewOfFile(shared);
    return Failed("Start crossdesk_privacy_cursor_helper.exe", error,
                  start_error);
  }
  if (used_job_fallback) {
    LOG_INFO("Privacy cursor: launcher denied job breakaway; helper process={} "
             "started without breakaway flag with parent-exit monitoring",
             child.dwProcessId);
  }
  CloseHandle(child.hThread);
  process_ = child.hProcess;
  stop_event_ = stop.value;
  stop.value = nullptr;
  // The helper may hide the pointer before signaling readiness. Track its
  // entire lifetime, including startup failure and delayed restoration.
  SharedDxgiCursorState().SetPrivacyCursorHidden(true);
  HANDLE startup_events[] = {ready.value, process_};
  const DWORD result = WaitForMultipleObjects(2, startup_events, FALSE, 2000);
  const DWORD ready_error =
      static_cast<DWORD>(InterlockedCompareExchange(&shared->error, 0, 0));
  UnmapViewOfFile(shared);
  if (result != WAIT_OBJECT_0 || ready_error != ERROR_SUCCESS) {
    std::string cleanup;
    Stop(cleanup);
    return Failed(
        "Hide system cursor", error,
        ready_error == ERROR_IO_PENDING ? ERROR_TIMEOUT : ready_error);
  }
  return true;
}

bool PrivacyCursorGuard::RestoreAfterFailure(std::string& error) {
  PrivacyCursorApi cursor;
  if (!cursor.Initialize() || !cursor.RestoreAndClose()) {
    restore_pending_ = true;
    return Failed("Restore system cursor after helper failure", error);
  }
  restore_pending_ = false;
  SharedDxgiCursorState().SetPrivacyCursorHidden(false);
  return true;
}

bool PrivacyCursorGuard::Healthy(std::string& error) {
  if (delegated_) {
    if (SharedDxgiCursorState().ExternalCursorHidden()) return true;
    delegated_ = false;
    return Start(error, desktop_);
  }
  if (process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) return true;
  if (restore_pending_ || (process_ && !helper_failure_reported_))
    RestoreAfterFailure(error);
  helper_failure_reported_ = true;
  error = "System cursor helper exited; privacy screen will turn off";
  return false;
}

bool PrivacyCursorGuard::Stop(std::string& error) {
  if (delegated_) {
    delegated_ = false;
    return true;
  }
  if (!process_) return !restore_pending_ || RestoreAfterFailure(error);
  if (!SetEvent(stop_event_)) return Failed("Stop cursor helper", error);
  if (WaitForSingleObject(process_, 2000) != WAIT_OBJECT_0) {
    return Failed("Wait for cursor helper restoration", error, ERROR_TIMEOUT);
  }
  DWORD code = ERROR_GEN_FAILURE;
  GetExitCodeProcess(process_, &code);
  CloseHandle(process_);
  process_ = nullptr;
  CloseHandle(stop_event_);
  stop_event_ = nullptr;
  if (code != ERROR_SUCCESS) return RestoreAfterFailure(error);
  SharedDxgiCursorState().SetPrivacyCursorHidden(false);
  return true;
}

}  // namespace crossdesk
