// clang-format off
#include <Windows.h>
#include <TlHelp32.h>
// clang-format on
#include <WtsApi32.h>
#include <libyuv.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "path_manager.h"
#include "rd_log.h"
#include "session_helper_shared.h"
#include "windows_input_marker.h"

namespace {

using crossdesk::get_logger;
using crossdesk::InitLogger;
using Json = nlohmann::json;

constexpr DWORD kSessionHelperStatePollMs = 1000;

std::atomic<HANDLE> g_session_helper_desktop_switch_event{nullptr};

struct InputDesktopInfo {
  bool available = false;
  DWORD error_code = 0;
  std::string name;
};

struct HelperState {
  std::mutex mutex;
  DWORD session_id = 0xFFFFFFFF;
  DWORD process_id = 0;
  bool session_locked = false;
  bool input_desktop_available = false;
  DWORD input_desktop_error_code = 0;
  std::string input_desktop_name;
  bool lock_app_visible = false;
  bool logon_ui_visible = false;
  bool consent_ui_visible = false;
  bool secure_desktop_active = false;
  ULONGLONG started_at_tick = 0;
  ULONGLONG last_update_tick = 0;
};

struct SecureCaptureRequest {
  int left = 0;
  int top = 0;
  int width = 0;
  int height = 0;
  bool show_cursor = true;
  int fps = 30;
  std::string interactive_stage;
  std::string interactive_desktop;
};

struct SecureMouseRequest {
  int x = 0;
  int y = 0;
  int wheel = 0;
  int flag = 0;
  std::string interactive_stage;
  std::string interactive_desktop;
};

struct SecureCaptureBuffers {
  std::vector<uint8_t> nv12_frame;
};

struct SecureSharedCaptureState {
  std::mutex mutex;
  std::thread capture_thread;
  std::atomic<bool> stop_requested{false};
  DWORD session_id = 0xFFFFFFFF;
  SecureCaptureRequest request;
  HANDLE frame_mapping = nullptr;
  HANDLE frame_ready_event = nullptr;
  uint8_t* frame_view = nullptr;
  size_t frame_view_size = 0;
  uint32_t sequence = 0;
};

struct PipeSecurityAttributes {
  PipeSecurityAttributes() = default;
  ~PipeSecurityAttributes() {
    if (security_descriptor_ != nullptr) {
      LocalFree(security_descriptor_);
    }
  }

  bool Initialize() {
    constexpr wchar_t kPipeSddl[] = L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kPipeSddl, SDDL_REVISION_1, &security_descriptor_, nullptr)) {
      return false;
    }

    attributes_.nLength = sizeof(attributes_);
    attributes_.lpSecurityDescriptor = security_descriptor_;
    attributes_.bInheritHandle = FALSE;
    return true;
  }

  SECURITY_ATTRIBUTES* get() { return &attributes_; }

 private:
  SECURITY_ATTRIBUTES attributes_{};
  PSECURITY_DESCRIPTOR security_descriptor_ = nullptr;
};

void InitializeHelperLogger() {
  static std::once_flag once_flag;
  std::call_once(once_flag, []() {
    crossdesk::PathManager path_manager("CrossDesk");
    std::filesystem::path log_path =
        path_manager.GetLogPath() / "session_helper";
    if (!log_path.empty() && path_manager.CreateDirectories(log_path)) {
      InitLogger(log_path.string());
      return;
    }
    InitLogger("logs/session_helper");
  });
}

void EnablePerMonitorDpiAwareness() {
  SetProcessDpiAwarenessContext(
      DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

void CALLBACK SessionHelperDesktopSwitchWinEventProc(
    HWINEVENTHOOK, DWORD event, HWND, LONG, LONG, DWORD, DWORD) {
  if (event != EVENT_SYSTEM_DESKTOPSWITCH) {
    return;
  }

  HANDLE event_handle =
      g_session_helper_desktop_switch_event.load(std::memory_order_relaxed);
  if (event_handle != nullptr) {
    SetEvent(event_handle);
  }
}

void EnsureSessionHelperMessageQueue() {
  MSG message{};
  PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
}

void PumpSessionHelperMessages() {
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

DWORD WaitForSessionHelperStateChange(HANDLE stop_event,
                                      HANDLE desktop_switch_event) {
  std::vector<HANDLE> wait_handles;
  if (stop_event != nullptr) {
    wait_handles.push_back(stop_event);
  }
  if (desktop_switch_event != nullptr) {
    wait_handles.push_back(desktop_switch_event);
  }

  const DWORD handle_count = static_cast<DWORD>(wait_handles.size());
  const DWORD wait_result = MsgWaitForMultipleObjects(
      handle_count, wait_handles.empty() ? nullptr : wait_handles.data(), FALSE,
      kSessionHelperStatePollMs, QS_ALLINPUT);
  if (wait_result == WAIT_OBJECT_0 + handle_count) {
    PumpSessionHelperMessages();
    return WAIT_TIMEOUT;
  }

  return wait_result;
}

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) {
    return {};
  }

  int size_needed =
      MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  if (size_needed <= 1) {
    return {};
  }

  std::wstring result(static_cast<size_t>(size_needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(),
                      size_needed);
  result.pop_back();
  return result;
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) {
    return {};
  }

  int size_needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr,
                                        0, nullptr, nullptr);
  if (size_needed <= 1) {
    return {};
  }

  std::string result(static_cast<size_t>(size_needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size_needed,
                      nullptr, nullptr);
  result.pop_back();
  return result;
}

InputDesktopInfo GetInputDesktopInfo() {
  InputDesktopInfo info;
  HDESK desktop = OpenInputDesktop(0, FALSE, GENERIC_READ);
  if (desktop == nullptr) {
    info.error_code = GetLastError();
    return info;
  }

  DWORD bytes_needed = 0;
  GetUserObjectInformationW(desktop, UOI_NAME, nullptr, 0, &bytes_needed);
  if (bytes_needed == 0) {
    info.error_code = GetLastError();
    CloseDesktop(desktop);
    return info;
  }

  std::wstring desktop_name(bytes_needed / sizeof(wchar_t), L'\0');
  if (!GetUserObjectInformationW(desktop, UOI_NAME, desktop_name.data(),
                                 bytes_needed, &bytes_needed)) {
    info.error_code = GetLastError();
    CloseDesktop(desktop);
    return info;
  }

  CloseDesktop(desktop);
  while (!desktop_name.empty() && desktop_name.back() == L'\0') {
    desktop_name.pop_back();
  }
  info.available = true;
  info.name = WideToUtf8(desktop_name);
  return info;
}

bool IsProcessRunningInCurrentSession(const wchar_t* executable_name,
                                      DWORD session_id) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return false;
  }

  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (_wcsicmp(entry.szExeFile, executable_name) != 0) {
        continue;
      }

      DWORD process_session_id = 0xFFFFFFFF;
      if (ProcessIdToSessionId(entry.th32ProcessID, &process_session_id) &&
          process_session_id == session_id) {
        found = true;
        break;
      }
    } while (Process32NextW(snapshot, &entry));
  }

  CloseHandle(snapshot);
  return found;
}

bool IsLogonUiRunningInCurrentSession(DWORD session_id) {
  return IsProcessRunningInCurrentSession(L"LogonUI.exe", session_id);
}

bool IsConsentUiRunningInCurrentSession(DWORD session_id) {
  return IsProcessRunningInCurrentSession(L"Consent.exe", session_id);
}

bool IsLockAppRunningInCurrentSession(DWORD session_id) {
  return IsProcessRunningInCurrentSession(L"LockApp.exe", session_id);
}

bool QuerySessionLockState(DWORD session_id, bool* session_locked_out) {
  if (session_locked_out == nullptr) {
    return false;
  }

  *session_locked_out = false;
  PWTSINFOEXW session_info = nullptr;
  DWORD bytes = 0;
  if (!WTSQuerySessionInformationW(
          WTS_CURRENT_SERVER_HANDLE, session_id, WTSSessionInfoEx,
          reinterpret_cast<LPWSTR*>(&session_info), &bytes)) {
    return false;
  }

  bool success = false;
  if (session_info != nullptr && bytes >= sizeof(WTSINFOEXW) &&
      session_info->Level == 1) {
    const LONG session_flags = session_info->Data.WTSInfoExLevel1.SessionFlags;
    if (session_flags == WTS_SESSIONSTATE_LOCK) {
      *session_locked_out = true;
      success = true;
    } else if (session_flags == WTS_SESSIONSTATE_UNLOCK) {
      *session_locked_out = false;
      success = true;
    }
  }

  if (session_info != nullptr) {
    WTSFreeMemory(session_info);
  }
  return success;
}

const char* DetermineInteractiveStage(bool lock_app_visible,
                                      bool credential_ui_visible,
                                      bool secure_desktop_active) {
  if (credential_ui_visible) {
    return "credential-ui";
  }
  if (lock_app_visible) {
    return "lock-screen";
  }
  if (secure_desktop_active) {
    return "secure-desktop";
  }
  return "user-desktop";
}

bool IsCredentialUiVisible(bool session_locked, bool logon_ui_running,
                           bool consent_ui_visible,
                           bool input_desktop_available,
                           bool secure_desktop_active) {
  return consent_ui_visible ||
         ((session_locked || secure_desktop_active) &&
          (logon_ui_running || !input_desktop_available));
}

std::string BuildErrorJson(const char* error, DWORD error_code = 0) {
  Json json;
  json["ok"] = false;
  json["error"] = error;
  if (error_code != 0) {
    json["code"] = error_code;
  }
  return json.dump();
}

void UpdateHelperState(HelperState* helper_state) {
  if (helper_state == nullptr) {
    return;
  }

  InputDesktopInfo desktop_info = GetInputDesktopInfo();
  bool lock_app_process_running =
      IsLockAppRunningInCurrentSession(helper_state->session_id);
  bool logon_ui_visible =
      IsLogonUiRunningInCurrentSession(helper_state->session_id);
  bool consent_ui_visible =
      IsConsentUiRunningInCurrentSession(helper_state->session_id);
  const bool consent_on_input_desktop =
      desktop_info.available && consent_ui_visible;
  const bool input_desktop_is_winlogon =
      _stricmp(desktop_info.name.c_str(), "Winlogon") == 0;
  const bool inaccessible_secure_input_desktop =
      !desktop_info.available &&
      desktop_info.error_code == ERROR_ACCESS_DENIED && !logon_ui_visible;
  const bool secure_desktop_active = input_desktop_is_winlogon ||
                                     inaccessible_secure_input_desktop;
  bool session_locked = false;
  if (!QuerySessionLockState(helper_state->session_id, &session_locked)) {
    session_locked =
        lock_app_process_running || logon_ui_visible || secure_desktop_active;
  }
  const bool lock_app_visible = session_locked && lock_app_process_running;

  std::lock_guard<std::mutex> lock(helper_state->mutex);
  helper_state->session_locked = session_locked;
  helper_state->input_desktop_available = desktop_info.available;
  helper_state->input_desktop_error_code = desktop_info.error_code;
  helper_state->input_desktop_name = desktop_info.name;
  helper_state->lock_app_visible = lock_app_visible;
  helper_state->logon_ui_visible = logon_ui_visible;
  helper_state->consent_ui_visible =
      consent_on_input_desktop || consent_ui_visible;
  helper_state->secure_desktop_active = secure_desktop_active;
  helper_state->last_update_tick = GetTickCount64();
}

std::string BuildHelperStatusResponse(HelperState* helper_state) {
  if (helper_state == nullptr) {
    return BuildErrorJson("invalid_state");
  }

  Json json;
  std::lock_guard<std::mutex> lock(helper_state->mutex);
  const bool credential_ui_visible = IsCredentialUiVisible(
      helper_state->session_locked, helper_state->logon_ui_visible,
      helper_state->consent_ui_visible,
      helper_state->input_desktop_available,
      helper_state->secure_desktop_active);
  const bool unlock_ui_visible =
      credential_ui_visible || helper_state->secure_desktop_active;
  json["ok"] = true;
  json["session_id"] = helper_state->session_id;
  json["process_id"] = helper_state->process_id;
  json["session_locked"] = helper_state->session_locked;
  json["input_desktop_available"] = helper_state->input_desktop_available;
  json["input_desktop_error_code"] = helper_state->input_desktop_error_code;
  json["input_desktop"] = helper_state->input_desktop_name;
  json["lock_app_visible"] = helper_state->lock_app_visible;
  json["logon_ui_visible"] = helper_state->logon_ui_visible;
  json["consent_ui_visible"] = helper_state->consent_ui_visible;
  json["secure_desktop_active"] = helper_state->secure_desktop_active;
  json["credential_ui_visible"] = credential_ui_visible;
  json["unlock_ui_visible"] = unlock_ui_visible;
  json["interactive_stage"] = DetermineInteractiveStage(
      helper_state->lock_app_visible, credential_ui_visible,
      helper_state->secure_desktop_active);
  json["uptime_ms"] = GetTickCount64() >= helper_state->started_at_tick
                          ? (GetTickCount64() - helper_state->started_at_tick)
                          : 0;
  json["state_age_ms"] =
      GetTickCount64() >= helper_state->last_update_tick
          ? (GetTickCount64() - helper_state->last_update_tick)
          : 0;
  return json.dump();
}

void HelperIpcServerLoop(HANDLE stop_event, DWORD session_id,
                         HelperState* helper_state) {
  PipeSecurityAttributes security_attributes;
  SECURITY_ATTRIBUTES* pipe_attributes = nullptr;
  if (security_attributes.Initialize()) {
    pipe_attributes = security_attributes.get();
  }

  std::wstring pipe_name =
      crossdesk::GetCrossDeskSessionHelperPipeName(session_id);
  while (stop_event == nullptr ||
         WaitForSingleObject(stop_event, 0) != WAIT_OBJECT_0) {
    HANDLE pipe = CreateNamedPipeW(
        pipe_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 4096, 4096, 0,
        pipe_attributes);
    if (pipe == INVALID_HANDLE_VALUE) {
      LOG_ERROR("CreateNamedPipeW failed in helper, error={}", GetLastError());
      if (stop_event != nullptr) {
        WaitForSingleObject(stop_event, 1000);
      }
      continue;
    }

    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) {
      LOG_ERROR("CreateEventW failed in helper IPC loop, error={}",
                GetLastError());
      CloseHandle(pipe);
      break;
    }

    BOOL connected = ConnectNamedPipe(pipe, &overlapped);
    DWORD connect_error = connected ? ERROR_SUCCESS : GetLastError();
    if (connected) {
      SetEvent(overlapped.hEvent);
    }
    if (!connected && connect_error == ERROR_PIPE_CONNECTED) {
      SetEvent(overlapped.hEvent);
      connected = TRUE;
    }
    if (!connected && connect_error != ERROR_IO_PENDING) {
      CloseHandle(overlapped.hEvent);
      CloseHandle(pipe);
      continue;
    }

    if (!connected) {
      if (stop_event != nullptr) {
        HANDLE wait_handles[] = {stop_event, overlapped.hEvent};
        DWORD wait_result =
            WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        if (wait_result == WAIT_OBJECT_0) {
          CancelIoEx(pipe, &overlapped);
          CloseHandle(overlapped.hEvent);
          CloseHandle(pipe);
          break;
        }
      } else {
        WaitForSingleObject(overlapped.hEvent, INFINITE);
      }
    }

    char buffer[1024] = {0};
    DWORD bytes_read = 0;
    if (ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) &&
        bytes_read > 0) {
      std::string command(buffer, buffer + bytes_read);
      std::string response;
      if (command == crossdesk::kCrossDeskSessionHelperStatusCommand) {
        response = BuildHelperStatusResponse(helper_state);
      } else if (command == "ping") {
        response = "{\"ok\":true,\"reply\":\"pong\"}";
      } else {
        response = BuildErrorJson("unknown_command");
      }

      DWORD bytes_written = 0;
      WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()),
                &bytes_written, nullptr);
      FlushFileBuffers(pipe);
    }

    DisconnectNamedPipe(pipe);
    CloseHandle(overlapped.hEvent);
    CloseHandle(pipe);
  }
}

std::wstring GetDesktopNameW(HDESK desktop) {
  if (desktop == nullptr) {
    return L"";
  }

  DWORD bytes_needed = 0;
  GetUserObjectInformationW(desktop, UOI_NAME, nullptr, 0, &bytes_needed);
  if (bytes_needed == 0) {
    return L"";
  }

  std::wstring desktop_name(bytes_needed / sizeof(wchar_t), L'\0');
  if (!GetUserObjectInformationW(desktop, UOI_NAME, desktop_name.data(),
                                 bytes_needed, &bytes_needed)) {
    return L"";
  }

  while (!desktop_name.empty() && desktop_name.back() == L'\0') {
    desktop_name.pop_back();
  }
  return desktop_name;
}

std::wstring GetCurrentThreadDesktopNameW() {
  return GetDesktopNameW(GetThreadDesktop(GetCurrentThreadId()));
}

constexpr ACCESS_MASK kCrossDeskInteractiveDesktopAccess =
    DESKTOP_CREATEWINDOW | DESKTOP_WRITEOBJECTS | DESKTOP_READOBJECTS |
    DESKTOP_SWITCHDESKTOP;

bool EnsureThreadDesktop(const wchar_t* desktop_name,
                         HDESK* opened_desktop_out = nullptr) {
  if (desktop_name == nullptr) {
    return false;
  }

  std::wstring current_desktop = GetCurrentThreadDesktopNameW();
  if (!current_desktop.empty() &&
      _wcsicmp(current_desktop.c_str(), desktop_name) == 0) {
    return true;
  }

  HDESK desktop =
      OpenDesktopW(desktop_name, 0, FALSE, kCrossDeskInteractiveDesktopAccess);
  if (desktop == nullptr) {
    return false;
  }

  if (!SetThreadDesktop(desktop)) {
    CloseDesktop(desktop);
    return false;
  }

  if (opened_desktop_out != nullptr) {
    *opened_desktop_out = desktop;
  } else {
    CloseDesktop(desktop);
  }
  return true;
}

bool EnsureThreadInputDesktop(HDESK* opened_desktop_out = nullptr) {
  HDESK desktop =
      OpenInputDesktop(0, FALSE, kCrossDeskInteractiveDesktopAccess);
  if (desktop == nullptr) {
    return false;
  }

  const std::wstring input_desktop = GetDesktopNameW(desktop);
  const std::wstring current_desktop = GetCurrentThreadDesktopNameW();
  if (!input_desktop.empty() && !current_desktop.empty() &&
      _wcsicmp(input_desktop.c_str(), current_desktop.c_str()) == 0) {
    if (opened_desktop_out != nullptr) {
      *opened_desktop_out = desktop;
    } else {
      CloseDesktop(desktop);
    }
    return true;
  }

  if (!SetThreadDesktop(desktop)) {
    CloseDesktop(desktop);
    return false;
  }

  if (opened_desktop_out != nullptr) {
    *opened_desktop_out = desktop;
  } else {
    CloseDesktop(desktop);
  }
  return true;
}

bool EnsureThreadInteractiveDesktop(HDESK* opened_desktop_out = nullptr) {
  if (opened_desktop_out != nullptr) {
    *opened_desktop_out = nullptr;
  }

  if (EnsureThreadInputDesktop(opened_desktop_out)) {
    return true;
  }
  const DWORD input_desktop_error = GetLastError();

  if (EnsureThreadDesktop(L"Winlogon", opened_desktop_out)) {
    return true;
  }
  const DWORD winlogon_error = GetLastError();

  LOG_WARN(
      "Failed to switch secure input helper desktop, input_error={}, "
      "winlogon_error={}, current='{}'",
      input_desktop_error, winlogon_error,
      WideToUtf8(GetCurrentThreadDesktopNameW()));
  SetLastError(winlogon_error != ERROR_SUCCESS ? winlogon_error
                                               : input_desktop_error);
  return false;
}

std::wstring DesktopNameForInteractiveStage(
    const std::string& interactive_stage,
    const std::string& interactive_desktop) {
  if (!interactive_desktop.empty()) {
    return Utf8ToWide(interactive_desktop);
  }
  if (interactive_stage == "credential-ui" ||
      interactive_stage == "secure-desktop") {
    return L"Winlogon";
  }
  if (interactive_stage == "lock-screen") {
    return L"Default";
  }
  return {};
}

struct DesktopSwitchDetails {
  std::string stage;
  std::string target_desktop;
  std::string current_desktop;
  DWORD error_code = ERROR_SUCCESS;
};

struct InputInjectionResult {
  bool ok = true;
  std::string error;
  DWORD error_code = ERROR_SUCCESS;
  DesktopSwitchDetails desktop;
};

DesktopSwitchDetails BuildDesktopSwitchDetails(
    const std::string& interactive_stage,
    const std::string& interactive_desktop) {
  DesktopSwitchDetails details;
  details.stage = interactive_stage;
  const std::wstring desktop_name =
      DesktopNameForInteractiveStage(interactive_stage, interactive_desktop);
  details.target_desktop =
      !desktop_name.empty() ? WideToUtf8(desktop_name) : "input-or-Winlogon";
  details.current_desktop = WideToUtf8(GetCurrentThreadDesktopNameW());
  return details;
}

InputInjectionResult BuildInputSuccess() {
  return {};
}

InputInjectionResult BuildInputFailure(const char* error, DWORD error_code,
                                       DesktopSwitchDetails desktop) {
  desktop.error_code = error_code;
  desktop.current_desktop = WideToUtf8(GetCurrentThreadDesktopNameW());

  InputInjectionResult result;
  result.ok = false;
  result.error = error != nullptr ? error : "input_failed";
  result.error_code =
      error_code != ERROR_SUCCESS ? error_code : ERROR_GEN_FAILURE;
  result.desktop = std::move(desktop);
  return result;
}

Json BuildInputFailureJson(const InputInjectionResult& result) {
  Json json;
  json["ok"] = false;
  json["error"] = result.error;
  json["code"] = result.error_code;
  json["stage"] = result.desktop.stage;
  json["target_desktop"] = result.desktop.target_desktop;
  json["current_desktop"] = result.desktop.current_desktop;
  return json;
}

bool EnsureThreadInteractiveDesktopForStage(
    const std::string& interactive_stage,
    const std::string& interactive_desktop,
    HDESK* opened_desktop_out = nullptr,
    DesktopSwitchDetails* switch_details = nullptr) {
  if (opened_desktop_out != nullptr) {
    *opened_desktop_out = nullptr;
  }

  DesktopSwitchDetails local_details =
      BuildDesktopSwitchDetails(interactive_stage, interactive_desktop);
  if (switch_details != nullptr) {
    *switch_details = local_details;
  }

  const std::wstring desktop_name =
      DesktopNameForInteractiveStage(interactive_stage, interactive_desktop);
  if (!desktop_name.empty()) {
    if (EnsureThreadDesktop(desktop_name.c_str(), opened_desktop_out)) {
      if (switch_details != nullptr) {
        switch_details->current_desktop =
            WideToUtf8(GetCurrentThreadDesktopNameW());
      }
      return true;
    }

    const DWORD error = GetLastError();
    if (switch_details != nullptr) {
      switch_details->error_code = error;
      switch_details->current_desktop =
          WideToUtf8(GetCurrentThreadDesktopNameW());
    }
    LOG_WARN(
        "Failed to switch secure input helper to stage desktop, stage='{}', "
        "desktop='{}', error={}, current='{}'",
        interactive_stage, WideToUtf8(desktop_name),
        error,
        WideToUtf8(GetCurrentThreadDesktopNameW()));
    SetLastError(error);
    return false;
  }

  if (EnsureThreadInteractiveDesktop(opened_desktop_out)) {
    if (switch_details != nullptr) {
      switch_details->current_desktop = WideToUtf8(GetCurrentThreadDesktopNameW());
    }
    return true;
  }

  const DWORD error = GetLastError();
  if (switch_details != nullptr) {
    switch_details->error_code = error;
    switch_details->current_desktop = WideToUtf8(GetCurrentThreadDesktopNameW());
  }
  return false;
}

struct ScopedDesktopHandle {
  HDESK handle = nullptr;
  ~ScopedDesktopHandle() {
    if (handle != nullptr) {
      CloseDesktop(handle);
    }
  }
};

bool PreferSideSpecificVkInjection(int key_code) {
  switch (key_code) {
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
      return true;
    default:
      return false;
  }
}

InputInjectionResult InjectKeyboardInput(
    int key_code, bool is_down, uint32_t scan_code, bool extended,
    const std::string& interactive_stage,
    const std::string& interactive_desktop) {
  ScopedDesktopHandle desktop;
  DesktopSwitchDetails desktop_switch;
  if (!EnsureThreadInteractiveDesktopForStage(interactive_stage,
                                              interactive_desktop,
                                              &desktop.handle,
                                              &desktop_switch)) {
    const DWORD error = GetLastError();
    return BuildInputFailure("switch_interactive_desktop_failed",
                             error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE,
                             desktop_switch);
  }

  INPUT input = {0};
  input.type = INPUT_KEYBOARD;
  input.ki.dwExtraInfo = static_cast<ULONG_PTR>(
      crossdesk::kInjectedKeyboardInputMarker);

  const bool prefer_vk = PreferSideSpecificVkInjection(key_code);
  const UINT resolved_scan_code =
      scan_code != 0
          ? static_cast<UINT>(scan_code & 0xFF) | (extended ? 0xE000u : 0u)
          : MapVirtualKeyW(static_cast<UINT>(key_code), MAPVK_VK_TO_VSC_EX);

  if (scan_code != 0 && !prefer_vk) {
    input.ki.wVk = 0;
    input.ki.wScan = static_cast<WORD>(scan_code & 0xFF);
    input.ki.dwFlags |= KEYEVENTF_SCANCODE;
    if (extended) {
      input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
  } else {
    input.ki.wVk = static_cast<WORD>(key_code);

    if (prefer_vk && resolved_scan_code != 0) {
      input.ki.wScan = static_cast<WORD>(resolved_scan_code & 0xFF);
      if ((resolved_scan_code & 0xFF00) != 0) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
      }
    } else if (resolved_scan_code != 0) {
      input.ki.wVk = 0;
      input.ki.wScan = static_cast<WORD>(resolved_scan_code & 0xFF);
      input.ki.dwFlags |= KEYEVENTF_SCANCODE;
      if ((resolved_scan_code & 0xFF00) != 0) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
      }
    }
  }

  if (!is_down) {
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
  }

  UINT sent = SendInput(1, &input, sizeof(INPUT));
  if (sent != 1) {
    const DWORD error = GetLastError();
    return BuildInputFailure("send_input_failed",
                             error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE,
                             desktop_switch);
  }

  return BuildInputSuccess();
}

void ParseInteractionTail(const std::string& tail,
                          std::string* interactive_stage_out,
                          std::string* interactive_desktop_out) {
  if (interactive_stage_out != nullptr) {
    interactive_stage_out->clear();
  }
  if (interactive_desktop_out != nullptr) {
    interactive_desktop_out->clear();
  }
  if (tail.empty()) {
    return;
  }

  const size_t separator = tail.find(':');
  if (interactive_stage_out != nullptr) {
    *interactive_stage_out = separator == std::string::npos
                                 ? tail
                                 : tail.substr(0, separator);
  }
  if (separator != std::string::npos && interactive_desktop_out != nullptr) {
    *interactive_desktop_out = tail.substr(separator + 1);
  }
}

bool ParseSecureInputKeyboardCommand(const std::string& command,
                                     int* key_code_out, bool* is_down_out,
                                     uint32_t* scan_code_out,
                                     bool* extended_out,
                                     std::string* interactive_stage_out =
                                         nullptr,
                                     std::string* interactive_desktop_out =
                                         nullptr) {
  if (key_code_out == nullptr || is_down_out == nullptr ||
      scan_code_out == nullptr || extended_out == nullptr) {
    return false;
  }

  *scan_code_out = 0;
  *extended_out = false;
  if (interactive_stage_out != nullptr) {
    interactive_stage_out->clear();
  }
  if (interactive_desktop_out != nullptr) {
    interactive_desktop_out->clear();
  }

  if (command.rfind(crossdesk::kCrossDeskSecureInputKeyboardCommandPrefix, 0) !=
      0) {
    return false;
  }

  const size_t key_begin =
      std::strlen(crossdesk::kCrossDeskSecureInputKeyboardCommandPrefix);
  const size_t separator = command.find(':', key_begin);
  if (separator == std::string::npos) {
    return false;
  }

  try {
    *key_code_out = std::stoi(command.substr(key_begin, separator - key_begin));
  } catch (...) {
    return false;
  }

  const size_t scan_separator = command.find(':', separator + 1);
  const std::string state =
      scan_separator == std::string::npos
          ? command.substr(separator + 1)
          : command.substr(separator + 1, scan_separator - separator - 1);
  if (state == "1" || state == "down") {
    *is_down_out = true;
  } else if (state == "0" || state == "up") {
    *is_down_out = false;
  } else {
    return false;
  }

  if (scan_separator == std::string::npos) {
    return true;
  }

  const size_t extended_separator = command.find(':', scan_separator + 1);
  const std::string scan_code_str =
      extended_separator == std::string::npos
          ? command.substr(scan_separator + 1)
          : command.substr(scan_separator + 1,
                           extended_separator - scan_separator - 1);
  try {
    *scan_code_out = static_cast<uint32_t>(std::stoul(scan_code_str));
  } catch (...) {
    return false;
  }

  if (extended_separator == std::string::npos) {
    return true;
  }

  const size_t stage_separator = command.find(':', extended_separator + 1);
  const std::string extended_str =
      stage_separator == std::string::npos
          ? command.substr(extended_separator + 1)
          : command.substr(extended_separator + 1,
                           stage_separator - extended_separator - 1);
  if (stage_separator != std::string::npos &&
      (interactive_stage_out != nullptr || interactive_desktop_out != nullptr)) {
    ParseInteractionTail(command.substr(stage_separator + 1),
                         interactive_stage_out, interactive_desktop_out);
  }
  if (extended_str == "1" || extended_str == "true") {
    *extended_out = true;
    return true;
  }
  if (extended_str == "0" || extended_str == "false") {
    *extended_out = false;
    return true;
  }
  return false;
}

bool ParseSecureInputMouseCommand(const std::string& command,
                                  SecureMouseRequest* request_out) {
  if (request_out == nullptr) {
    return false;
  }

  if (command.rfind(crossdesk::kCrossDeskSecureInputMouseCommandPrefix, 0) !=
      0) {
    return false;
  }
  request_out->interactive_stage.clear();
  request_out->interactive_desktop.clear();

  const size_t x_begin =
      std::strlen(crossdesk::kCrossDeskSecureInputMouseCommandPrefix);
  size_t separator = command.find(':', x_begin);
  if (separator == std::string::npos) {
    return false;
  }

  try {
    request_out->x = std::stoi(command.substr(x_begin, separator - x_begin));
  } catch (...) {
    return false;
  }

  const size_t y_begin = separator + 1;
  separator = command.find(':', y_begin);
  if (separator == std::string::npos) {
    return false;
  }

  try {
    request_out->y = std::stoi(command.substr(y_begin, separator - y_begin));
  } catch (...) {
    return false;
  }

  const size_t wheel_begin = separator + 1;
  separator = command.find(':', wheel_begin);
  if (separator == std::string::npos) {
    return false;
  }

  try {
    request_out->wheel =
        std::stoi(command.substr(wheel_begin, separator - wheel_begin));
    const size_t flag_begin = separator + 1;
    const size_t stage_separator = command.find(':', flag_begin);
    request_out->flag = std::stoi(
        stage_separator == std::string::npos
            ? command.substr(flag_begin)
            : command.substr(flag_begin, stage_separator - flag_begin));
    if (stage_separator != std::string::npos) {
      ParseInteractionTail(command.substr(stage_separator + 1),
                           &request_out->interactive_stage,
                           &request_out->interactive_desktop);
    }
  } catch (...) {
    return false;
  }

  return true;
}

bool ParseSecureInputCaptureCommand(const std::string& command,
                                    SecureCaptureRequest* request_out) {
  if (request_out == nullptr) {
    return false;
  }

  if (command.rfind(crossdesk::kCrossDeskSecureInputCaptureCommandPrefix, 0) !=
      0) {
    return false;
  }
  request_out->interactive_stage.clear();
  request_out->interactive_desktop.clear();

  const size_t values_begin =
      std::strlen(crossdesk::kCrossDeskSecureInputCaptureCommandPrefix);
  int parsed_values[5] = {0};
  size_t token_begin = values_begin;
  size_t separator = std::string::npos;
  for (int index = 0; index < 5; ++index) {
    separator = command.find(':', token_begin);
    const size_t token_end =
        separator == std::string::npos ? command.size() : separator;
    if (token_end == std::string::npos || token_end <= token_begin) {
      return false;
    }

    try {
      parsed_values[index] =
          std::stoi(command.substr(token_begin, token_end - token_begin));
    } catch (...) {
      return false;
    }

    token_begin = token_end + 1;
  }

  request_out->left = parsed_values[0];
  request_out->top = parsed_values[1];
  request_out->width = parsed_values[2] & ~1;
  request_out->height = parsed_values[3] & ~1;
  request_out->show_cursor = parsed_values[4] != 0;
  if (separator != std::string::npos) {
    ParseInteractionTail(command.substr(token_begin),
                         &request_out->interactive_stage,
                         &request_out->interactive_desktop);
  }
  return request_out->width > 0 && request_out->height > 0;
}

bool ParseSecureInputCaptureStartCommand(const std::string& command,
                                         SecureCaptureRequest* request_out) {
  if (request_out == nullptr) {
    return false;
  }

  if (command.rfind(
          crossdesk::kCrossDeskSecureInputCaptureStartCommandPrefix, 0) != 0) {
    return false;
  }
  request_out->interactive_stage.clear();
  request_out->interactive_desktop.clear();

  const size_t values_begin = std::strlen(
      crossdesk::kCrossDeskSecureInputCaptureStartCommandPrefix);
  int parsed_values[6] = {0};
  size_t token_begin = values_begin;
  size_t separator = std::string::npos;
  for (int index = 0; index < 6; ++index) {
    separator = command.find(':', token_begin);
    const size_t token_end =
        separator == std::string::npos ? command.size() : separator;
    if (token_end == std::string::npos || token_end <= token_begin) {
      return false;
    }

    try {
      parsed_values[index] =
          std::stoi(command.substr(token_begin, token_end - token_begin));
    } catch (...) {
      return false;
    }

    token_begin = token_end + 1;
  }

  request_out->left = parsed_values[0];
  request_out->top = parsed_values[1];
  request_out->width = parsed_values[2] & ~1;
  request_out->height = parsed_values[3] & ~1;
  request_out->show_cursor = parsed_values[4] != 0;
  request_out->fps = parsed_values[5] > 0 ? parsed_values[5] : 30;
  if (separator != std::string::npos) {
    ParseInteractionTail(command.substr(token_begin),
                         &request_out->interactive_stage,
                         &request_out->interactive_desktop);
  }
  return request_out->width > 0 && request_out->height > 0;
}

LONG NormalizeAbsoluteMouseCoordinate(int value, int origin, int size) {
  if (size <= 1) {
    return 0;
  }

  const int clamped_value =
      (std::max)(origin, (std::min)(value, origin + size - 1));
  const long long relative_value =
      static_cast<long long>(clamped_value - origin) * 65535;
  return static_cast<LONG>(relative_value / (size - 1));
}

INPUT BuildAbsoluteMouseMoveInput(int x, int y) {
  INPUT input = {0};
  input.type = INPUT_MOUSE;

  const int virtual_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int virtual_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int virtual_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int virtual_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

  input.mi.dx =
      NormalizeAbsoluteMouseCoordinate(x, virtual_left, virtual_width);
  input.mi.dy =
      NormalizeAbsoluteMouseCoordinate(y, virtual_top, virtual_height);
  input.mi.dwFlags =
      MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
  return input;
}

InputInjectionResult InjectMouseInput(const SecureMouseRequest& request) {
  ScopedDesktopHandle desktop;
  DesktopSwitchDetails desktop_switch;
  if (!EnsureThreadInteractiveDesktopForStage(request.interactive_stage,
                                              request.interactive_desktop,
                                              &desktop.handle,
                                              &desktop_switch)) {
    const DWORD error = GetLastError();
    return BuildInputFailure("switch_interactive_desktop_failed",
                             error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE,
                             desktop_switch);
  }

  std::vector<INPUT> inputs;
  inputs.push_back(BuildAbsoluteMouseMoveInput(request.x, request.y));

  INPUT action_input = {0};
  action_input.type = INPUT_MOUSE;
  switch (request.flag) {
    case 0:
      break;
    case 1:
      action_input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
      break;
    case 2:
      action_input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
      break;
    case 3:
      action_input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
      break;
    case 4:
      action_input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
      break;
    case 5:
      action_input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
      break;
    case 6:
      action_input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
      break;
    case 7:
      action_input.mi.dwFlags = MOUSEEVENTF_WHEEL;
      action_input.mi.mouseData = request.wheel * 120;
      break;
    case 8:
      action_input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
      action_input.mi.mouseData = request.wheel * 120;
      break;
    default:
      action_input.mi.dwFlags = 0;
      break;
  }

  if (action_input.mi.dwFlags != 0) {
    inputs.push_back(action_input);
  }

  UINT sent =
      SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
  if (sent != inputs.size()) {
    const DWORD error = GetLastError();
    return BuildInputFailure("send_input_failed",
                             error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE,
                             desktop_switch);
  }

  return BuildInputSuccess();
}

std::vector<uint8_t> BuildTextResponseBytes(const std::string& response) {
  return std::vector<uint8_t>(response.begin(), response.end());
}

std::vector<uint8_t> CaptureSecureDesktopFrame(
    const SecureCaptureRequest& request,
    SecureCaptureBuffers* capture_buffers) {
  if (capture_buffers == nullptr) {
    return BuildTextResponseBytes(BuildErrorJson("invalid_capture_buffers"));
  }

  ScopedDesktopHandle desktop;
  if (!EnsureThreadInteractiveDesktopForStage(request.interactive_stage,
                                              request.interactive_desktop,
                                              &desktop.handle)) {
    const DWORD error = GetLastError();
    return BuildTextResponseBytes(BuildErrorJson(
        "switch_interactive_desktop_failed",
        error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE));
  }

  HDC screen_dc = GetDC(nullptr);
  if (screen_dc == nullptr) {
    return BuildTextResponseBytes(
        BuildErrorJson("get_dc_failed", GetLastError()));
  }

  HDC mem_dc = CreateCompatibleDC(screen_dc);
  if (mem_dc == nullptr) {
    const DWORD error = GetLastError();
    ReleaseDC(nullptr, screen_dc);
    return BuildTextResponseBytes(BuildErrorJson("create_dc_failed", error));
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = request.width;
  bmi.bmiHeader.biHeight = -request.height;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP dib =
      CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (dib == nullptr || bits == nullptr) {
    const DWORD error = GetLastError();
    DeleteDC(mem_dc);
    ReleaseDC(nullptr, screen_dc);
    return BuildTextResponseBytes(BuildErrorJson("create_dib_failed", error));
  }

  HGDIOBJ old_bitmap = SelectObject(mem_dc, dib);
  if (!BitBlt(mem_dc, 0, 0, request.width, request.height, screen_dc,
              request.left, request.top, SRCCOPY | CAPTUREBLT)) {
    const DWORD error = GetLastError();
    SelectObject(mem_dc, old_bitmap);
    DeleteObject(dib);
    DeleteDC(mem_dc);
    ReleaseDC(nullptr, screen_dc);
    return BuildTextResponseBytes(BuildErrorJson("bitblt_failed", error));
  }

  if (request.show_cursor) {
    CURSORINFO cursor_info{};
    cursor_info.cbSize = sizeof(CURSORINFO);
    if (GetCursorInfo(&cursor_info) && cursor_info.flags == CURSOR_SHOWING &&
        cursor_info.hCursor != nullptr) {
      const int cursor_x = cursor_info.ptScreenPos.x - request.left;
      const int cursor_y = cursor_info.ptScreenPos.y - request.top;
      if (cursor_x >= -64 && cursor_y >= -64 && cursor_x < request.width + 64 &&
          cursor_y < request.height + 64) {
        DrawIconEx(mem_dc, cursor_x, cursor_y, cursor_info.hCursor, 0, 0, 0,
                   nullptr, DI_NORMAL);
      }
    }
  }

  const size_t nv12_size =
      static_cast<size_t>(request.width) * request.height * 3 / 2;
  capture_buffers->nv12_frame.resize(nv12_size);
  const int convert_result = libyuv::ARGBToNV12(
      static_cast<const uint8_t*>(bits), request.width * 4,
      capture_buffers->nv12_frame.data(), request.width,
      capture_buffers->nv12_frame.data() + request.width * request.height,
      request.width, request.width, request.height);

  SelectObject(mem_dc, old_bitmap);
  DeleteObject(dib);
  DeleteDC(mem_dc);
  ReleaseDC(nullptr, screen_dc);

  if (convert_result != 0) {
    return BuildTextResponseBytes(BuildErrorJson("argb_to_nv12_failed"));
  }

  crossdesk::CrossDeskSecureDesktopFrameHeader header{};
  header.magic = crossdesk::kCrossDeskSecureDesktopFrameMagic;
  header.version = crossdesk::kCrossDeskSecureDesktopFrameVersion;
  header.left = request.left;
  header.top = request.top;
  header.width = static_cast<uint32_t>(request.width);
  header.height = static_cast<uint32_t>(request.height);
  header.payload_size =
      static_cast<uint32_t>(capture_buffers->nv12_frame.size());

  std::vector<uint8_t> response(sizeof(header) +
                                capture_buffers->nv12_frame.size());
  std::memcpy(response.data(), &header, sizeof(header));
  if (!capture_buffers->nv12_frame.empty()) {
    std::memcpy(response.data() + sizeof(header),
                capture_buffers->nv12_frame.data(),
                capture_buffers->nv12_frame.size());
  }
  return response;
}

void CloseSecureDesktopSharedCaptureResourcesLocked(
    SecureSharedCaptureState* capture_state) {
  if (capture_state == nullptr) {
    return;
  }

  if (capture_state->frame_view != nullptr) {
    UnmapViewOfFile(capture_state->frame_view);
    capture_state->frame_view = nullptr;
  }
  if (capture_state->frame_ready_event != nullptr) {
    CloseHandle(capture_state->frame_ready_event);
    capture_state->frame_ready_event = nullptr;
  }
  if (capture_state->frame_mapping != nullptr) {
    CloseHandle(capture_state->frame_mapping);
    capture_state->frame_mapping = nullptr;
  }
  capture_state->frame_view_size = 0;
}

void SecureDesktopSharedCaptureThread(
    SecureSharedCaptureState* capture_state) {
  if (capture_state == nullptr) {
    return;
  }

  SecureCaptureRequest request;
  uint8_t* frame_view = nullptr;
  {
    std::lock_guard<std::mutex> lock(capture_state->mutex);
    request = capture_state->request;
    frame_view = capture_state->frame_view;
  }

  if (frame_view == nullptr || request.width <= 0 || request.height <= 0) {
    return;
  }

  const int interval_ms =
      request.fps > 0 ? (std::max)(1, 1000 / request.fps) : 33;
  const size_t nv12_size =
      static_cast<size_t>(request.width) * request.height * 3 / 2;
  std::vector<uint8_t> nv12_frame(nv12_size);

  ScopedDesktopHandle desktop;
  if (!EnsureThreadInteractiveDesktopForStage(request.interactive_stage,
                                              request.interactive_desktop,
                                              &desktop.handle)) {
    LOG_ERROR("Secure shared capture desktop switch failed, error={}",
              GetLastError());
    return;
  }

  HDC screen_dc = GetDC(nullptr);
  if (screen_dc == nullptr) {
    LOG_ERROR("Secure shared capture GetDC failed, error={}", GetLastError());
    return;
  }

  HDC mem_dc = CreateCompatibleDC(screen_dc);
  if (mem_dc == nullptr) {
    LOG_ERROR("Secure shared capture CreateCompatibleDC failed, error={}",
              GetLastError());
    ReleaseDC(nullptr, screen_dc);
    return;
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = request.width;
  bmi.bmiHeader.biHeight = -request.height;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP dib =
      CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (dib == nullptr || bits == nullptr) {
    LOG_ERROR("Secure shared capture CreateDIBSection failed, error={}",
              GetLastError());
    DeleteDC(mem_dc);
    ReleaseDC(nullptr, screen_dc);
    return;
  }

  HGDIOBJ old_bitmap = SelectObject(mem_dc, dib);
  while (!capture_state->stop_requested.load(std::memory_order_relaxed)) {
    const auto frame_started = std::chrono::steady_clock::now();
    if (BitBlt(mem_dc, 0, 0, request.width, request.height, screen_dc,
               request.left, request.top, SRCCOPY | CAPTUREBLT)) {
      if (request.show_cursor) {
        CURSORINFO cursor_info{};
        cursor_info.cbSize = sizeof(CURSORINFO);
        if (GetCursorInfo(&cursor_info) &&
            cursor_info.flags == CURSOR_SHOWING &&
            cursor_info.hCursor != nullptr) {
          const int cursor_x = cursor_info.ptScreenPos.x - request.left;
          const int cursor_y = cursor_info.ptScreenPos.y - request.top;
          if (cursor_x >= -64 && cursor_y >= -64 &&
              cursor_x < request.width + 64 &&
              cursor_y < request.height + 64) {
            DrawIconEx(mem_dc, cursor_x, cursor_y, cursor_info.hCursor, 0, 0,
                       0, nullptr, DI_NORMAL);
          }
        }
      }

      const int convert_result = libyuv::ARGBToNV12(
          static_cast<const uint8_t*>(bits), request.width * 4,
          nv12_frame.data(), request.width,
          nv12_frame.data() + request.width * request.height, request.width,
          request.width, request.height);
      if (convert_result == 0) {
        auto* header =
            reinterpret_cast<crossdesk::CrossDeskSecureDesktopSharedFrameHeader*>(
                frame_view);
        uint8_t* payload = frame_view + sizeof(*header);
        header->writing = 1;
        MemoryBarrier();
        header->magic = crossdesk::kCrossDeskSecureDesktopFrameMagic;
        header->version = crossdesk::kCrossDeskSecureDesktopFrameVersion;
        header->left = request.left;
        header->top = request.top;
        header->width = static_cast<uint32_t>(request.width);
        header->height = static_cast<uint32_t>(request.height);
        header->payload_size = static_cast<uint32_t>(nv12_frame.size());
        std::memcpy(payload, nv12_frame.data(), nv12_frame.size());
        header->sequence = ++capture_state->sequence;
        MemoryBarrier();
        header->writing = 0;
        SetEvent(capture_state->frame_ready_event);
      }
    } else {
      LOG_WARN("Secure shared capture BitBlt failed, error={}", GetLastError());
    }

    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - frame_started)
            .count();
    if (elapsed_ms < interval_ms) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(interval_ms - elapsed_ms));
    }
  }

  SelectObject(mem_dc, old_bitmap);
  DeleteObject(dib);
  DeleteDC(mem_dc);
  ReleaseDC(nullptr, screen_dc);
}

std::vector<uint8_t> StopSecureDesktopSharedCapture(
    SecureSharedCaptureState* capture_state) {
  if (capture_state == nullptr) {
    return BuildTextResponseBytes(BuildErrorJson("invalid_capture_state"));
  }

  std::thread thread_to_join;
  {
    std::lock_guard<std::mutex> lock(capture_state->mutex);
    capture_state->stop_requested.store(true, std::memory_order_relaxed);
    if (capture_state->frame_ready_event != nullptr) {
      SetEvent(capture_state->frame_ready_event);
    }
    if (capture_state->capture_thread.joinable()) {
      thread_to_join = std::move(capture_state->capture_thread);
    }
  }

  if (thread_to_join.joinable()) {
    thread_to_join.join();
  }

  {
    std::lock_guard<std::mutex> lock(capture_state->mutex);
    CloseSecureDesktopSharedCaptureResourcesLocked(capture_state);
    capture_state->stop_requested.store(false, std::memory_order_relaxed);
    capture_state->sequence = 0;
  }

  return BuildTextResponseBytes("{\"ok\":true,\"shared_capture\":\"stopped\"}");
}

std::vector<uint8_t> StartSecureDesktopSharedCapture(
    const SecureCaptureRequest& request,
    SecureSharedCaptureState* capture_state) {
  if (capture_state == nullptr) {
    return BuildTextResponseBytes(BuildErrorJson("invalid_capture_state"));
  }

  StopSecureDesktopSharedCapture(capture_state);

  const size_t payload_size =
      static_cast<size_t>(request.width) * request.height * 3 / 2;
  const size_t mapping_size =
      sizeof(crossdesk::CrossDeskSecureDesktopSharedFrameHeader) + payload_size;
  if (payload_size == 0 || mapping_size > MAXDWORD) {
    return BuildTextResponseBytes(BuildErrorJson("invalid_capture_size"));
  }

  PipeSecurityAttributes security_attributes;
  SECURITY_ATTRIBUTES* attributes = nullptr;
  if (security_attributes.Initialize()) {
    attributes = security_attributes.get();
  }

  const std::wstring mapping_name =
      crossdesk::GetCrossDeskSecureDesktopFrameMappingName(
          capture_state->session_id);
  const std::wstring event_name =
      crossdesk::GetCrossDeskSecureDesktopFrameReadyEventName(
          capture_state->session_id);

  HANDLE frame_mapping =
      CreateFileMappingW(INVALID_HANDLE_VALUE, attributes, PAGE_READWRITE, 0,
                         static_cast<DWORD>(mapping_size),
                         mapping_name.c_str());
  if (frame_mapping == nullptr) {
    return BuildTextResponseBytes(
        BuildErrorJson("create_frame_mapping_failed", GetLastError()));
  }

  auto* frame_view = static_cast<uint8_t*>(
      MapViewOfFile(frame_mapping, FILE_MAP_ALL_ACCESS, 0, 0, mapping_size));
  if (frame_view == nullptr) {
    const DWORD error = GetLastError();
    CloseHandle(frame_mapping);
    return BuildTextResponseBytes(BuildErrorJson("map_frame_view_failed",
                                                 error));
  }

  HANDLE frame_ready_event =
      CreateEventW(attributes, FALSE, FALSE, event_name.c_str());
  if (frame_ready_event == nullptr) {
    const DWORD error = GetLastError();
    UnmapViewOfFile(frame_view);
    CloseHandle(frame_mapping);
    return BuildTextResponseBytes(
        BuildErrorJson("create_frame_event_failed", error));
  }

  std::memset(frame_view, 0, mapping_size);
  auto* header =
      reinterpret_cast<crossdesk::CrossDeskSecureDesktopSharedFrameHeader*>(
          frame_view);
  header->magic = crossdesk::kCrossDeskSecureDesktopFrameMagic;
  header->version = crossdesk::kCrossDeskSecureDesktopFrameVersion;
  header->left = request.left;
  header->top = request.top;
  header->width = static_cast<uint32_t>(request.width);
  header->height = static_cast<uint32_t>(request.height);
  header->buffer_size = static_cast<uint32_t>(payload_size);

  {
    std::lock_guard<std::mutex> lock(capture_state->mutex);
    capture_state->request = request;
    capture_state->frame_mapping = frame_mapping;
    capture_state->frame_ready_event = frame_ready_event;
    capture_state->frame_view = frame_view;
    capture_state->frame_view_size = mapping_size;
    capture_state->sequence = 0;
    capture_state->stop_requested.store(false, std::memory_order_relaxed);
    capture_state->capture_thread =
        std::thread(SecureDesktopSharedCaptureThread, capture_state);
  }

  Json json;
  json["ok"] = true;
  json["shared_capture"] = "started";
  json["width"] = request.width;
  json["height"] = request.height;
  json["fps"] = request.fps;
  return BuildTextResponseBytes(json.dump());
}

std::vector<uint8_t> HandleSecureInputHelperCommand(
    const std::string& command, SecureCaptureBuffers* capture_buffers,
    SecureSharedCaptureState* capture_state) {
  if (command == "ping") {
    return BuildTextResponseBytes("{\"ok\":true,\"reply\":\"pong\"}");
  }

  if (command == crossdesk::kCrossDeskSecureInputCaptureStopCommand) {
    return StopSecureDesktopSharedCapture(capture_state);
  }

  SecureCaptureRequest capture_start_request;
  if (ParseSecureInputCaptureStartCommand(command, &capture_start_request)) {
    return StartSecureDesktopSharedCapture(capture_start_request,
                                           capture_state);
  }

  int key_code = 0;
  bool is_down = false;
  uint32_t scan_code = 0;
  bool extended = false;
  std::string interactive_stage;
  std::string interactive_desktop;
  if (ParseSecureInputKeyboardCommand(command, &key_code, &is_down, &scan_code,
                                      &extended, &interactive_stage,
                                      &interactive_desktop)) {
    const InputInjectionResult inject_result =
        InjectKeyboardInput(key_code, is_down, scan_code, extended,
                            interactive_stage, interactive_desktop);
    if (!inject_result.ok) {
      LOG_WARN(
          "Secure input helper input failed for key_code={}, is_down={}, "
          "scan_code={}, extended={}, error='{}', stage='{}', target='{}', "
          "current='{}', code={}",
          key_code, is_down, scan_code, extended, inject_result.error,
          interactive_stage, inject_result.desktop.target_desktop,
          inject_result.desktop.current_desktop, inject_result.error_code);
      return BuildTextResponseBytes(BuildInputFailureJson(inject_result).dump());
    }

    Json json;
    json["ok"] = true;
    json["injected"] = "keyboard";
    json["key_code"] = key_code;
    json["is_down"] = is_down;
    json["scan_code"] = scan_code;
    json["extended"] = extended;
    json["stage"] = interactive_stage;
    json["interactive_desktop"] = interactive_desktop;
    json["desktop"] = WideToUtf8(GetCurrentThreadDesktopNameW());
    return BuildTextResponseBytes(json.dump());
  }

  SecureMouseRequest mouse_request;
  if (ParseSecureInputMouseCommand(command, &mouse_request)) {
    const InputInjectionResult inject_result = InjectMouseInput(mouse_request);
    if (!inject_result.ok) {
      LOG_WARN(
          "Secure input helper input failed for mouse x={}, y={}, "
          "wheel={}, flag={}, error='{}', stage='{}', target='{}', "
          "current='{}', code={}",
          mouse_request.x, mouse_request.y, mouse_request.wheel,
          mouse_request.flag, inject_result.error,
          mouse_request.interactive_stage, inject_result.desktop.target_desktop,
          inject_result.desktop.current_desktop, inject_result.error_code);
      return BuildTextResponseBytes(BuildInputFailureJson(inject_result).dump());
    }

    Json json;
    json["ok"] = true;
    json["injected"] = "mouse";
    json["x"] = mouse_request.x;
    json["y"] = mouse_request.y;
    json["wheel"] = mouse_request.wheel;
    json["flag"] = mouse_request.flag;
    json["stage"] = mouse_request.interactive_stage;
    json["interactive_desktop"] = mouse_request.interactive_desktop;
    json["desktop"] = WideToUtf8(GetCurrentThreadDesktopNameW());
    return BuildTextResponseBytes(json.dump());
  }

  SecureCaptureRequest capture_request;
  if (ParseSecureInputCaptureCommand(command, &capture_request)) {
    return CaptureSecureDesktopFrame(capture_request, capture_buffers);
  }

  return BuildTextResponseBytes(BuildErrorJson("unknown_command"));
}

void HandleSecureInputHelperPipeClient(
    HANDLE pipe, HANDLE event_handle,
    std::shared_ptr<SecureSharedCaptureState> capture_state) {
  SecureCaptureBuffers capture_buffers;
  char buffer[1024] = {0};
  DWORD bytes_read = 0;
  if (ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) &&
      bytes_read > 0) {
    std::vector<uint8_t> response = HandleSecureInputHelperCommand(
        std::string(buffer, buffer + bytes_read), &capture_buffers,
        capture_state.get());
    DWORD bytes_written = 0;
    if (!response.empty()) {
      WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()),
                &bytes_written, nullptr);
      FlushFileBuffers(pipe);
    }
  }

  DisconnectNamedPipe(pipe);
  if (event_handle != nullptr) {
    CloseHandle(event_handle);
  }
  CloseHandle(pipe);
}

void SecureInputHelperIpcServerLoop(HANDLE stop_event, DWORD session_id) {
  auto capture_state = std::make_shared<SecureSharedCaptureState>();
  capture_state->session_id = session_id;

  PipeSecurityAttributes security_attributes;
  SECURITY_ATTRIBUTES* pipe_attributes = nullptr;
  if (security_attributes.Initialize()) {
    pipe_attributes = security_attributes.get();
  }

  std::wstring pipe_name =
      crossdesk::GetCrossDeskSecureInputHelperPipeName(session_id);
  while (stop_event == nullptr ||
         WaitForSingleObject(stop_event, 0) != WAIT_OBJECT_0) {
    HANDLE pipe = CreateNamedPipeW(
        pipe_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        crossdesk::kCrossDeskSecureInputPipeBufferBytes, 4096, 0,
        pipe_attributes);
    if (pipe == INVALID_HANDLE_VALUE) {
      LOG_ERROR("CreateNamedPipeW failed in secure input helper, error={}",
                GetLastError());
      if (stop_event != nullptr) {
        WaitForSingleObject(stop_event, 1000);
      }
      continue;
    }

    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) {
      LOG_ERROR("CreateEventW failed in secure input IPC loop, error={}",
                GetLastError());
      CloseHandle(pipe);
      break;
    }

    BOOL connected = ConnectNamedPipe(pipe, &overlapped);
    DWORD connect_error = connected ? ERROR_SUCCESS : GetLastError();
    if (connected) {
      SetEvent(overlapped.hEvent);
    }
    if (!connected && connect_error == ERROR_PIPE_CONNECTED) {
      SetEvent(overlapped.hEvent);
      connected = TRUE;
    }
    if (!connected && connect_error != ERROR_IO_PENDING) {
      CloseHandle(overlapped.hEvent);
      CloseHandle(pipe);
      continue;
    }

    if (!connected) {
      if (stop_event != nullptr) {
        HANDLE wait_handles[] = {stop_event, overlapped.hEvent};
        DWORD wait_result =
            WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        if (wait_result == WAIT_OBJECT_0) {
          CancelIoEx(pipe, &overlapped);
          CloseHandle(overlapped.hEvent);
          CloseHandle(pipe);
          break;
        }
      } else {
        WaitForSingleObject(overlapped.hEvent, INFINITE);
      }
    }

    std::thread(HandleSecureInputHelperPipeClient, pipe, overlapped.hEvent,
                capture_state)
        .detach();
  }

  StopSecureDesktopSharedCapture(capture_state.get());
}

void PrintUsage() {
  std::cout << "CrossDesk session helper\n"
            << "  --session-helper             Run helper loop\n"
            << "  --secure-input-helper        Run Winlogon input helper\n"
            << "  --session-id <id>            Expected target session id\n"
            << "  --stop-event <name>          Global stop event name\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  EnablePerMonitorDpiAwareness();

  InitializeHelperLogger();

  bool run_helper = false;
  bool run_secure_input_helper = false;
  DWORD expected_session_id = 0xFFFFFFFF;
  std::wstring stop_event_name;
  for (int index = 1; index < argc; ++index) {
    std::string argument = argv[index];
    if (argument == "--session-helper") {
      run_helper = true;
      continue;
    }
    if (argument == "--secure-input-helper") {
      run_secure_input_helper = true;
      continue;
    }
    if (argument == "--session-id" && index + 1 < argc) {
      expected_session_id = static_cast<DWORD>(std::stoul(argv[++index]));
      continue;
    }
    if (argument == "--stop-event" && index + 1 < argc) {
      stop_event_name = Utf8ToWide(argv[++index]);
      continue;
    }
  }

  if (!run_helper && !run_secure_input_helper) {
    PrintUsage();
    return 0;
  }

  DWORD current_session_id = 0xFFFFFFFF;
  ProcessIdToSessionId(GetCurrentProcessId(), &current_session_id);
  HANDLE stop_event = nullptr;
  if (!stop_event_name.empty()) {
    stop_event = OpenEventW(SYNCHRONIZE, FALSE, stop_event_name.c_str());
    if (stop_event == nullptr) {
      LOG_ERROR("OpenEventW failed for stop event, error={}", GetLastError());
      return 1;
    }
  }

  if (run_secure_input_helper) {
    LOG_INFO(
        "Secure input helper starting: pid={}, current_session_id={}, "
        "expected_session_id={}",
        GetCurrentProcessId(), current_session_id, expected_session_id);
    if (expected_session_id != 0xFFFFFFFF &&
        expected_session_id != current_session_id) {
      LOG_WARN("Secure input helper session mismatch: expected={}, current={}",
               expected_session_id, current_session_id);
    }

    LOG_INFO("Secure input helper initial desktop: '{}'",
             WideToUtf8(GetCurrentThreadDesktopNameW()));
    SecureInputHelperIpcServerLoop(stop_event, current_session_id);

    if (stop_event != nullptr) {
      CloseHandle(stop_event);
    }
    LOG_INFO("Secure input helper exiting: session_id={}", current_session_id);
    return 0;
  }

  LOG_INFO(
      "Session helper starting: pid={}, current_session_id={}, "
      "expected_session_id={}",
      GetCurrentProcessId(), current_session_id, expected_session_id);

  HelperState helper_state;
  helper_state.session_id = current_session_id;
  helper_state.process_id = GetCurrentProcessId();
  helper_state.started_at_tick = GetTickCount64();
  UpdateHelperState(&helper_state);

  EnsureSessionHelperMessageQueue();
  HANDLE desktop_switch_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  HWINEVENTHOOK desktop_switch_hook = nullptr;
  if (desktop_switch_event != nullptr) {
    g_session_helper_desktop_switch_event.store(
        desktop_switch_event, std::memory_order_relaxed);
    desktop_switch_hook = SetWinEventHook(
        EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH, nullptr,
        SessionHelperDesktopSwitchWinEventProc, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (desktop_switch_hook == nullptr) {
      LOG_WARN(
          "SetWinEventHook(EVENT_SYSTEM_DESKTOPSWITCH) failed, error={}",
          GetLastError());
    }
  } else {
    LOG_WARN("CreateEventW failed for desktop switch watcher, error={}",
             GetLastError());
  }

  std::thread ipc_thread(HelperIpcServerLoop, stop_event, current_session_id,
                         &helper_state);

  std::string last_desktop_name;
  bool last_lock_app = false;
  bool last_logon_ui = false;
  bool last_consent_ui = false;
  bool last_secure_desktop = false;
  bool last_session_locked = false;
  std::string last_stage;
  while (true) {
    UpdateHelperState(&helper_state);

    std::string desktop_name;
    bool session_locked = false;
    bool input_desktop_available = false;
    bool lock_app_visible = false;
    bool logon_ui_running = false;
    bool consent_ui_visible = false;
    bool secure_desktop_active = false;
    {
      std::lock_guard<std::mutex> lock(helper_state.mutex);
      desktop_name = helper_state.input_desktop_name;
      session_locked = helper_state.session_locked;
      input_desktop_available = helper_state.input_desktop_available;
      lock_app_visible = helper_state.lock_app_visible;
      logon_ui_running = helper_state.logon_ui_visible;
      consent_ui_visible = helper_state.consent_ui_visible;
      secure_desktop_active = helper_state.secure_desktop_active;
    }
    const bool credential_ui_visible =
        IsCredentialUiVisible(session_locked, logon_ui_running,
                              consent_ui_visible,
                              input_desktop_available,
                              secure_desktop_active);
    std::string stage = DetermineInteractiveStage(
        lock_app_visible, credential_ui_visible, secure_desktop_active);

    if (desktop_name != last_desktop_name ||
        session_locked != last_session_locked ||
        lock_app_visible != last_lock_app ||
        logon_ui_running != last_logon_ui ||
        consent_ui_visible != last_consent_ui ||
        secure_desktop_active != last_secure_desktop || stage != last_stage) {
      LOG_INFO(
          "Session helper state: session_id={}, input_desktop='{}', "
          "session_locked={}, lock_app_visible={}, logon_ui_running={}, "
          "consent_ui_visible={}, secure_desktop_active={}, stage={}",
          current_session_id, desktop_name, session_locked, lock_app_visible,
          logon_ui_running, consent_ui_visible, secure_desktop_active, stage);
      last_desktop_name = desktop_name;
      last_session_locked = session_locked;
      last_lock_app = lock_app_visible;
      last_logon_ui = logon_ui_running;
      last_consent_ui = consent_ui_visible;
      last_secure_desktop = secure_desktop_active;
      last_stage = stage;
    }

    DWORD wait_result =
        WaitForSessionHelperStateChange(stop_event, desktop_switch_event);
    if (stop_event != nullptr && wait_result == WAIT_OBJECT_0) {
      break;
    }
    if (wait_result == WAIT_FAILED) {
      LOG_WARN("Session helper wait failed, error={}", GetLastError());
      break;
    }
    if (wait_result != WAIT_TIMEOUT && wait_result != WAIT_OBJECT_0 &&
        wait_result != WAIT_OBJECT_0 + 1) {
      break;
    }
  }

  if (ipc_thread.joinable()) {
    ipc_thread.join();
  }

  if (desktop_switch_hook != nullptr) {
    UnhookWinEvent(desktop_switch_hook);
  }
  g_session_helper_desktop_switch_event.store(nullptr,
                                              std::memory_order_relaxed);
  if (desktop_switch_event != nullptr) {
    CloseHandle(desktop_switch_event);
  }

  if (stop_event != nullptr) {
    CloseHandle(stop_event);
  }

  LOG_INFO("Session helper exiting: session_id={}", current_session_id);
  return 0;
}
