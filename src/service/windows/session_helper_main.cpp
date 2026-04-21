#include <TlHelp32.h>
#include <Windows.h>
#include <WtsApi32.h>
#include <libyuv.h>
#include <sddl.h>

#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "path_manager.h"
#include "rd_log.h"
#include "session_helper_shared.h"

namespace {

using crossdesk::get_logger;
using crossdesk::InitLogger;
using Json = nlohmann::json;

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
};

struct SecureMouseRequest {
  int x = 0;
  int y = 0;
  int wheel = 0;
  int flag = 0;
};

struct SecureCaptureBuffers {
  std::vector<uint8_t> nv12_frame;
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
  const bool secure_desktop_active =
      _stricmp(desktop_info.name.c_str(), "Winlogon") == 0;
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
  helper_state->secure_desktop_active = secure_desktop_active;
  helper_state->last_update_tick = GetTickCount64();
}

std::string BuildHelperStatusResponse(HelperState* helper_state) {
  if (helper_state == nullptr) {
    return BuildErrorJson("invalid_state");
  }

  Json json;
  std::lock_guard<std::mutex> lock(helper_state->mutex);
  const bool credential_ui_visible =
      helper_state->logon_ui_visible ||
      (helper_state->session_locked && !helper_state->input_desktop_available);
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

std::wstring GetCurrentThreadDesktopNameW() {
  HDESK desktop = GetThreadDesktop(GetCurrentThreadId());
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

  HDESK desktop = OpenDesktopW(desktop_name, 0, FALSE,
                               DESKTOP_CREATEWINDOW | DESKTOP_WRITEOBJECTS |
                                   DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
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

int InjectKeyboardInput(int key_code, bool is_down) {
  INPUT input = {0};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = static_cast<WORD>(key_code);

  const UINT scan_code =
      MapVirtualKeyW(static_cast<UINT>(key_code), MAPVK_VK_TO_VSC_EX);
  if (scan_code != 0) {
    input.ki.wVk = 0;
    input.ki.wScan = static_cast<WORD>(scan_code & 0xFF);
    input.ki.dwFlags |= KEYEVENTF_SCANCODE;
    if ((scan_code & 0xFF00) != 0) {
      input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
  }

  if (!is_down) {
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
  }

  UINT sent = SendInput(1, &input, sizeof(INPUT));
  if (sent != 1) {
    return static_cast<int>(GetLastError());
  }

  return 0;
}

bool ParseSecureInputKeyboardCommand(const std::string& command,
                                     int* key_code_out, bool* is_down_out) {
  if (key_code_out == nullptr || is_down_out == nullptr) {
    return false;
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

  const std::string state = command.substr(separator + 1);
  if (state == "1" || state == "down") {
    *is_down_out = true;
    return true;
  }
  if (state == "0" || state == "up") {
    *is_down_out = false;
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
    request_out->flag = std::stoi(command.substr(separator + 1));
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

  const size_t values_begin =
      std::strlen(crossdesk::kCrossDeskSecureInputCaptureCommandPrefix);
  int parsed_values[5] = {0};
  size_t token_begin = values_begin;
  for (int index = 0; index < 5; ++index) {
    const size_t separator = command.find(':', token_begin);
    const bool is_last = index == 4;
    const size_t token_end = is_last ? command.size() : separator;
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
  return request_out->width > 0 && request_out->height > 0;
}

int InjectMouseInput(const SecureMouseRequest& request) {
  SetCursorPos(request.x, request.y);

  INPUT input = {0};
  input.type = INPUT_MOUSE;
  switch (request.flag) {
    case 1:
      input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
      break;
    case 2:
      input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
      break;
    case 3:
      input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
      break;
    case 4:
      input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
      break;
    case 5:
      input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
      break;
    case 6:
      input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
      break;
    case 7:
      input.mi.dwFlags = MOUSEEVENTF_WHEEL;
      input.mi.mouseData = request.wheel * 120;
      break;
    case 8:
      input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
      input.mi.mouseData = request.wheel * 120;
      break;
    default:
      input.mi.dwFlags = 0;
      break;
  }

  if (input.mi.dwFlags == 0) {
    return 0;
  }

  UINT sent = SendInput(1, &input, sizeof(INPUT));
  if (sent != 1) {
    return static_cast<int>(GetLastError());
  }

  return 0;
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

std::vector<uint8_t> HandleSecureInputHelperCommand(
    const std::string& command, SecureCaptureBuffers* capture_buffers) {
  if (command == "ping") {
    return BuildTextResponseBytes("{\"ok\":true,\"reply\":\"pong\"}");
  }

  int key_code = 0;
  bool is_down = false;
  if (ParseSecureInputKeyboardCommand(command, &key_code, &is_down)) {
    const int inject_result = InjectKeyboardInput(key_code, is_down);
    if (inject_result != 0) {
      LOG_WARN(
          "Secure input helper SendInput failed for key_code={}, is_down={}, "
          "err={}",
          key_code, is_down, inject_result);
      return BuildTextResponseBytes(BuildErrorJson(
          "send_input_failed", static_cast<DWORD>(inject_result)));
    }

    Json json;
    json["ok"] = true;
    json["injected"] = "keyboard";
    json["key_code"] = key_code;
    json["is_down"] = is_down;
    json["desktop"] = WideToUtf8(GetCurrentThreadDesktopNameW());
    return BuildTextResponseBytes(json.dump());
  }

  SecureMouseRequest mouse_request;
  if (ParseSecureInputMouseCommand(command, &mouse_request)) {
    const int inject_result = InjectMouseInput(mouse_request);
    if (inject_result != 0) {
      LOG_WARN(
          "Secure input helper SendInput failed for mouse x={}, y={}, "
          "wheel={}, flag={}, err={}",
          mouse_request.x, mouse_request.y, mouse_request.wheel,
          mouse_request.flag, inject_result);
      return BuildTextResponseBytes(BuildErrorJson(
          "send_input_failed", static_cast<DWORD>(inject_result)));
    }

    Json json;
    json["ok"] = true;
    json["injected"] = "mouse";
    json["x"] = mouse_request.x;
    json["y"] = mouse_request.y;
    json["wheel"] = mouse_request.wheel;
    json["flag"] = mouse_request.flag;
    json["desktop"] = WideToUtf8(GetCurrentThreadDesktopNameW());
    return BuildTextResponseBytes(json.dump());
  }

  SecureCaptureRequest capture_request;
  if (ParseSecureInputCaptureCommand(command, &capture_request)) {
    return CaptureSecureDesktopFrame(capture_request, capture_buffers);
  }

  return BuildTextResponseBytes(BuildErrorJson("unknown_command"));
}

void HandleSecureInputHelperPipeClient(HANDLE pipe, HANDLE event_handle) {
  SecureCaptureBuffers capture_buffers;
  char buffer[1024] = {0};
  DWORD bytes_read = 0;
  if (ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) &&
      bytes_read > 0) {
    std::vector<uint8_t> response = HandleSecureInputHelperCommand(
        std::string(buffer, buffer + bytes_read), &capture_buffers);
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

    std::thread(HandleSecureInputHelperPipeClient, pipe, overlapped.hEvent)
        .detach();
  }
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

    HDESK secure_desktop = nullptr;
    if (!EnsureThreadDesktop(L"Winlogon", &secure_desktop)) {
      LOG_ERROR(
          "Failed to switch secure input helper to Winlogon desktop, error={}",
          GetLastError());
      if (stop_event != nullptr) {
        CloseHandle(stop_event);
      }
      return 1;
    }

    LOG_INFO("Secure input helper desktop: '{}'",
             WideToUtf8(GetCurrentThreadDesktopNameW()));
    SecureInputHelperIpcServerLoop(stop_event, current_session_id);

    if (secure_desktop != nullptr) {
      CloseDesktop(secure_desktop);
    }
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

  std::thread ipc_thread(HelperIpcServerLoop, stop_event, current_session_id,
                         &helper_state);

  std::string last_desktop_name;
  bool last_lock_app = false;
  bool last_logon_ui = false;
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
    bool secure_desktop_active = false;
    {
      std::lock_guard<std::mutex> lock(helper_state.mutex);
      desktop_name = helper_state.input_desktop_name;
      session_locked = helper_state.session_locked;
      input_desktop_available = helper_state.input_desktop_available;
      lock_app_visible = helper_state.lock_app_visible;
      logon_ui_running = helper_state.logon_ui_visible;
      secure_desktop_active = helper_state.secure_desktop_active;
    }
    const bool credential_ui_visible =
        logon_ui_running || (session_locked && !input_desktop_available);
    std::string stage = DetermineInteractiveStage(
        lock_app_visible, credential_ui_visible, secure_desktop_active);

    if (desktop_name != last_desktop_name ||
        session_locked != last_session_locked ||
        lock_app_visible != last_lock_app ||
        logon_ui_running != last_logon_ui ||
        secure_desktop_active != last_secure_desktop || stage != last_stage) {
      LOG_INFO(
          "Session helper state: session_id={}, input_desktop='{}', "
          "session_locked={}, lock_app_visible={}, logon_ui_running={}, "
          "secure_desktop_active={}, stage={}",
          current_session_id, desktop_name, session_locked, lock_app_visible,
          logon_ui_running, secure_desktop_active, stage);
      last_desktop_name = desktop_name;
      last_session_locked = session_locked;
      last_lock_app = lock_app_visible;
      last_logon_ui = logon_ui_running;
      last_secure_desktop = secure_desktop_active;
      last_stage = stage;
    }

    DWORD wait_result = stop_event != nullptr
                            ? WaitForSingleObject(stop_event, 1000)
                            : WAIT_TIMEOUT;
    if (wait_result == WAIT_OBJECT_0) {
      break;
    }
    if (wait_result != WAIT_TIMEOUT && wait_result != WAIT_FAILED) {
      break;
    }
  }

  if (ipc_thread.joinable()) {
    ipc_thread.join();
  }

  if (stop_event != nullptr) {
    CloseHandle(stop_event);
  }

  LOG_INFO("Session helper exiting: session_id={}", current_session_id);
  return 0;
}