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
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "interactive_desktop.h"
#include "named_pipe_deadline.h"
#include "path_manager.h"
#include "platform/path_backend.h"
#include "rd_log.h"
#include "session_helper_shared.h"
#include "secure_text_input.h"
#include "secure_desktop_dxgi_capture.h"
#include "secure_desktop_wgc_capture.h"
#include "capture_backend_policy.h"
#include "secure_desktop_frame_pacer.h"
#include "secure_desktop_nv12_cache.h"
#include "service_lifetime.h"
#include "shutdown_order.h"
#include "shared_capture_lifecycle.h"
#include "shared_capture_configuration.h"
#include "usbmmidd_policy.h"
#include "usbmmidd_virtual_display.h"
#include "windows_cursor_state.h"
#include "cursor_draw.h"
#include "dxgi_cursor_state.h"
#include "windows_input_marker.h"
#include "windows_text_input.h"
#include "windows_key_metadata.h"
#include "../windows_thread_dpi.h"
#include "../privacy/privacy_cursor_guard.h"

namespace {

using crossdesk::get_logger;
using crossdesk::InitLogger;
using crossdesk::ScopedInteractiveDesktop;
using Json = nlohmann::json;

constexpr DWORD kSessionHelperStatePollMs = 1000;
constexpr ULONGLONG kCaptureOwnerLeaseMs = 4000;

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

struct SecureCaptureRequest : crossdesk::SharedCaptureConfiguration {
  std::string interactive_stage;
  std::string interactive_desktop;
  std::shared_ptr<void> owner_process;
  DWORD owner_pid = 0;
};

struct SecureMouseRequest {
  int x = 0;
  int y = 0;
  int wheel = 0;
  int flag = 0;
  std::string interactive_stage;
  std::string interactive_desktop;
};

struct SecureSharedCaptureState {
  DWORD session_id = 0xFFFFFFFF;
  SecureCaptureRequest request;
  HANDLE frame_mapping = nullptr;
  HANDLE frame_ready_event = nullptr;
  uint8_t* frame_view = nullptr;
  uint32_t sequence = 0;
  std::atomic<ULONGLONG> last_owner_refresh{0};
  // Last member: joins/cleans the producer before any captured state is destroyed.
  crossdesk::SharedCaptureLifecycle lifecycle;
};

struct IpcSecurityAttributes {
  IpcSecurityAttributes() = default;
  ~IpcSecurityAttributes() {
    if (security_descriptor_ != nullptr) {
      LocalFree(security_descriptor_);
    }
  }

  bool Initialize(const wchar_t* sddl =
                      L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)") {
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl, SDDL_REVISION_1, &security_descriptor_, nullptr)) {
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
    const auto initialize = [&](const std::filesystem::path& log_path) {
      if (log_path.empty() || !path_manager.CreateDirectories(log_path)) return false;
      try {
        InitLogger(log_path.string());
        // Creating a directory does not prove its existing log is writable.
        // Force lazy sink creation here so an access failure can be recovered.
        get_logger();
        return true;
      } catch (const spdlog::spdlog_ex&) {
        return false;
      }
    };
    if (initialize(path_manager.GetLogPath() / "session_helper")) return;
    // The ordinary session helper uses the user's token. A portable helper
    // installed in Program Files cannot append to a SYSTEM-owned capture log.
    if (initialize(crossdesk::platform::GetLogPath("CrossDesk") /
                   "session_helper")) return;
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

  const ULONGLONG sample_started = GetTickCount64();
  bool lock_app_process_running =
      IsLockAppRunningInCurrentSession(helper_state->session_id);
  bool logon_ui_visible =
      IsLogonUiRunningInCurrentSession(helper_state->session_id);
  bool consent_ui_visible =
      IsConsentUiRunningInCurrentSession(helper_state->session_id);
  // Process enumeration can straddle an unlock. Observe the input desktop
  // afterwards, and expose the sample's start time for service-side rejection.
  InputDesktopInfo desktop_info = GetInputDesktopInfo();
  const bool secure_desktop_active = crossdesk::IsSecureInputDesktop(
      desktop_info.available, desktop_info.name, desktop_info.error_code);
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
  helper_state->consent_ui_visible = consent_ui_visible;
  helper_state->secure_desktop_active = secure_desktop_active;
  helper_state->last_update_tick = sample_started;
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
  json["sample_started_tick"] = helper_state->last_update_tick;
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

// Driver install and monitor plug/unplug take seconds. They run on a worker
// so the status pipe (polled by the service every second) stays responsive;
// the GUI polls the status command until |busy| clears.
struct VirtualDisplayJobState {
  std::mutex mutex;
  std::thread worker;
  bool busy = false;
  // Monitors this helper plugged and has not yet unplugged. Unplug releases
  // only these, so a monitor the user plugged for other software survives.
  int plugged_by_helper = 0;
  // An unplug that arrived while a plug was running; the plug worker honours
  // it on completion so a cancelled session never leaves a monitor behind.
  bool unplug_queued = false;
  std::string last_action;
  std::string last_error;

  ~VirtualDisplayJobState() {
    if (worker.joinable()) worker.join();
  }
};

// The helper pipe grants read/write to every authenticated user so that the
// GUI can poll status without elevation. Plug/unplug install a driver and
// change the interactive desktop's topology as SYSTEM, so those commands are
// accepted only from a CrossDesk executable in this helper's own directory.
bool IsTrustedVirtualDisplayClient(HANDLE pipe) {
  ULONG client_pid = 0;
  if (!GetNamedPipeClientProcessId(pipe, &client_pid)) return false;
  HANDLE process =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, client_pid);
  if (process == nullptr) return false;
  wchar_t image[MAX_PATH] = {0};
  DWORD length = MAX_PATH;
  const bool queried =
      QueryFullProcessImageNameW(process, 0, image, &length) && length > 0;
  CloseHandle(process);
  if (!queried) return false;

  wchar_t own[MAX_PATH] = {0};
  const DWORD own_length = GetModuleFileNameW(nullptr, own, MAX_PATH);
  if (own_length == 0 || own_length >= MAX_PATH) return false;
  std::error_code ignored;
  const std::filesystem::path client(image);
  if (_wcsicmp(client.filename().c_str(), L"CrossDesk.exe") != 0) return false;
  const std::filesystem::path client_directory =
      std::filesystem::weakly_canonical(client.parent_path(), ignored);
  const std::filesystem::path own_directory = std::filesystem::weakly_canonical(
      std::filesystem::path(own).parent_path(), ignored);
  return _wcsicmp(client_directory.c_str(), own_directory.c_str()) == 0;
}

std::string BuildVirtualDisplayStatusResponse(VirtualDisplayJobState* job) {
  const crossdesk::UsbmmiddStatus status = crossdesk::QueryUsbmmiddStatus();
  Json json;
  json["ok"] = true;
  json["package_available"] = status.package_available;
  json["package_directory"] = status.package_directory;
  json["driver_installed"] = status.driver_installed;
  json["plugged_monitors"] = status.plugged_monitors;
  std::lock_guard<std::mutex> lock(job->mutex);
  json["busy"] = job->busy;
  json["last_action"] = job->last_action;
  json["last_error"] = job->last_error;
  return json.dump();
}

std::string StartVirtualDisplayJob(
    VirtualDisplayJobState* job, const char* action,
    std::function<bool(std::string*)> work) {
  std::lock_guard<std::mutex> lock(job->mutex);
  if (job->busy) return BuildErrorJson("virtual_display_busy");
  // A finished worker no longer touches the mutex; joining under it is safe.
  if (job->worker.joinable()) job->worker.join();
  job->busy = true;
  job->last_action = action;
  job->last_error.clear();
  LOG_INFO("Session helper virtual display job started: {}", action);
  job->worker = std::thread([job, action, work = std::move(work)]() {
    std::string error;
    const bool ok = work(&error);
    if (ok) {
      LOG_INFO("Session helper virtual display job finished: {}", action);
    } else {
      LOG_WARN("Session helper virtual display job failed: {}, error={}",
               action, error);
    }
    std::lock_guard<std::mutex> lock(job->mutex);
    job->busy = false;
    job->last_error = ok ? std::string() : (error.empty() ? "unknown" : error);
  });
  Json json;
  json["ok"] = true;
  json["pending"] = true;
  return json.dump();
}

std::string HandleVirtualDisplayCommand(const std::string& command,
                                       HANDLE pipe,
                                       VirtualDisplayJobState* job,
                                       bool* handled) {
  *handled = true;
  if (command == crossdesk::kCrossDeskVirtualDisplayStatusCommand) {
    return BuildVirtualDisplayStatusResponse(job);
  }
  const bool mutating =
      command == crossdesk::kCrossDeskVirtualDisplayUnplugCommand ||
      command.rfind(crossdesk::kCrossDeskVirtualDisplayPlugCommandPrefix, 0) ==
          0;
  if (mutating && !IsTrustedVirtualDisplayClient(pipe)) {
    LOG_WARN("Session helper rejected virtual display command '{}' from an "
             "untrusted client",
             command);
    return BuildErrorJson("virtual_display_client_not_allowed",
                          ERROR_ACCESS_DENIED);
  }
  if (command == crossdesk::kCrossDeskVirtualDisplayUnplugCommand) {
    {
      std::lock_guard<std::mutex> lock(job->mutex);
      if (job->busy && job->last_action == "plug") {
        job->unplug_queued = true;
        Json json;
        json["ok"] = true;
        json["pending"] = true;
        json["queued"] = true;
        return json.dump();
      }
    }
    return StartVirtualDisplayJob(job, "unplug", [job](std::string* error) {
      int count = 0;
      {
        std::lock_guard<std::mutex> lock(job->mutex);
        count = job->plugged_by_helper;
      }
      if (!crossdesk::ReleaseUsbmmiddDisplays(count, error)) return false;
      std::lock_guard<std::mutex> lock(job->mutex);
      job->plugged_by_helper = 0;
      return true;
    });
  }
  crossdesk::VirtualDisplayMode mode;
  if (crossdesk::ParseVirtualDisplayPlugCommand(command, &mode)) {
    return StartVirtualDisplayJob(job, "plug", [job, mode](std::string* error) {
      {
        // This job only starts when no other job runs, so any unplug queued
        // behind an earlier plug has already been honoured.
        std::lock_guard<std::mutex> lock(job->mutex);
        job->unplug_queued = false;
      }
      int plugged = 0;
      const bool ok = crossdesk::ProvisionUsbmmiddDisplay(mode, &plugged, error);
      int to_release = 0;
      {
        std::lock_guard<std::mutex> lock(job->mutex);
        job->plugged_by_helper += plugged;
        if (job->unplug_queued) {
          job->unplug_queued = false;
          to_release = job->plugged_by_helper;
        }
      }
      if (to_release > 0) {
        LOG_INFO("Session helper virtual display: releasing {} monitor(s) "
                 "for an unplug queued during the plug",
                 to_release);
        std::string release_error;
        if (crossdesk::ReleaseUsbmmiddDisplays(to_release, &release_error)) {
          std::lock_guard<std::mutex> lock(job->mutex);
          job->plugged_by_helper = 0;
        } else {
          LOG_WARN("Session helper virtual display: queued unplug failed, "
                   "error={}",
                   release_error);
        }
      }
      return ok;
    });
  }
  if (command.rfind(crossdesk::kCrossDeskVirtualDisplayPlugCommandPrefix, 0) ==
      0) {
    return BuildErrorJson("invalid_virtual_display_mode");
  }
  *handled = false;
  return {};
}

void HelperIpcServerLoop(HANDLE stop_event, DWORD session_id,
                         HelperState* helper_state,
                         VirtualDisplayJobState* virtual_display_job) {
  IpcSecurityAttributes security_attributes;
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
      bool handled = false;
      if (command == crossdesk::kCrossDeskSessionHelperStatusCommand) {
        response = BuildHelperStatusResponse(helper_state);
      } else if (command == "ping") {
        response = "{\"ok\":true,\"reply\":\"pong\"}";
      } else if (response = HandleVirtualDisplayCommand(
                     command, pipe, virtual_display_job, &handled);
                 !handled) {
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

std::wstring GetCurrentThreadDesktopNameW() {
  return crossdesk::GetDesktopNameW(GetThreadDesktop(GetCurrentThreadId()));
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

InputInjectionResult BuildInputSuccess(DesktopSwitchDetails desktop) {
  InputInjectionResult result;
  result.desktop = std::move(desktop);
  return result;
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
    ScopedInteractiveDesktop* desktop,
    DesktopSwitchDetails* switch_details = nullptr) {
  DesktopSwitchDetails local_details =
      BuildDesktopSwitchDetails(interactive_stage, interactive_desktop);
  if (switch_details != nullptr) {
    *switch_details = local_details;
  }

  std::wstring desktop_name =
      DesktopNameForInteractiveStage(interactive_stage, interactive_desktop);
  if (desktop_name.empty()) {
    desktop_name = L"Winlogon";
  }
  if (desktop->Bind(desktop_name)) {
    if (!desktop->IsReceivingInput()) {
      if (switch_details != nullptr) {
        switch_details->error_code = ERROR_REQUIRES_INTERACTIVE_WINDOWSTATION;
      }
      SetLastError(ERROR_REQUIRES_INTERACTIVE_WINDOWSTATION);
      return false;
    }
    if (switch_details != nullptr) {
      switch_details->target_desktop = WideToUtf8(desktop->name());
      switch_details->current_desktop = switch_details->target_desktop;
    }
    return true;
  }

  const DWORD error = GetLastError();
  if (switch_details != nullptr) {
    switch_details->error_code = error;
    switch_details->current_desktop = WideToUtf8(GetCurrentThreadDesktopNameW());
  }
  LOG_WARN(
      "Failed to switch secure input helper desktop, stage='{}', "
      "fallback='{}', error={}, current='{}'",
      interactive_stage, WideToUtf8(desktop_name), error,
      WideToUtf8(GetCurrentThreadDesktopNameW()));
  SetLastError(error);
  return false;
}

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
  ScopedInteractiveDesktop desktop;
  DesktopSwitchDetails desktop_switch;
  if (!EnsureThreadInteractiveDesktopForStage(interactive_stage,
                                              interactive_desktop,
                                              &desktop,
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

  if (scan_code == 0) {
    crossdesk::LookupWindowsKeyMetadataFromVk(key_code, &scan_code, &extended);
  }
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

  return BuildInputSuccess(std::move(desktop_switch));
}

InputInjectionResult InjectTextInput(
    const crossdesk::SecureTextInputRequest& request) {
  std::vector<INPUT> inputs;
  if (!crossdesk::BuildWindowsTextInputs(request.text, inputs)) {
    return BuildInputFailure("invalid_text_input", ERROR_INVALID_PARAMETER,
                            BuildDesktopSwitchDetails(request.stage,
                                                       request.desktop));
  }
  ScopedInteractiveDesktop desktop;
  DesktopSwitchDetails desktop_switch;
  if (!EnsureThreadInteractiveDesktopForStage(request.stage, request.desktop,
                                               &desktop, &desktop_switch)) {
    return BuildInputFailure("switch_interactive_desktop_failed", GetLastError(),
                             desktop_switch);
  }
  const UINT sent = crossdesk::SendWindowsTextInputs(
      inputs, [](INPUT* batch, UINT count) {
        return SendInput(count, batch, sizeof(INPUT));
      });
  if (sent != inputs.size()) {
    return BuildInputFailure("send_input_failed", GetLastError(), desktop_switch);
  }
  return BuildInputSuccess(std::move(desktop_switch));
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
  input.mi.dwExtraInfo = crossdesk::kInjectedMouseInputMarker;

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
  ScopedInteractiveDesktop desktop;
  DesktopSwitchDetails desktop_switch;
  if (!EnsureThreadInteractiveDesktopForStage(request.interactive_stage,
                                              request.interactive_desktop,
                                              &desktop,
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
  action_input.mi.dwExtraInfo = crossdesk::kInjectedMouseInputMarker;
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

  return BuildInputSuccess(std::move(desktop_switch));
}

std::vector<uint8_t> BuildTextResponseBytes(const std::string& response) {
  return std::vector<uint8_t>(response.begin(), response.end());
}

// Own the desktop before its capture objects, and release those objects before
// restoring the original desktop. Shared and pipe capture use the same order.
struct SecureDesktopCaptureResources {
  ScopedInteractiveDesktop desktop;
  // Destroy before releasing the bound desktop, after capture objects.
  std::unique_ptr<crossdesk::PrivacyCursorGuard> cursor_guard;
  bool cursor_hidden = false;
  HDC screen_dc = nullptr;
  HDC mem_dc = nullptr;
  HBITMAP dib = nullptr;
  HGDIOBJ old_bitmap = nullptr;
  void* bits = nullptr;
  const char* error = nullptr;
  DWORD error_code = ERROR_SUCCESS;
  crossdesk::SecureDesktopCursorSnapshot cursor_snapshot{};
  std::unique_ptr<crossdesk::SecureDesktopDxgiCapture> dxgi;
  std::unique_ptr<crossdesk::SecureDesktopWgcCapture> wgc;
  ULONGLONG next_gpu_attempt = 0, dxgi_started = 0, wgc_started = 0;
  bool wgc_attempted = false;
  bool using_dxgi = false;
  bool using_wgc = false;
  bool frame_pending = false;
  const uint8_t* frame_bits = nullptr;
  int frame_stride = 0;
  const std::vector<uint8_t>* frame_nv12 = nullptr;
  crossdesk::SecureDesktopNv12Cache nv12;

  ~SecureDesktopCaptureResources() {
    if (old_bitmap != nullptr && old_bitmap != HGDI_ERROR) {
      SelectObject(mem_dc, old_bitmap);
    }
    if (dib != nullptr) DeleteObject(dib);
    if (mem_dc != nullptr) DeleteDC(mem_dc);
    if (screen_dc != nullptr) ReleaseDC(nullptr, screen_dc);
  }

  bool Fail(const char* reason) {
    error = reason;
    error_code = GetLastError();
    if (error_code == ERROR_SUCCESS) error_code = ERROR_GEN_FAILURE;
    return false;
  }

  bool Initialize(const SecureCaptureRequest& request, bool shared = false) {
    const auto fallback = DesktopNameForInteractiveStage(
        request.interactive_stage, request.interactive_desktop);
    if (!desktop.Bind(fallback.empty() ? L"Winlogon" : fallback)) {
      return Fail("switch_interactive_desktop_failed");
    }
    if (!desktop.IsReceivingInput()) {
      SetLastError(ERROR_REQUIRES_INTERACTIVE_WINDOWSTATION);
      return Fail("inactive_capture_desktop");
    }
    if (shared && !request.show_cursor && request.owner_process) {
      cursor_guard = std::make_unique<crossdesk::PrivacyCursorGuard>();
      std::string cursor_error;
      cursor_hidden = cursor_guard->Start(cursor_error,
          L"winsta0\\" + desktop.name(), request.owner_process.get());
      if (cursor_hidden) {
        LOG_INFO("Secure capture: system cursor hidden on '{}'; native controller draws it",
                 WideToUtf8(desktop.name()));
      } else {
        LOG_WARN("Secure capture: cursor exclusion unavailable: {}", cursor_error);
        cursor_guard.reset();
      }
    }
    return true;
  }

  bool InitializeGdi(const SecureCaptureRequest& request) {
    if (bits) return true;
    screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) return Fail("get_dc_failed");
    mem_dc = CreateCompatibleDC(screen_dc);
    if (mem_dc == nullptr) return Fail("create_dc_failed");
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = request.width;
    bmi.bmiHeader.biHeight = -request.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    dib = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib == nullptr || bits == nullptr) return Fail("create_dib_failed");
    old_bitmap = SelectObject(mem_dc, dib);
    if (old_bitmap == nullptr || old_bitmap == HGDI_ERROR) {
      return Fail("select_bitmap_failed");
    }
    return true;
  }

  bool Capture(const SecureCaptureRequest& request, bool wait_for_first_frame = false) {
    frame_pending = false;
    if (cursor_hidden) {
      std::string cursor_error;
      if (!cursor_guard->Healthy(cursor_error)) {
        cursor_hidden = false;
        cursor_guard.reset();
        dxgi.reset();
        wgc.reset();
        next_gpu_attempt = 0;
        wgc_attempted = false;
        LOG_WARN("Secure capture: cursor helper stopped; restoring captured cursor: {}",
                 cursor_error);
      }
    }
    const ULONGLONG now = GetTickCount64();
    const bool retry_gpu = now >= next_gpu_attempt;
    if (retry_gpu) {
      next_gpu_attempt = now + 5000;
      wgc_attempted = false;
    }
    using_dxgi = using_wgc = false;
    frame_nv12 = nullptr;
    frame_bits = nullptr;
    for (const auto backend : crossdesk::WindowsCaptureBackendOrder()) {
      if (backend == crossdesk::ScreenCaptureMethod::Dxgi) {
        if (!dxgi && retry_gpu) {
          auto candidate = std::make_unique<crossdesk::SecureDesktopDxgiCapture>();
          const HRESULT result = candidate->Initialize(request.left, request.top,
                                                        request.width, request.height);
          if (SUCCEEDED(result)) {
            dxgi = std::move(candidate);
            dxgi_started = GetTickCount64();
          } else LOG_INFO("Secure capture: DXGI unavailable, trying WGC, hr=0x{:08X}",
                          static_cast<uint32_t>(result));
        }
        if (!dxgi) continue;
        const HRESULT result = dxgi->Capture();
        if (result == S_OK) {
          using_dxgi = true;
          wgc.reset();
          wgc_attempted = false;
          frame_bits = dxgi->pixels();
          frame_stride = dxgi->stride();
          break;
        }
        if (result == S_FALSE && GetTickCount64() - dxgi_started < 250) {
          // Keep an already usable fallback while the preferred GPU warms up.
          if (wait_for_first_frame && !wgc && !bits) {
            frame_pending = true;
            return false;
          }
        } else {
          LOG_INFO("Secure capture: DXGI lost, trying WGC, hr=0x{:08X}", static_cast<uint32_t>(result));
          dxgi.reset();
          next_gpu_attempt = now + 5000;
        }
      } else if (backend == crossdesk::ScreenCaptureMethod::Wgc) {
        if (!wgc && !wgc_attempted) {
          wgc_attempted = true;
          auto candidate = std::make_unique<crossdesk::SecureDesktopWgcCapture>();
          const int result = candidate->Initialize(request.left, request.top, request.width,
                  request.height, request.fps, request.show_cursor);
          if (result == 0) {
            wgc = std::move(candidate);
            wgc_started = GetTickCount64();
            LOG_INFO("Secure capture: WGC started");
          } else LOG_INFO("Secure capture: WGC unavailable, trying GDI, ret={}", result);
        }
        if (!wgc) continue;
        const HRESULT result = wgc->Capture();
        if (result == S_OK) {
          using_wgc = true;
          frame_nv12 = &wgc->pixels();
          break;
        }
        if (result == S_FALSE && GetTickCount64() - wgc_started < 250) {
          if (wait_for_first_frame && !bits) {
            frame_pending = true;
            return false;
          }
        } else {
          LOG_INFO("Secure capture: WGC lost, trying GDI, hr=0x{:08X}", static_cast<uint32_t>(result));
          wgc.reset();
          next_gpu_attempt = now + 5000;
        }
      } else {
        if (!InitializeGdi(request)) return false;
        if (!BitBlt(mem_dc, 0, 0, request.width, request.height, screen_dc,
                    request.left, request.top, SRCCOPY | CAPTUREBLT))
          return Fail("bitblt_failed");
        frame_bits = static_cast<const uint8_t*>(bits);
        frame_stride = request.width * 4;
      }
    }
    // Sample on the thread bound to the captured input desktop, including when
    // native controllers render the cursor themselves (show_cursor == false).
    cursor_snapshot = {};
    CURSORINFO cursor{};
    cursor.cbSize = sizeof(cursor);
    if (GetCursorInfo(&cursor)) {
      static thread_local crossdesk::WindowsCursorState cursor_state;
      crossdesk::CursorState state{};
      // MagShowSystemCursor hides the pointer without clearing CURSOR_SHOWING.
      // A successfully hidden pointer is separate, regardless of the driver's
      // usual software-cursor default on Winlogon.
      const bool embedded = using_wgc ? wgc->CursorEmbedded()
          : !(cursor_hidden || crossdesk::SharedDxgiCursorState().PrivacyCursorHidden()) &&
                using_dxgi && dxgi->CursorEmbedded(cursor);
      cursor_state.Sample(cursor, embedded, &state);
      cursor_snapshot.valid = 1;
      cursor_snapshot.visible = state.visible;
      cursor_snapshot.shape = static_cast<uint32_t>(state.shape);
      cursor_snapshot.render_mode = static_cast<uint32_t>(state.render_mode);
      cursor_snapshot.hidden_reason = static_cast<uint32_t>(state.hidden_reason);
      cursor_snapshot.x = cursor.ptScreenPos.x;
      cursor_snapshot.y = cursor.ptScreenPos.y;
      // Never draw into DXGI's cached desktop: each cursor position starts
      // with clean pixels, including on a static screen or after a toggle.
      if (request.show_cursor && !embedded && (using_dxgi || using_wgc) &&
          (cursor.flags & CURSOR_SHOWING) != 0) {
        if (!InitializeGdi(request)) return false;
        const size_t row_bytes = static_cast<size_t>(request.width) * 4;
        if (frame_nv12) {
          const auto* pixels = frame_nv12->data();
          if (libyuv::NV12ToARGB(pixels, request.width,
                pixels + static_cast<size_t>(request.width) * request.height,
                request.width, static_cast<uint8_t*>(bits), request.width * 4,
                request.width, request.height) != 0)
            return Fail("nv12_to_argb_failed");
          frame_nv12 = nullptr;
        } else {
          for (int y = 0; y < request.height; ++y)
            std::memcpy(static_cast<uint8_t*>(bits) + y * row_bytes,
                        frame_bits + static_cast<size_t>(y) * frame_stride, row_bytes);
        }
        frame_bits = static_cast<const uint8_t*>(bits);
        frame_stride = request.width * 4;
      }
      if (request.show_cursor && !embedded && mem_dc &&
          crossdesk::DrawCursorInCapture(mem_dc, cursor, request.left,
                                        request.top, request.width,
                                        request.height)) {
        cursor_snapshot.visible = 0;
        cursor_snapshot.render_mode =
            static_cast<uint32_t>(crossdesk::CursorRenderMode::embedded);
      }
    }
    // Complete GDI writes before libyuv reads the DIB's pixel memory.
    if ((!using_dxgi && !using_wgc) || (bits && frame_bits == bits)) GdiFlush();
    return true;
  }
};

std::vector<uint8_t> CaptureSecureDesktopFrame(
    const SecureCaptureRequest& request) {
  SecureDesktopCaptureResources capture;
  if (!capture.Initialize(request) || !capture.Capture(request)) {
    return BuildTextResponseBytes(
        BuildErrorJson(capture.error, capture.error_code));
  }

  const size_t nv12_size =
      static_cast<size_t>(request.width) * request.height * 3 / 2;
  crossdesk::CrossDeskSecureDesktopFrameHeader header{};
  // Each pipe client handles one command, so an intermediate capture buffer
  // cannot be reused. Convert straight into the response payload.
  std::vector<uint8_t> response(sizeof(header) + nv12_size);
  uint8_t* payload = response.data() + sizeof(header);
  const int convert_result = capture.frame_nv12
      ? (std::memcpy(payload, capture.frame_nv12->data(), nv12_size), 0)
      : libyuv::ARGBToNV12(
      capture.frame_bits, capture.frame_stride, payload, request.width,
      payload + static_cast<size_t>(request.width) * request.height,
      request.width, request.width, request.height);

  if (convert_result != 0) {
    return BuildTextResponseBytes(BuildErrorJson("argb_to_nv12_failed"));
  }

  header.magic = crossdesk::kCrossDeskSecureDesktopFrameMagic;
  header.version = crossdesk::kCrossDeskSecureDesktopFrameVersion;
  header.left = request.left;
  header.top = request.top;
  header.width = static_cast<uint32_t>(request.width);
  header.height = static_cast<uint32_t>(request.height);
  header.payload_size = static_cast<uint32_t>(nv12_size);
  header.cursor = capture.cursor_snapshot;

  std::memcpy(response.data(), &header, sizeof(header));
  return response;
}

void CloseSecureDesktopSharedCaptureResources(
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
  capture_state->sequence = 0;
  capture_state->request = {};
  capture_state->last_owner_refresh.store(0, std::memory_order_relaxed);
}

void SecureDesktopSharedCaptureThread(SecureSharedCaptureState* capture_state) {
  if (capture_state == nullptr) {
    return;
  }

  // Start publishes these fields before launching us. Lifecycle control keeps
  // the request, mapping and event alive and unchanged until this thread joins.
  const SecureCaptureRequest request = capture_state->request;
  uint8_t* const frame_view = capture_state->frame_view;

  if (frame_view == nullptr || request.width <= 0 || request.height <= 0) {
    return;
  }

  crossdesk::ScopedWindowsPhysicalCoordinates physical_coordinates;
  crossdesk::SecureDesktopFramePacer pacer(request.fps);
  std::unique_ptr<SecureDesktopCaptureResources> capture;
  ULONGLONG last_error_tick = 0;
  auto stats_started = std::chrono::steady_clock::now();
  unsigned frame_count = 0, dxgi_frames = 0, dxgi_updates = 0, wgc_frames = 0;
  unsigned converted_frames = 0;
  double capture_ms = 0, convert_ms = 0, publish_ms = 0;
  auto report_stats = [&](bool force) {
    const auto elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - stats_started)
                             .count();
    if (elapsed <= 0 || (!force && elapsed < 5.0)) return;
    if (frame_count > 0) {
      LOG_INFO(
          "Secure capture producer: fps={:.1f}, capture_avg_ms={:.1f}, "
          "convert_avg_ms={:.1f}, publish_avg_ms={:.1f}, size={}x{}, "
          "target_fps={}, dxgi_frames={}, dxgi_updates={}, wgc_frames={}, gdi_frames={}, "
          "converted_frames={}, reused_frames={}",
          frame_count / elapsed, capture_ms / frame_count,
          convert_ms / frame_count, publish_ms / frame_count, request.width,
          request.height, request.fps, dxgi_frames, dxgi_updates, wgc_frames,
          frame_count - dxgi_frames - wgc_frames, converted_frames,
          frame_count - converted_frames);
    }
    stats_started = std::chrono::steady_clock::now();
    frame_count = dxgi_frames = dxgi_updates = wgc_frames = converted_frames = 0;
    capture_ms = convert_ms = publish_ms = 0;
  };
  while (!capture_state->lifecycle.StopRequested()) {
    if (request.owner_process &&
        WaitForSingleObject(request.owner_process.get(), 0) != WAIT_TIMEOUT) {
      LOG_INFO("Secure capture owner exited; stopping capture and restoring cursor");
      break;
    }
    if (GetTickCount64() - capture_state->last_owner_refresh.load(
            std::memory_order_relaxed) >= kCaptureOwnerLeaseMs) {
      LOG_INFO("Secure capture lease expired; restoring cursor");
      break;
    }
    const auto frame_started = std::chrono::steady_clock::now();
    report_stats(false);
    // A lock/credential/UAC transition can change the desktop before the
    // service's next status poll. Release its DCs before binding the new one.
    if (capture && !capture->desktop.IsInputDesktop()) {
      capture.reset();
    }
    if (!capture) {
      capture = std::make_unique<SecureDesktopCaptureResources>();
      if (!capture->Initialize(request, true)) {
        const DWORD error = capture->error_code;
        capture.reset();
        const ULONGLONG now = GetTickCount64();
        if (last_error_tick == 0 || now - last_error_tick >= 5000) {
          LOG_WARN(
              "Secure shared capture initialization failed; retrying, "
              "error={}",
              error);
          last_error_tick = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }
    }

    if (capture->Capture(request, true)) {
      if (capture_state->lifecycle.StopRequested()) break;
      const auto captured_at = std::chrono::steady_clock::now();
      // Composited/animated cursors and GDI frames always need conversion.
      // Native controllers use independent cursor metadata, so a DXGI timeout
      // or pointer-only update can reuse the NV12 image safely.
      const bool pixels_changed = !capture->using_dxgi ||
                                  capture->dxgi->updated() || request.show_cursor;
      if (capture->frame_nv12 || capture->nv12.Convert(capture->frame_bits, capture->frame_stride,
                               request.width, request.height, pixels_changed)) {
        const auto& nv12_frame = capture->frame_nv12 ? *capture->frame_nv12 : capture->nv12.pixels();
        const bool pixels_updated = capture->frame_nv12 ? capture->wgc->updated() : capture->nv12.updated();
        const auto converted_at = std::chrono::steady_clock::now();
        auto* header = reinterpret_cast<
            crossdesk::CrossDeskSecureDesktopSharedFrameHeader*>(frame_view);
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
        header->cursor = capture->cursor_snapshot;
        if (pixels_updated) {
          std::memcpy(payload, nv12_frame.data(), nv12_frame.size());
          ++converted_frames;
        }
        header->sequence = ++capture_state->sequence;
        MemoryBarrier();
        header->writing = 0;
        SetEvent(capture_state->frame_ready_event);
        ++frame_count;
        if (capture->using_dxgi) {
          ++dxgi_frames;
          if (capture->dxgi->updated()) ++dxgi_updates;
        } else if (capture->using_wgc) ++wgc_frames;
        capture_ms += std::chrono::duration<double, std::milli>(captured_at -
                                                                frame_started)
                          .count();
        convert_ms += std::chrono::duration<double, std::milli>(converted_at -
                                                                captured_at)
                          .count();
        publish_ms += std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - converted_at)
                          .count();
      }
    } else if (!capture->frame_pending) {
      const DWORD error = capture->error_code;
      capture.reset();
      const ULONGLONG now = GetTickCount64();
      if (last_error_tick == 0 || now - last_error_tick >= 5000) {
        LOG_WARN(
            "Secure shared capture BitBlt failed; recreating capture, "
            "error={}",
            error);
        last_error_tick = now;
      }
    }

    if (capture) {
      pacer.Wait(frame_started);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  report_stats(true);
}

std::vector<uint8_t> StopSecureDesktopSharedCapture(
    SecureSharedCaptureState* capture_state) {
  if (capture_state == nullptr) {
    return BuildTextResponseBytes(BuildErrorJson("invalid_capture_state"));
  }

  capture_state->lifecycle.Stop();

  return BuildTextResponseBytes("{\"ok\":true,\"shared_capture\":\"stopped\"}");
}

std::vector<uint8_t> PrepareSecureDesktopSharedCapture(
    const SecureCaptureRequest& request,
    SecureSharedCaptureState* capture_state) {
  if (capture_state == nullptr) {
    return BuildTextResponseBytes(BuildErrorJson("invalid_capture_state"));
  }

  const size_t payload_size =
      static_cast<size_t>(request.width) * request.height * 3 / 2;
  const size_t mapping_size =
      sizeof(crossdesk::CrossDeskSecureDesktopSharedFrameHeader) + payload_size;
  if (payload_size == 0 || mapping_size > MAXDWORD) {
    return BuildTextResponseBytes(BuildErrorJson("invalid_capture_size"));
  }

  IpcSecurityAttributes security_attributes;
  SECURITY_ATTRIBUTES* attributes = nullptr;
  if (security_attributes.Initialize()) {
    attributes = security_attributes.get();
  }

  // Event GENERIC_READ/GENERIC_WRITE omit SYNCHRONIZE. The GUI only needs
  // to wait for frames; the SYSTEM helper retains permission to signal them.
  IpcSecurityAttributes event_security_attributes;
  if (!event_security_attributes.Initialize(
          L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00100000;;;AU)")) {
    return BuildTextResponseBytes(
        BuildErrorJson("create_frame_event_security_failed", GetLastError()));
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
      CreateEventW(event_security_attributes.get(), FALSE, FALSE,
                   event_name.c_str());
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

  // Transfer handles before any potentially throwing request/string copy.
  // The lifecycle owner cleans partially prepared resources on failure too.
  capture_state->frame_mapping = frame_mapping;
  capture_state->frame_ready_event = frame_ready_event;
  capture_state->frame_view = frame_view;
  capture_state->sequence = 0;
  capture_state->request = request;
  capture_state->last_owner_refresh.store(GetTickCount64(), std::memory_order_relaxed);

  return {};
}

std::vector<uint8_t> StartSecureDesktopSharedCapture(
    const SecureCaptureRequest& request,
    SecureSharedCaptureState* capture_state) {
  if (!capture_state) {
    return BuildTextResponseBytes(BuildErrorJson("invalid_capture_state"));
  }
  std::vector<uint8_t> error;
  const auto result = capture_state->lifecycle.Start(
      [&] {
        error = PrepareSecureDesktopSharedCapture(request, capture_state);
        return error.empty();
      },
      [capture_state] { SecureDesktopSharedCaptureThread(capture_state); },
      [capture_state] {
        CloseSecureDesktopSharedCaptureResources(capture_state);
      },
      [&] {
        // A timed-out caller may retry a start that already succeeded.
        const bool reuse = capture_state->request == request &&
            capture_state->request.owner_pid == request.owner_pid &&
            capture_state->request.owner_process &&
            WaitForSingleObject(capture_state->request.owner_process.get(), 0) == WAIT_TIMEOUT;
        if (reuse)
          capture_state->last_owner_refresh.store(GetTickCount64(), std::memory_order_relaxed);
        return reuse;
      });
  if (result == crossdesk::SharedCaptureLifecycle::StartResult::ShuttingDown) {
    return BuildTextResponseBytes(BuildErrorJson("capture_shutting_down"));
  }
  if (result != crossdesk::SharedCaptureLifecycle::StartResult::Started) {
    return error.empty()
               ? BuildTextResponseBytes(BuildErrorJson("capture_start_failed"))
               : error;
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
    const std::string& command,
    SecureSharedCaptureState* capture_state,
    const std::shared_ptr<void>& owner_process, DWORD owner_pid) {
  if (command.rfind(crossdesk::kSecureTextInputCommandPrefix, 0) == 0) {
    crossdesk::SecureTextInputRequest request;
    if (!crossdesk::ParseSecureTextInputCommand(command, request))
      return BuildTextResponseBytes(BuildErrorJson("invalid_text_input"));
    const auto result = InjectTextInput(request);
    if (!result.ok) {
      // Text may contain a login password. Never echo it in logs or replies.
      LOG_WARN("Secure input helper text injection failed: error={}, code={}, "
               "stage={}, target={}, current={}",
               result.error, result.error_code, result.desktop.stage,
               result.desktop.target_desktop, result.desktop.current_desktop);
      return BuildTextResponseBytes(BuildInputFailureJson(result).dump());
    }
    return BuildTextResponseBytes("{\"ok\":true,\"injected\":\"text\"}");
  }
  if (command == "ping") {
    return BuildTextResponseBytes("{\"ok\":true,\"reply\":\"pong\"}");
  }

  if (command == crossdesk::kCrossDeskSecureInputCaptureStopCommand) {
    return StopSecureDesktopSharedCapture(capture_state);
  }

  SecureCaptureRequest capture_start_request;
  if (ParseSecureInputCaptureStartCommand(command, &capture_start_request)) {
    if (!owner_process || WaitForSingleObject(owner_process.get(), 0) != WAIT_TIMEOUT)
      return BuildTextResponseBytes(BuildErrorJson("capture_owner_unavailable"));
    capture_start_request.owner_process = owner_process;
    capture_start_request.owner_pid = owner_pid;
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
    json["desktop"] = inject_result.desktop.current_desktop;
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
    json["desktop"] = inject_result.desktop.current_desktop;
    return BuildTextResponseBytes(json.dump());
  }

  SecureCaptureRequest capture_request;
  if (ParseSecureInputCaptureCommand(command, &capture_request)) {
    return CaptureSecureDesktopFrame(capture_request);
  }

  return BuildTextResponseBytes(BuildErrorJson("unknown_command"));
}

void HandleSecureInputHelperPipeClient(
    HANDLE pipe, HANDLE event_handle,
    std::shared_ptr<SecureSharedCaptureState> capture_state) {
  char buffer[crossdesk::kSecureInputCommandBufferBytes] = {0};
  DWORD bytes_read = 0;
  DWORD error = ERROR_SUCCESS;
  if (crossdesk::pipe_deadline_detail::Transfer(
          pipe, event_handle, false, buffer, sizeof(buffer) - 1,
          GetTickCount64() + 1000, &bytes_read, &error) &&
      bytes_read > 0) {
    // Bind the producer/cursor watchdog to the Windows-identified caller,
    // never a PID supplied in a capture command. The process object remains
    // valid even after its PID is reused.
    ULONG owner_pid = 0;
    std::shared_ptr<void> owner_process;
    if (GetNamedPipeClientProcessId(pipe, &owner_pid)) {
      HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, owner_pid);
      if (process) owner_process = std::shared_ptr<void>(process,
          [](void* handle) { CloseHandle(handle); });
    }
    std::vector<uint8_t> response = HandleSecureInputHelperCommand(
        std::string(buffer, buffer + bytes_read), capture_state.get(),
        owner_process, owner_pid);
    DWORD bytes_written = 0;
    if (!response.empty()) {
      if (crossdesk::pipe_deadline_detail::Transfer(
              pipe, event_handle, true, response.data(),
              static_cast<DWORD>(response.size()), GetTickCount64() + 1000,
              &bytes_written, &error)) {
        FlushFileBuffers(pipe);
      }
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

  IpcSecurityAttributes security_attributes;
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
        if (wait_result != WAIT_OBJECT_0 + 1) {
          CancelIoEx(pipe, &overlapped);
          DWORD ignored = 0;
          GetOverlappedResult(pipe, &overlapped, &ignored, TRUE);
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

  capture_state->lifecycle.Shutdown();
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
  const DWORD shutdown_error = crossdesk::ConfigureWindowsShutdownOrder(
      crossdesk::WindowsShutdownRole::helper);
  EnablePerMonitorDpiAwareness();

  InitializeHelperLogger();
  if (shutdown_error != ERROR_SUCCESS) {
    LOG_WARN("Could not configure Windows helper shutdown order, error={}",
             shutdown_error);
  }

  bool run_helper = false;
  bool run_secure_input_helper = false;
  DWORD expected_session_id = 0xFFFFFFFF;
  std::wstring stop_event_name;
  std::wstring service_lifetime_name;
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
    if (argument == "--service-lifetime" && index + 1 < argc) {
      service_lifetime_name = Utf8ToWide(argv[++index]);
      continue;
    }
  }

  if (!run_helper && !run_secure_input_helper) {
    PrintUsage();
    return 0;
  }

  DWORD current_session_id = 0xFFFFFFFF;
  crossdesk::ServiceLifetimeWatcher lifetime;
  if (!service_lifetime_name.empty() && !lifetime.Start(service_lifetime_name))
    return 1;
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

  VirtualDisplayJobState virtual_display_job;
  std::thread ipc_thread(HelperIpcServerLoop, stop_event, current_session_id,
                         &helper_state, &virtual_display_job);

  std::string last_desktop_name;
  bool last_lock_app = false;
  bool last_logon_ui = false;
  bool last_consent_ui = false;
  bool last_secure_desktop = false;
  bool last_session_locked = false;
  bool last_input_desktop_available = false;
  DWORD last_input_desktop_error = ERROR_SUCCESS;
  std::string last_stage;
  while (true) {
    UpdateHelperState(&helper_state);

    std::string desktop_name;
    bool session_locked = false;
    bool input_desktop_available = false;
    DWORD input_desktop_error = ERROR_SUCCESS;
    bool lock_app_visible = false;
    bool logon_ui_running = false;
    bool consent_ui_visible = false;
    bool secure_desktop_active = false;
    {
      std::lock_guard<std::mutex> lock(helper_state.mutex);
      desktop_name = helper_state.input_desktop_name;
      session_locked = helper_state.session_locked;
      input_desktop_available = helper_state.input_desktop_available;
      input_desktop_error = helper_state.input_desktop_error_code;
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
        input_desktop_available != last_input_desktop_available ||
        input_desktop_error != last_input_desktop_error ||
        session_locked != last_session_locked ||
        lock_app_visible != last_lock_app ||
        logon_ui_running != last_logon_ui ||
        consent_ui_visible != last_consent_ui ||
        secure_desktop_active != last_secure_desktop || stage != last_stage) {
      LOG_INFO(
          "Session helper state: session_id={}, input_desktop='{}', "
          "input_desktop_available={}, input_desktop_error={}, "
          "session_locked={}, lock_app_visible={}, logon_ui_running={}, "
          "consent_ui_visible={}, secure_desktop_active={}, stage={}",
          current_session_id, desktop_name, input_desktop_available,
          input_desktop_error, session_locked, lock_app_visible,
          logon_ui_running, consent_ui_visible, secure_desktop_active, stage);
      last_desktop_name = desktop_name;
      last_input_desktop_available = input_desktop_available;
      last_input_desktop_error = input_desktop_error;
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
