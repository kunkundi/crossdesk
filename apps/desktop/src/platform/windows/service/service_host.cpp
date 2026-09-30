#include "service_host.h"

#include <Aclapi.h>
#include <TlHelp32.h>
#include <Userenv.h>
#include <WtsApi32.h>
#include <sddl.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>

#include "interactive_state.h"
#include "interactive_desktop.h"
#include "desktop_transition_policy.h"
#include "named_pipe_deadline.h"
#include "path_manager.h"
#include "rd_log.h"
#include "session_helper_shared.h"
#include "unattended_config.h"
#include "unattended_policy.h"

namespace crossdesk {

namespace {

using Json = nlohmann::json;

constexpr char kSecureDesktopKeyboardIpcCommandPrefix[] = "secure-input-key:";
constexpr char kSecureDesktopMouseIpcCommandPrefix[] = "secure-input-mouse:";
constexpr wchar_t kCrossDeskClientProcessName[] = L"crossdesk.exe";
constexpr DWORD kCrossDeskClientMonitorIntervalMs = 1000;
constexpr ULONGLONG kCrossDeskClientMonitorStartupGraceMs = 5000;
constexpr ULONGLONG kSasSecureDesktopGraceMs = 15000;
constexpr ULONGLONG kUserDesktopInputIdleMs = 5000;

using SendSasFunction = VOID(WINAPI*)(BOOL);

struct SasResult {
  bool success = false;
  DWORD error_code = 0;
  std::string error;
};

struct InputDesktopInfo {
  bool available = false;
  DWORD error_code = 0;
  std::string name;
};

struct SecureDesktopMouseRequest {
  int x = 0;
  int y = 0;
  int wheel = 0;
  int flag = 0;
};

struct ScopedEnvironmentBlock {
  ~ScopedEnvironmentBlock() {
    if (environment != nullptr) {
      DestroyEnvironmentBlock(environment);
    }
  }

  LPVOID environment = nullptr;
};

std::wstring Utf8ToWide(const std::string& value);
std::string WideToUtf8(const std::wstring& value);

std::wstring GetCurrentExecutablePathW() {
  wchar_t path[MAX_PATH] = {0};
  DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return L"";
  }
  return std::wstring(path, length);
}

std::wstring QuoteWindowsArgument(const std::wstring& value) {
  std::wstring escaped = L"\"";
  for (wchar_t character : value) {
    if (character == L'\"') {
      escaped += L"\\\"";
      continue;
    }
    escaped += character;
  }
  escaped += L"\"";
  return escaped;
}

void InitializeServiceLogger() {
  static std::once_flag once_flag;
  std::call_once(once_flag, []() {
    PathManager path_manager("CrossDesk");
    std::filesystem::path log_path = path_manager.GetLogPath() / "service";
    if (!log_path.empty() && path_manager.CreateDirectories(log_path)) {
      InitLogger(log_path.string());
      return;
    }
    InitLogger("logs/service");
  });
}

std::string EscapeJsonString(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char character : value) {
    switch (character) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += character;
        break;
    }
  }
  return escaped;
}

std::string Trim(const std::string& value) {
  size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }

  size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }

  return value.substr(begin, end - begin);
}

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

std::string BuildErrorJson(const char* error, DWORD error_code = 0) {
  std::ostringstream stream;
  stream << "{\"ok\":false,\"error\":\"" << error << "\"";
  if (error_code != 0) {
    stream << ",\"code\":" << error_code;
  }
  stream << "}";
  return stream.str();
}

bool HasRunningCrossDeskClientProcess() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    LOG_ERROR("CreateToolhelp32Snapshot failed, error={}", GetLastError());
    return true;
  }

  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Process32FirstW(snapshot, &entry)) {
    DWORD error = GetLastError();
    CloseHandle(snapshot);
    if (error != ERROR_NO_MORE_FILES) {
      LOG_ERROR("Process32FirstW failed, error={}", error);
      return true;
    }
    return false;
  }

  do {
    if (_wcsicmp(entry.szExeFile, kCrossDeskClientProcessName) == 0) {
      CloseHandle(snapshot);
      return true;
    }
  } while (Process32NextW(snapshot, &entry));

  DWORD error = GetLastError();
  CloseHandle(snapshot);
  if (error != ERROR_NO_MORE_FILES) {
    LOG_ERROR("Process32NextW failed, error={}", error);
    return true;
  }

  return false;
}

bool GrantCrossDeskServiceStartAccessToAuthenticatedUsers(SC_HANDLE service) {
  if (service == nullptr) {
    return false;
  }

  PACL existing_dacl = nullptr;
  PACL updated_dacl = nullptr;
  PSECURITY_DESCRIPTOR security_descriptor = nullptr;
  DWORD error =
      GetSecurityInfo(service, SE_SERVICE, DACL_SECURITY_INFORMATION, nullptr,
                      nullptr, &existing_dacl, nullptr, &security_descriptor);
  if (error != ERROR_SUCCESS) {
    LOG_ERROR("GetSecurityInfo failed, error={}", error);
    return false;
  }

  BYTE sid_buffer[SECURITY_MAX_SID_SIZE] = {0};
  DWORD sid_size = sizeof(sid_buffer);
  if (!CreateWellKnownSid(WinAuthenticatedUserSid, nullptr, sid_buffer,
                          &sid_size)) {
    error = GetLastError();
    LOG_ERROR("CreateWellKnownSid failed, error={}", error);
    LocalFree(security_descriptor);
    return false;
  }

  EXPLICIT_ACCESSW access{};
  access.grfAccessPermissions = SERVICE_START | SERVICE_QUERY_STATUS;
  access.grfAccessMode = SET_ACCESS;
  access.grfInheritance = NO_INHERITANCE;
  access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid_buffer);

  error = SetEntriesInAclW(1, &access, existing_dacl, &updated_dacl);
  if (error != ERROR_SUCCESS) {
    LOG_ERROR("SetEntriesInAclW failed, error={}", error);
    LocalFree(security_descriptor);
    return false;
  }

  error = SetSecurityInfo(service, SE_SERVICE, DACL_SECURITY_INFORMATION,
                          nullptr, nullptr, updated_dacl, nullptr);
  if (error != ERROR_SUCCESS) {
    LOG_ERROR("SetSecurityInfo failed, error={}", error);
    LocalFree(updated_dacl);
    LocalFree(security_descriptor);
    return false;
  }

  LocalFree(updated_dacl);
  LocalFree(security_descriptor);
  return true;
}

std::string QueryNamedPipeMessage(const std::wstring& pipe_name,
                                  const std::string& command,
                                  DWORD timeout_ms) {
  std::vector<uint8_t> response;
  std::string error;
  DWORD code = ERROR_SUCCESS;
  if (!QueryNamedPipeWithDeadline(pipe_name, command, timeout_ms,
                                  &response, &error, &code)) {
    return BuildErrorJson(error.c_str(), code);
  }
  return std::string(response.begin(), response.end());
}

std::string BuildSecureDesktopKeyboardIpcCommand(int key_code, bool is_down,
                                                 uint32_t scan_code,
                                                 bool extended) {
  std::ostringstream stream;
  stream << kSecureDesktopKeyboardIpcCommandPrefix << key_code << ":"
         << (is_down ? 1 : 0) << ":" << scan_code << ":" << (extended ? 1 : 0);
  return stream.str();
}

std::string BuildSecureDesktopMouseIpcCommand(int x, int y, int wheel,
                                              int flag) {
  std::ostringstream stream;
  stream << kSecureDesktopMouseIpcCommandPrefix << x << ":" << y << ":" << wheel
         << ":" << flag;
  return stream.str();
}

std::string BuildSecureInputHelperKeyboardCommand(
    int key_code, bool is_down, uint32_t scan_code, bool extended,
    const std::string& interactive_stage,
    const std::string& interactive_desktop) {
  std::ostringstream stream;
  stream << kCrossDeskSecureInputKeyboardCommandPrefix << key_code << ":"
         << (is_down ? 1 : 0) << ":" << scan_code << ":" << (extended ? 1 : 0);
  if (!interactive_stage.empty()) {
    stream << ":" << interactive_stage;
    if (!interactive_desktop.empty()) {
      stream << ":" << interactive_desktop;
    }
  }
  return stream.str();
}

std::string BuildSecureInputHelperMouseCommand(
    int x, int y, int wheel, int flag,
    const std::string& interactive_stage,
    const std::string& interactive_desktop) {
  std::ostringstream stream;
  stream << kCrossDeskSecureInputMouseCommandPrefix << x << ":" << y << ":"
         << wheel << ":" << flag;
  if (!interactive_stage.empty()) {
    stream << ":" << interactive_stage;
    if (!interactive_desktop.empty()) {
      stream << ":" << interactive_desktop;
    }
  }
  return stream.str();
}

bool ParseSecureDesktopKeyboardIpcCommand(const std::string& command,
                                          int* key_code_out, bool* is_down_out,
                                          uint32_t* scan_code_out,
                                          bool* extended_out) {
  if (key_code_out == nullptr || is_down_out == nullptr ||
      scan_code_out == nullptr || extended_out == nullptr) {
    return false;
  }

  *scan_code_out = 0;
  *extended_out = false;

  if (command.rfind(kSecureDesktopKeyboardIpcCommandPrefix, 0) != 0) {
    return false;
  }

  const size_t key_begin = sizeof(kSecureDesktopKeyboardIpcCommandPrefix) - 1;
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

  const std::string extended_str = command.substr(extended_separator + 1);
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

bool ParseSecureDesktopMouseIpcCommand(const std::string& command,
                                       SecureDesktopMouseRequest* request_out) {
  if (request_out == nullptr) {
    return false;
  }

  if (command.rfind(kSecureDesktopMouseIpcCommandPrefix, 0) != 0) {
    return false;
  }

  const size_t x_begin = sizeof(kSecureDesktopMouseIpcCommandPrefix) - 1;
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

bool CreateSessionSystemToken(DWORD session_id, HANDLE* token_out,
                              DWORD* error_code_out = nullptr) {
  if (token_out == nullptr) {
    return false;
  }

  *token_out = nullptr;
  if (error_code_out != nullptr) {
    *error_code_out = 0;
  }

  HANDLE process_token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(),
                        TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY |
                            TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                        &process_token)) {
    if (error_code_out != nullptr) {
      *error_code_out = GetLastError();
    }
    return false;
  }

  HANDLE primary_token = nullptr;
  BOOL duplicated = DuplicateTokenEx(
      process_token,
      TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY |
          TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
      nullptr, SecurityImpersonation, TokenPrimary, &primary_token);
  CloseHandle(process_token);
  if (!duplicated) {
    if (error_code_out != nullptr) {
      *error_code_out = GetLastError();
    }
    return false;
  }

  if (!SetTokenInformation(primary_token, TokenSessionId, &session_id,
                           sizeof(session_id))) {
    if (error_code_out != nullptr) {
      *error_code_out = GetLastError();
    }
    CloseHandle(primary_token);
    return false;
  }

  *token_out = primary_token;
  return true;
}

const char* SessionEventToString(DWORD event_type) {
  switch (event_type) {
    case WTS_CONSOLE_CONNECT:
      return "console-connect";
    case WTS_CONSOLE_DISCONNECT:
      return "console-disconnect";
    case WTS_REMOTE_CONNECT:
      return "remote-connect";
    case WTS_REMOTE_DISCONNECT:
      return "remote-disconnect";
    case WTS_SESSION_LOGON:
      return "session-logon";
    case WTS_SESSION_LOGOFF:
      return "session-logoff";
    case WTS_SESSION_LOCK:
      return "session-lock";
    case WTS_SESSION_UNLOCK:
      return "session-unlock";
    default:
      return "unknown";
  }
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

bool IsCredentialUiVisible(bool prelogin, bool session_locked,
                           bool logon_ui_running, bool consent_ui_visible,
                           bool input_desktop_available,
                           bool secure_desktop_active) {
  return consent_ui_visible ||
         ((prelogin || session_locked || secure_desktop_active) &&
          (logon_ui_running || !input_desktop_available));
}

std::wstring SecureInputHelperDesktopForStage(
    const std::string& interactive_stage,
    const std::string& interactive_desktop) {
  if (!interactive_desktop.empty()) {
    std::wstring interactive_desktop_w = Utf8ToWide(interactive_desktop);
    if (!interactive_desktop_w.empty()) {
      return L"winsta0\\" + interactive_desktop_w;
    }
  }
  if (interactive_stage == "credential-ui" ||
      interactive_stage == "secure-desktop") {
    return L"winsta0\\Winlogon";
  }
  return L"winsta0\\default";
}

bool GetSessionUserName(DWORD session_id, std::wstring* username_out) {
  if (username_out == nullptr) {
    return false;
  }

  username_out->clear();
  LPWSTR username = nullptr;
  DWORD bytes = 0;
  if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session_id,
                                   WTSUserName, &username, &bytes)) {
    return false;
  }

  if (username != nullptr) {
    *username_out = username;
    WTSFreeMemory(username);
    return true;
  }

  return false;
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

bool FindProcessInSession(const wchar_t* executable_name, DWORD session_id,
                          DWORD* process_id_out) {
  if (executable_name == nullptr || executable_name[0] == L'\0') {
    return false;
  }

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
        if (process_id_out != nullptr) {
          *process_id_out = entry.th32ProcessID;
        }
        found = true;
        break;
      }
    } while (Process32NextW(snapshot, &entry));
  }

  CloseHandle(snapshot);
  return found;
}

bool IsProcessRunningInSession(const wchar_t* executable_name,
                               DWORD session_id) {
  return FindProcessInSession(executable_name, session_id, nullptr);
}

bool IsLogonUiRunningInSession(DWORD session_id) {
  return IsProcessRunningInSession(L"LogonUI.exe", session_id);
}

bool IsConsentUiRunningInSession(DWORD session_id) {
  return IsProcessRunningInSession(L"Consent.exe", session_id);
}

// The service runs as SYSTEM and could terminate any process, so a name match
// alone is not enough: a user-named `Consent.exe` in their own session must not
// become killable through the remote control channel.
bool IsProcessInWindowsSystemDirectory(DWORD process_id) {
  HANDLE process =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
  if (process == nullptr) {
    return false;
  }

  wchar_t image_path[MAX_PATH]{};
  DWORD path_length = MAX_PATH;
  const bool resolved =
      QueryFullProcessImageNameW(process, 0, image_path, &path_length) != FALSE;
  CloseHandle(process);
  if (!resolved) {
    return false;
  }

  wchar_t windows_directory[MAX_PATH]{};
  if (GetWindowsDirectoryW(windows_directory, MAX_PATH) == 0) {
    return false;
  }
  const std::wstring prefixes[] = {
      std::wstring(windows_directory) + L"\\system32\\",
      std::wstring(windows_directory) + L"\\sysnative\\",
      std::wstring(windows_directory) + L"\\syswow64\\",
  };
  for (const auto& prefix : prefixes) {
    if (_wcsnicmp(image_path, prefix.c_str(),
                  static_cast<size_t>(prefix.size())) == 0) {
      return true;
    }
  }
  return false;
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

bool QuerySoftwareSasGeneration(DWORD* value_out, bool* existed_out) {
  if (value_out == nullptr || existed_out == nullptr) {
    return false;
  }

  *value_out = 0;
  *existed_out = false;
  HKEY key = nullptr;
  constexpr wchar_t kPolicyKey[] =
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kPolicyKey, 0,
                    KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
    return false;
  }

  DWORD value = 0;
  DWORD value_size = sizeof(value);
  DWORD type = REG_DWORD;
  LONG result = RegQueryValueExW(key, L"SoftwareSASGeneration", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(&value), &value_size);
  if (result == ERROR_SUCCESS && type == REG_DWORD) {
    *value_out = value;
    *existed_out = true;
  }

  RegCloseKey(key);
  return true;
}

bool SetSoftwareSasGeneration(DWORD value) {
  HKEY key = nullptr;
  constexpr wchar_t kPolicyKey[] =
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
  DWORD disposition = 0;
  LONG result = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kPolicyKey, 0, nullptr, 0,
                                KEY_SET_VALUE, nullptr, &key, &disposition);
  if (result != ERROR_SUCCESS) {
    return false;
  }

  result = RegSetValueExW(key, L"SoftwareSASGeneration", 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&value), sizeof(value));
  RegCloseKey(key);
  return result == ERROR_SUCCESS;
}

bool RestoreSoftwareSasGeneration(DWORD original_value, bool existed_before) {
  HKEY key = nullptr;
  constexpr wchar_t kPolicyKey[] =
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
  LONG result =
      RegOpenKeyExW(HKEY_LOCAL_MACHINE, kPolicyKey, 0, KEY_SET_VALUE, &key);
  if (result != ERROR_SUCCESS) {
    return false;
  }

  if (!existed_before) {
    result = RegDeleteValueW(key, L"SoftwareSASGeneration");
    RegCloseKey(key);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
  }

  result = RegSetValueExW(key, L"SoftwareSASGeneration", 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&original_value),
                          sizeof(original_value));
  RegCloseKey(key);
  return result == ERROR_SUCCESS;
}

SasResult SendSasNow() {
  SasResult result;
  HMODULE sas_module = LoadLibraryW(L"sas.dll");
  if (sas_module == nullptr) {
    result.error = "sas_dll_unavailable";
    result.error_code = GetLastError();
    return result;
  }

  auto* send_sas =
      reinterpret_cast<SendSasFunction>(GetProcAddress(sas_module, "SendSAS"));
  if (send_sas == nullptr) {
    result.error = "send_sas_proc_missing";
    result.error_code = GetLastError();
    FreeLibrary(sas_module);
    return result;
  }

  DWORD original_value = 0;
  bool existed_before = false;
  bool queried = QuerySoftwareSasGeneration(&original_value, &existed_before);
  bool mutated_policy = false;
  if (queried) {
    if (!existed_before || (original_value != 1 && original_value != 3)) {
      if (!SetSoftwareSasGeneration(1)) {
        result.error = "set_software_sas_generation_failed";
        result.error_code = GetLastError();
        FreeLibrary(sas_module);
        return result;
      }
      mutated_policy = true;
    }
  }

  send_sas(FALSE);

  if (mutated_policy) {
    RestoreSoftwareSasGeneration(original_value, existed_before);
  }

  FreeLibrary(sas_module);
  result.success = true;
  return result;
}

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

struct KernelObjectSecurityAttributes {
  KernelObjectSecurityAttributes() = default;
  ~KernelObjectSecurityAttributes() {
    if (security_descriptor_ != nullptr) {
      LocalFree(security_descriptor_);
    }
  }

  bool Initialize() {
    constexpr wchar_t kObjectSddl[] = L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;AU)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kObjectSddl, SDDL_REVISION_1, &security_descriptor_, nullptr)) {
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

}  // namespace

CrossDeskServiceHost* CrossDeskServiceHost::instance_ = nullptr;

CrossDeskServiceHost::CrossDeskServiceHost() { InitializeServiceLogger(); }

CrossDeskServiceHost::~CrossDeskServiceHost() { ShutdownRuntime(); }

void WINAPI CrossDeskServiceHost::ServiceMain([[maybe_unused]] DWORD argc,
                                              [[maybe_unused]] LPWSTR* argv) {
  if (instance_ != nullptr) {
    instance_->RunServiceLoop(true);
  }
}

BOOL WINAPI CrossDeskServiceHost::ConsoleControlHandler(DWORD control_type) {
  if (instance_ == nullptr) {
    return FALSE;
  }

  switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      instance_->RequestStop();
      return TRUE;
    default:
      return FALSE;
  }
}

DWORD WINAPI CrossDeskServiceHost::ServiceControlHandler(
    DWORD control, DWORD event_type, LPVOID event_data,
    [[maybe_unused]] LPVOID context) {
  if (instance_ == nullptr) {
    return ERROR_CALL_NOT_IMPLEMENTED;
  }

  switch (control) {
    case SERVICE_CONTROL_INTERROGATE:
      instance_->ReportServiceStatus(instance_->service_status_.dwCurrentState,
                                     NO_ERROR, 0);
      return NO_ERROR;
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
      instance_->ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 20000);
      instance_->RequestStop();
      return NO_ERROR;
    case SERVICE_CONTROL_SESSIONCHANGE: {
      DWORD session_id = 0xFFFFFFFF;
      if (event_data != nullptr) {
        auto* session = reinterpret_cast<WTSSESSION_NOTIFICATION*>(event_data);
        session_id = session->dwSessionId;
      }
      instance_->RecordSessionEvent(event_type, session_id);
      return NO_ERROR;
    }
    default:
      return ERROR_CALL_NOT_IMPLEMENTED;
  }
}

int CrossDeskServiceHost::RunAsService() {
  instance_ = this;

  SERVICE_TABLE_ENTRYW service_table[] = {
      {const_cast<LPWSTR>(kCrossDeskServiceName),
       &CrossDeskServiceHost::ServiceMain},
      {nullptr, nullptr}};

  if (!StartServiceCtrlDispatcherW(service_table)) {
    DWORD error = GetLastError();
    LOG_ERROR("StartServiceCtrlDispatcherW failed, error={}", error);
    return static_cast<int>(error);
  }

  return 0;
}

int CrossDeskServiceHost::RunInConsole() {
  instance_ = this;
  return RunServiceLoop(false);
}

int CrossDeskServiceHost::InitializeRuntime() {
  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (stop_event_ == nullptr) {
    DWORD error = GetLastError();
    LOG_ERROR("CreateEventW failed, error={}", error);
    return static_cast<int>(error);
  }

  GUID lifetime_id{};
  wchar_t lifetime_guid[40]{};
  if (FAILED(CoCreateGuid(&lifetime_id)) ||
      !StringFromGUID2(lifetime_id, lifetime_guid, 40))
    return ERROR_GEN_FAILURE;
  lifetime_mutex_name_ =
      L"Global\\CrossDeskServiceLifetime-" + std::wstring(lifetime_guid);
  PSECURITY_DESCRIPTOR lifetime_descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00100000;;;AU)", SDDL_REVISION_1,
          &lifetime_descriptor, nullptr))
    return static_cast<int>(GetLastError());
  SECURITY_ATTRIBUTES lifetime_attributes{sizeof(SECURITY_ATTRIBUTES),
                                          lifetime_descriptor, FALSE};
  // Owned by this service-main thread until every child has been stopped.
  lifetime_mutex_ =
      CreateMutexW(&lifetime_attributes, TRUE, lifetime_mutex_name_.c_str());
  const DWORD lifetime_error = GetLastError();
  LocalFree(lifetime_descriptor);
  if (!lifetime_mutex_ || lifetime_error == ERROR_ALREADY_EXISTS)
    return ERROR_INVALID_HANDLE;

  started_at_tick_ = GetTickCount64();
  session_state_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!session_state_event_) return static_cast<int>(GetLastError());
  last_sas_tick_ = 0;
  active_session_id_ = WTSGetActiveConsoleSessionId();
  process_session_id_ = 0xFFFFFFFF;
  input_desktop_error_code_ = 0;
  session_helper_process_id_ = 0;
  session_helper_session_id_ = 0xFFFFFFFF;
  session_helper_exit_code_ = 0;
  session_helper_last_error_code_ = 0;
  session_helper_status_error_code_ = 0;
  session_helper_report_session_id_ = 0xFFFFFFFF;
  session_helper_report_process_id_ = 0;
  session_helper_report_input_desktop_error_code_ = 0;
  secure_input_helper_process_id_ = 0;
  secure_input_helper_session_id_ = 0xFFFFFFFF;
  secure_input_helper_exit_code_ = 0;
  secure_input_helper_last_error_code_ = 0;
  session_locked_ = false;
  logon_ui_visible_ = false;
  consent_ui_visible_ = false;
  prelogin_ = false;
  secure_desktop_active_ = false;
  input_desktop_available_ = false;
  session_helper_running_ = false;
  session_helper_status_ok_ = false;
  session_helper_report_input_desktop_available_ = false;
  session_helper_report_lock_app_visible_ = false;
  session_helper_report_logon_ui_visible_ = false;
  session_helper_report_consent_ui_visible_ = false;
  session_helper_report_secure_desktop_active_ = false;
  session_helper_report_credential_ui_visible_ = false;
  session_helper_report_unlock_ui_visible_ = false;
  secure_input_helper_running_ = false;
  sas_secure_desktop_seen_ = false;
  last_sas_error_code_ = 0;
  last_sas_success_ = false;
  session_helper_started_at_tick_ = 0;
  session_helper_report_state_age_ms_ = 0;
  session_helper_report_uptime_ms_ = 0;
  secure_input_helper_started_at_tick_ = 0;
  user_desktop_input_until_tick_ = 0;
  sas_secure_desktop_until_tick_ = 0;
  session_helper_process_handle_ = nullptr;
  session_helper_stop_event_ = nullptr;
  secure_input_helper_process_handle_ = nullptr;
  secure_input_helper_stop_event_ = nullptr;
  input_desktop_name_.clear();
  last_sas_error_.clear();
  session_helper_last_error_.clear();
  session_helper_status_error_.clear();
  session_helper_report_input_desktop_.clear();
  session_helper_report_interactive_stage_.clear();
  secure_input_helper_last_error_.clear();
  secure_input_helper_interactive_stage_.clear();
  secure_input_helper_interactive_desktop_.clear();
  last_session_event_type_ = 0;
  last_session_event_session_id_ = active_session_id_;
  RefreshSessionState();
  EnsureSessionHelper();
  session_state_thread_ = std::thread(&CrossDeskServiceHost::SessionStateLoop, this);
  ipc_thread_ = std::thread(&CrossDeskServiceHost::IpcServerLoop, this);
  if (!console_mode_) {
    client_process_monitor_thread_ =
        std::thread(&CrossDeskServiceHost::ClientProcessMonitorLoop, this);
  }
  LOG_INFO("CrossDesk service runtime initialized, session_id={}",
           active_session_id_);
  return 0;
}

void CrossDeskServiceHost::ShutdownRuntime() {
  if (stop_event_ != nullptr) {
    SetEvent(stop_event_);
  }

  if (client_process_monitor_thread_.joinable()) {
    client_process_monitor_thread_.join();
  }
  StopUnattendedHost();

  if (ipc_thread_.joinable()) {
    ipc_thread_.join();
  }
  if (session_state_thread_.joinable()) session_state_thread_.join();
  StopSecureInputHelper();
  StopSessionHelper();
  if (session_state_event_) {
    CloseHandle(session_state_event_);
    session_state_event_ = nullptr;
  }
  if (lifetime_mutex_) {
    ReleaseMutex(lifetime_mutex_);
    CloseHandle(lifetime_mutex_);
    lifetime_mutex_ = nullptr;
  }

  if (stop_event_ != nullptr) {
    CloseHandle(stop_event_);
    stop_event_ = nullptr;
  }
}

void CrossDeskServiceHost::RequestStop() {
  if (stop_event_ != nullptr) {
    SetEvent(stop_event_);
  }
}

void CrossDeskServiceHost::ClientProcessMonitorLoop() {
  const ULONGLONG monitor_started_at = GetTickCount64();
  uint64_t logoff_generation = unattended_logoff_generation_.load();

  while (stop_event_ != nullptr) {
    DWORD wait_result =
        WaitForSingleObject(stop_event_, kCrossDeskClientMonitorIntervalMs);
    if (wait_result == WAIT_OBJECT_0) {
      return;
    }
    if (wait_result != WAIT_TIMEOUT) {
      continue;
    }

    const bool enabled = IsUnattendedEnabled();
    const uint64_t generation = unattended_logoff_generation_.load();
    if (generation != logoff_generation) {
      logoff_generation = generation;
      StopUnattendedHost();
      // Windows may destroy/recreate Default during logoff, even when the
      // numeric console session ID does not change. Rebind on the next tick.
      continue;
    }
    if (unattended_process_ &&
        WaitForSingleObject(unattended_process_, 0) != WAIT_TIMEOUT) {
      StopUnattendedHost();
    }
    const DWORD console_session = WTSGetActiveConsoleSessionId();
    const auto action =
        UnattendedAction(enabled, console_session,
                         unattended_process_ != nullptr, unattended_session_);
    if (action == UnattendedProcessAction::stop) StopUnattendedHost();
    if (action == UnattendedProcessAction::start) {
      if (!LaunchUnattendedHost(console_session)) {
        // Boot can precede creation of WinSta0 or network availability.
        WaitForSingleObject(stop_event_, 4000);
      }
    }
    // An enabled host is independent of the GUI, including while Windows is
    // between console sessions or the host is retrying startup.
    if (enabled) continue;

    if (GetTickCount64() - monitor_started_at <
        kCrossDeskClientMonitorStartupGraceMs) {
      continue;
    }

    if (HasRunningCrossDeskClientProcess()) {
      continue;
    }

    LOG_INFO("No crossdesk client process detected, stopping service");
    RequestStop();
    return;
  }
}

bool CrossDeskServiceHost::LaunchUnattendedHost(DWORD session_id) {
  if (!IsLocalSystemProcess() || !PrepareUnattendedDirectory(false)) {
    LOG_ERROR("Unattended host requires SYSTEM and a private machine profile");
    return false;
  }
  const auto executable =
      std::filesystem::path(GetCurrentExecutablePathW()).parent_path() /
      L"CrossDesk.exe";
  GUID nonce{};
  wchar_t guid[40]{};
  if (FAILED(CoCreateGuid(&nonce)) || !StringFromGUID2(nonce, guid, 40))
    return false;
  const std::wstring event_name =
      L"Global\\CrossDeskUnattendedStop-" + std::wstring(guid);
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:P(A;;GA;;;SY)", SDDL_REVISION_1, &descriptor, nullptr))
    return false;
  SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor,
                                 FALSE};
  HANDLE event = CreateEventW(&attributes, TRUE, FALSE, event_name.c_str());
  const DWORD event_error = GetLastError();
  LocalFree(descriptor);
  if (!event || event_error == ERROR_ALREADY_EXISTS) {
    if (event) CloseHandle(event);
    return false;
  }
  HANDLE token = nullptr;
  DWORD error = 0;
  ScopedEnvironmentBlock environment;
  PROCESS_INFORMATION process{};
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  wchar_t desktop[] = L"winsta0\\default";
  startup.lpDesktop = desktop;
  std::wstring command = QuoteWindowsArgument(executable.wstring()) +
                         L" --unattended-host " +
                         QuoteWindowsArgument(event_name) + L" " +
                         QuoteWindowsArgument(lifetime_mutex_name_);
  bool created = false;
  if (CreateSessionSystemToken(session_id, &token, &error)) {
    if (CreateEnvironmentBlock(&environment.environment, token, FALSE)) {
      created = CreateProcessAsUserW(
                    token, executable.c_str(), command.data(), nullptr, nullptr,
                    FALSE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                    environment.environment, executable.parent_path().c_str(),
                    &startup, &process) != FALSE;
    }
    error = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(token);
  }
  if (process.hThread) CloseHandle(process.hThread);
  if (!created) {
    CloseHandle(event);
    LOG_WARN("Unattended host launch failed: session={}, error={}", session_id,
             error);
    return false;
  }
  unattended_process_ = process.hProcess;
  unattended_stop_event_ = event;
  unattended_session_ = session_id;
  LOG_INFO("Unattended host started: session={}, pid={}", session_id,
           process.dwProcessId);
  return true;
}

void CrossDeskServiceHost::StopUnattendedHost() {
  if (unattended_stop_event_) SetEvent(unattended_stop_event_);
  if (unattended_process_) WaitForSingleObject(unattended_process_, 5000);
  if (unattended_process_ &&
      WaitForSingleObject(unattended_process_, 0) == WAIT_TIMEOUT)
    TerminateProcess(unattended_process_, ERROR_PROCESS_ABORTED);
  if (unattended_process_) {
    // Do not start another instance until the previous identity has exited.
    WaitForSingleObject(unattended_process_, INFINITE);
    CloseHandle(unattended_process_);
  }
  if (unattended_stop_event_) CloseHandle(unattended_stop_event_);
  unattended_process_ = unattended_stop_event_ = nullptr;
  unattended_session_ = 0xFFFFFFFF;
}

void CrossDeskServiceHost::ReportServiceStatus(DWORD current_state,
                                               DWORD win32_exit_code,
                                               DWORD wait_hint) {
  if (status_handle_ == nullptr) {
    return;
  }

  service_status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  service_status_.dwCurrentState = current_state;
  service_status_.dwWin32ExitCode = win32_exit_code;
  service_status_.dwWaitHint = wait_hint;
  service_status_.dwControlsAccepted =
      current_state == SERVICE_RUNNING
          ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN |
             SERVICE_ACCEPT_SESSIONCHANGE)
          : 0;
  SetServiceStatus(status_handle_, &service_status_);
}

int CrossDeskServiceHost::RunServiceLoop(bool as_service) {
  console_mode_ = !as_service;

  if (as_service) {
    status_handle_ = RegisterServiceCtrlHandlerExW(
        kCrossDeskServiceName, &CrossDeskServiceHost::ServiceControlHandler,
        this);
    if (status_handle_ == nullptr) {
      DWORD error = GetLastError();
      LOG_ERROR("RegisterServiceCtrlHandlerExW failed, error={}", error);
      return static_cast<int>(error);
    }
    ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
  }

  int init_error = InitializeRuntime();
  if (init_error != 0) {
    if (as_service) {
      ReportServiceStatus(SERVICE_STOPPED, static_cast<DWORD>(init_error), 0);
    }
    return init_error;
  }

  if (as_service) {
    ReportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);
  }

  if (console_mode_) {
    SetConsoleCtrlHandler(&CrossDeskServiceHost::ConsoleControlHandler, TRUE);
    std::cout << "CrossDesk service running in console mode. Press "
                 "Ctrl+C to stop."
              << std::endl;
  }

  WaitForSingleObject(stop_event_, INFINITE);

  if (as_service) {
    ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 20000);
  }

  ShutdownRuntime();

  if (console_mode_) {
    SetConsoleCtrlHandler(&CrossDeskServiceHost::ConsoleControlHandler, FALSE);
  }

  if (as_service) {
    ReportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
  }

  return 0;
}

void CrossDeskServiceHost::IpcServerLoop() {
  PipeSecurityAttributes security_attributes;
  SECURITY_ATTRIBUTES* pipe_attributes = nullptr;
  if (security_attributes.Initialize()) {
    pipe_attributes = security_attributes.get();
  } else {
    LOG_WARN("Pipe security initialization failed, error={}", GetLastError());
  }

  HANDLE pipe = INVALID_HANDLE_VALUE;
  pipe_deadline_detail::Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  if (!event.value) {
    LOG_ERROR("CreateEventW for pipe failed, error={}", GetLastError());
    return;
  }
  while (stop_event_ != nullptr &&
         WaitForSingleObject(stop_event_, 0) != WAIT_OBJECT_0) {
    // Keep the name registered between requests. Recreating the only instance
    // made high-rate mouse clients observe FILE_NOT_FOUND and sleep on retries.
    if (pipe == INVALID_HANDLE_VALUE) {
      pipe = CreateNamedPipeW(
          kCrossDeskServicePipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
          PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 4096, 4096, 0,
          pipe_attributes);
    }
    if (pipe == INVALID_HANDLE_VALUE) {
      DWORD error = GetLastError();
      LOG_ERROR("CreateNamedPipeW failed, error={}", error);
      WaitForSingleObject(stop_event_, 1000);
      continue;
    }

    OVERLAPPED overlapped{};
    overlapped.hEvent = event.value;
    ResetEvent(event.value);

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
      LOG_WARN("ConnectNamedPipe failed, error={}", connect_error);
      CloseHandle(pipe);
      pipe = INVALID_HANDLE_VALUE;
      continue;
    }

    if (!connected) {
      HANDLE wait_handles[] = {stop_event_, overlapped.hEvent};
      DWORD wait_result =
          WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
      if (wait_result != WAIT_OBJECT_0 + 1) {
        CancelIoEx(pipe, &overlapped);
        DWORD ignored = 0;
        GetOverlappedResult(pipe, &overlapped, &ignored, TRUE);
        break;
      }
    }

    char buffer[1024] = {0};
    DWORD bytes_read = 0;
    DWORD transfer_error = ERROR_SUCCESS;
    if (pipe_deadline_detail::Transfer(
            pipe, overlapped.hEvent, false, buffer, sizeof(buffer) - 1,
            GetTickCount64() + 1000, &bytes_read, &transfer_error) &&
        bytes_read > 0) {
      ULONG client_session_id = 0xFFFFFFFF;
      // Obtain identity from Windows, never from caller-supplied JSON. A GUI
      // left in an RDP session must not control a different console session.
      const std::string response =
          GetNamedPipeClientSessionId(pipe, &client_session_id)
              ? HandleIpcCommand(std::string(buffer, buffer + bytes_read),
                                  client_session_id)
              : BuildErrorJson("client_session_query_failed", GetLastError());
      DWORD bytes_written = 0;
      if (pipe_deadline_detail::Transfer(
              pipe, overlapped.hEvent, true, const_cast<char*>(response.data()),
              static_cast<DWORD>(response.size()), GetTickCount64() + 1000,
              &bytes_written, &transfer_error)) {
        FlushFileBuffers(pipe);
      }
    }

    DisconnectNamedPipe(pipe);
  }
  if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
}

void CrossDeskServiceHost::RefreshSessionState() {
  // WTS/process enumeration can be slow. Never hold the snapshot lock while
  // querying Windows: status and input requests only need the last sample.
  const ULONGLONG probe_started = GetTickCount64();
  uint64_t generation = 0;
  {
    std::lock_guard lock(state_mutex_);
    generation = session_state_generation_;
  }
  const DWORD session = WTSGetActiveConsoleSessionId();
  DWORD process_session = 0xFFFFFFFF;
  ProcessIdToSessionId(GetCurrentProcessId(), &process_session);
  const bool logon_ui = IsLogonUiRunningInSession(session);
  const bool consent_ui = IsConsentUiRunningInSession(session);
  const InputDesktopInfo desktop = GetInputDesktopInfo();
  std::wstring username;
  const bool prelogin = !GetSessionUserName(session, &username) || username.empty();
  bool locked = false;
  const bool lock_known = QuerySessionLockState(session, &locked);

  std::lock_guard<std::mutex> lock(state_mutex_);
  if (generation != session_state_generation_ ||
      session != WTSGetActiveConsoleSessionId()) return;
  const DWORD previous_session_id = active_session_id_;
  active_session_id_ = session;
  if (active_session_id_ != previous_session_id) {
    last_session_transition_tick_ = probe_started;
    session_lock_state_known_ = false;
    ResetSessionHelperReportedStateLocked("session_changed", 0);
    sas_secure_desktop_until_tick_ = 0;
    sas_secure_desktop_seen_ = false;
    user_desktop_input_until_tick_ = 0;
  }
  process_session_id_ = process_session;
  logon_ui_visible_ = logon_ui;
  consent_ui_visible_ = consent_ui;
  input_desktop_available_ = desktop.available;
  input_desktop_error_code_ = desktop.error_code;
  input_desktop_name_ = desktop.name;
  secure_desktop_active_ =
      _stricmp(input_desktop_name_.c_str(), "Winlogon") == 0;

  session_username_ = WideToUtf8(username);
  prelogin_ = prelogin;
  if (lock_known) {
    if (session_lock_state_known_ && session_locked_ != locked) {
      last_session_transition_tick_ = probe_started;
      ResetSessionHelperReportedStateLocked("lock_state_changed", 0);
    }
    session_locked_ = locked;
    session_lock_state_known_ = true;
  } else if (last_session_event_session_id_ == session &&
             last_session_event_type_ == WTS_SESSION_LOCK) {
    session_locked_ = true;
    session_lock_state_known_ = true;
  } else if (last_session_event_session_id_ == session &&
             (last_session_event_type_ == WTS_SESSION_UNLOCK ||
              last_session_event_type_ == WTS_SESSION_LOGON)) {
    session_locked_ = false;
    session_lock_state_known_ = true;
  } else {
    session_lock_state_known_ = false;
    session_locked_ =
        (logon_ui_visible_ || secure_desktop_active_) && !prelogin_;
  }
}

void CrossDeskServiceHost::ResetSessionHelperReportedStateLocked(
    const char* error, DWORD error_code) {
  session_helper_status_ok_ = false;
  session_helper_status_error_ = error != nullptr ? error : "";
  session_helper_status_error_code_ = error_code;
  session_helper_report_session_id_ = 0xFFFFFFFF;
  session_helper_report_process_id_ = 0;
  session_helper_report_session_locked_ = false;
  session_helper_report_input_desktop_available_ = false;
  session_helper_report_input_desktop_error_code_ = 0;
  session_helper_report_input_desktop_.clear();
  session_helper_report_lock_app_visible_ = false;
  session_helper_report_logon_ui_visible_ = false;
  session_helper_report_consent_ui_visible_ = false;
  session_helper_report_secure_desktop_active_ = false;
  session_helper_report_credential_ui_visible_ = false;
  session_helper_report_unlock_ui_visible_ = false;
  session_helper_report_interactive_stage_.clear();
  session_helper_report_state_age_ms_ = 0;
  session_helper_report_uptime_ms_ = 0;
}

bool CrossDeskServiceHost::GetEffectiveSessionLockedLocked() const {
  return session_lock_state_known_ || !session_helper_status_ok_
             ? session_locked_ : session_helper_report_session_locked_;
}

bool CrossDeskServiceHost::IsHelperReportingLockScreenLocked() const {
  return session_helper_report_lock_app_visible_ ||
         session_helper_report_interactive_stage_ == "lock-screen";
}

bool CrossDeskServiceHost::HasSecureInputUiLocked() const {
  const bool service_host_credential_ui_visible =
      !session_helper_status_ok_ &&
      IsCredentialUiVisible(prelogin_, session_locked_, logon_ui_visible_,
                            consent_ui_visible_,
                            input_desktop_available_,
                            secure_desktop_active_);
  return IsSasSecureDesktopGraceActiveLocked() || prelogin_ ||
         secure_desktop_active_ || service_host_credential_ui_visible ||
         session_helper_report_consent_ui_visible_ ||
         session_helper_report_credential_ui_visible_ ||
         session_helper_report_secure_desktop_active_ ||
         session_helper_report_unlock_ui_visible_ ||
         session_helper_report_interactive_stage_ == "credential-ui" ||
         session_helper_report_interactive_stage_ == "secure-desktop";
}

void CrossDeskServiceHost::UpdateSasSecureDesktopGraceLocked(
    const std::string& observed_stage) {
  if (sas_secure_desktop_until_tick_ == 0) {
    sas_secure_desktop_seen_ = false;
    return;
  }

  if (observed_stage == "credential-ui" || observed_stage == "secure-desktop" ||
      observed_stage == "lock-screen") {
    sas_secure_desktop_seen_ = true;
    return;
  }

  if (sas_secure_desktop_seen_ && observed_stage == "user-desktop") {
    sas_secure_desktop_until_tick_ = 0;
    sas_secure_desktop_seen_ = false;
  }
}

bool CrossDeskServiceHost::IsSasSecureDesktopGraceActiveLocked() const {
  return last_sas_success_ && sas_secure_desktop_until_tick_ != 0 &&
         GetTickCount64() < sas_secure_desktop_until_tick_;
}

bool CrossDeskServiceHost::ShouldKeepSecureInputHelperLocked(
    DWORD target_session_id) const {
  if (target_session_id == 0xFFFFFFFF) {
    return false;
  }

  return HasSecureInputUiLocked() || GetEffectiveSessionLockedLocked() ||
         GetTickCount64() < user_desktop_input_until_tick_;
}

std::string CrossDeskServiceHost::ResolveInteractiveStageLocked() const {
  // Session 0's input desktop cannot describe a console that has no user
  // helper yet. Pre-login must always use the SYSTEM secure-desktop path.
  if (prelogin_) return "credential-ui";
  if (IsSasSecureDesktopGraceActiveLocked() &&
      (session_helper_report_interactive_stage_.empty() ||
       session_helper_report_interactive_stage_ == "user-desktop")) {
    return "secure-desktop";
  }

  if (!session_helper_report_interactive_stage_.empty()) {
    return session_helper_report_interactive_stage_;
  }

  // A lock notification can precede LockApp's first helper sample. The SYSTEM
  // helper binds the actual input desktop on each operation.
  if (GetEffectiveSessionLockedLocked()) return "secure-desktop";

  const bool service_host_credential_ui_visible =
      IsCredentialUiVisible(prelogin_, session_locked_, logon_ui_visible_,
                            consent_ui_visible_,
                            input_desktop_available_,
                            secure_desktop_active_);
  return DetermineInteractiveStage(
      IsHelperReportingLockScreenLocked(),
      session_helper_report_credential_ui_visible_ ||
          service_host_credential_ui_visible,
      session_helper_report_secure_desktop_active_ || secure_desktop_active_);
}

std::string CrossDeskServiceHost::ResolveInteractiveDesktopLocked(
    const std::string& interactive_stage) const {
  if (prelogin_) return "Winlogon";
  if (session_helper_status_ok_ &&
      session_helper_report_input_desktop_available_ &&
      !session_helper_report_input_desktop_.empty() &&
      (IsSecureDesktopInteractionRequired(interactive_stage) ||
       session_helper_report_consent_ui_visible_)) {
    return session_helper_report_input_desktop_;
  }

  if (interactive_stage == "lock-screen") {
    return "Default";
  }

  if (interactive_stage == "user-desktop") {
    return "Default";
  }

  if (input_desktop_available_ && !input_desktop_name_.empty() &&
      (interactive_stage == "credential-ui" || consent_ui_visible_)) {
    return input_desktop_name_;
  }

  if (interactive_stage == "credential-ui" ||
      interactive_stage == "secure-desktop") {
    return "Winlogon";
  }

  return "";
}

std::wstring CrossDeskServiceHost::GetSessionHelperPath() const {
  std::wstring current_executable = GetCurrentExecutablePathW();
  if (current_executable.empty()) {
    return L"";
  }

  return (std::filesystem::path(current_executable).parent_path() /
          L"crossdesk_session_helper.exe")
      .wstring();
}

std::wstring CrossDeskServiceHost::GetSessionHelperStopEventName(
    DWORD session_id) const {
  return L"Global\\CrossDeskSessionHelperStop-" + std::to_wstring(session_id);
}

std::wstring CrossDeskServiceHost::GetSecureInputHelperPath() const {
  return GetSessionHelperPath();
}

std::wstring CrossDeskServiceHost::GetSecureInputHelperStopEventName(
    DWORD session_id) const {
  return L"Global\\CrossDeskSecureInputHelperStop-" +
         std::to_wstring(session_id);
}

void CrossDeskServiceHost::ReapSessionHelper() {
  HANDLE process_handle = nullptr;
  HANDLE stop_event_handle = nullptr;
  DWORD exit_code = 0;

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (session_helper_process_handle_ == nullptr) {
      return;
    }

    DWORD wait_result = WaitForSingleObject(session_helper_process_handle_, 0);
    if (wait_result != WAIT_OBJECT_0) {
      session_helper_running_ = true;
      return;
    }

    GetExitCodeProcess(session_helper_process_handle_, &exit_code);
    process_handle = session_helper_process_handle_;
    stop_event_handle = session_helper_stop_event_;
    session_helper_process_handle_ = nullptr;
    session_helper_stop_event_ = nullptr;
    session_helper_running_ = false;
    session_helper_process_id_ = 0;
    session_helper_exit_code_ = exit_code;
    session_helper_started_at_tick_ = 0;
  }

  if (process_handle != nullptr) {
    CloseHandle(process_handle);
  }
  if (stop_event_handle != nullptr) {
    CloseHandle(stop_event_handle);
  }
}

void CrossDeskServiceHost::ReapSecureInputHelper() {
  HANDLE process_handle = nullptr;
  HANDLE stop_event_handle = nullptr;
  DWORD exit_code = 0;

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (secure_input_helper_process_handle_ == nullptr) {
      return;
    }

    DWORD wait_result =
        WaitForSingleObject(secure_input_helper_process_handle_, 0);
    if (wait_result != WAIT_OBJECT_0) {
      secure_input_helper_running_ = true;
      return;
    }

    GetExitCodeProcess(secure_input_helper_process_handle_, &exit_code);
    process_handle = secure_input_helper_process_handle_;
    stop_event_handle = secure_input_helper_stop_event_;
    secure_input_helper_process_handle_ = nullptr;
    secure_input_helper_stop_event_ = nullptr;
    secure_input_helper_running_ = false;
    secure_input_helper_process_id_ = 0;
    secure_input_helper_exit_code_ = exit_code;
    secure_input_helper_started_at_tick_ = 0;
    secure_input_helper_interactive_stage_.clear();
    secure_input_helper_interactive_desktop_.clear();
  }

  if (process_handle != nullptr) {
    CloseHandle(process_handle);
  }
  if (stop_event_handle != nullptr) {
    CloseHandle(stop_event_handle);
  }
}

void CrossDeskServiceHost::StopSessionHelper() {
  HANDLE process_handle = nullptr;
  HANDLE stop_event_handle = nullptr;

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    process_handle = session_helper_process_handle_;
    stop_event_handle = session_helper_stop_event_;
    session_helper_process_handle_ = nullptr;
    session_helper_stop_event_ = nullptr;
    session_helper_running_ = false;
    session_helper_process_id_ = 0;
    session_helper_started_at_tick_ = 0;
    ResetSessionHelperReportedStateLocked(nullptr, 0);
  }

  if (stop_event_handle != nullptr) {
    SetEvent(stop_event_handle);
  }

  if (process_handle != nullptr) {
    if (WaitForSingleObject(process_handle, 3000) == WAIT_TIMEOUT) {
      TerminateProcess(process_handle, ERROR_PROCESS_ABORTED);
      WaitForSingleObject(process_handle, 1000);
    }
  }

  if (process_handle != nullptr) {
    CloseHandle(process_handle);
  }
  if (stop_event_handle != nullptr) {
    CloseHandle(stop_event_handle);
  }
}

void CrossDeskServiceHost::StopSecureInputHelper() {
  HANDLE process_handle = nullptr;
  HANDLE stop_event_handle = nullptr;

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    process_handle = secure_input_helper_process_handle_;
    stop_event_handle = secure_input_helper_stop_event_;
    secure_input_helper_process_handle_ = nullptr;
    secure_input_helper_stop_event_ = nullptr;
    secure_input_helper_running_ = false;
    secure_input_helper_process_id_ = 0;
    secure_input_helper_started_at_tick_ = 0;
    secure_input_helper_interactive_stage_.clear();
    secure_input_helper_interactive_desktop_.clear();
  }

  if (stop_event_handle != nullptr) {
    SetEvent(stop_event_handle);
  }

  if (process_handle != nullptr) {
    if (WaitForSingleObject(process_handle, 3000) == WAIT_TIMEOUT) {
      TerminateProcess(process_handle, ERROR_PROCESS_ABORTED);
      WaitForSingleObject(process_handle, 1000);
    }
  }

  if (process_handle != nullptr) {
    CloseHandle(process_handle);
  }
  if (stop_event_handle != nullptr) {
    CloseHandle(stop_event_handle);
  }
}

bool CrossDeskServiceHost::LaunchSessionHelper(DWORD session_id) {
  std::wstring helper_path = GetSessionHelperPath();
  if (helper_path.empty() || !std::filesystem::exists(helper_path)) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    session_helper_last_error_ = "helper_binary_missing";
    session_helper_last_error_code_ = ERROR_FILE_NOT_FOUND;
    return false;
  }

  std::wstring stop_event_name = GetSessionHelperStopEventName(session_id);
  KernelObjectSecurityAttributes event_security;
  SECURITY_ATTRIBUTES* event_attributes = nullptr;
  if (event_security.Initialize()) {
    event_attributes = event_security.get();
  }

  HANDLE stop_event_handle =
      CreateEventW(event_attributes, TRUE, FALSE, stop_event_name.c_str());
  if (stop_event_handle == nullptr) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    session_helper_last_error_ = "create_helper_stop_event_failed";
    session_helper_last_error_code_ = GetLastError();
    return false;
  }

  std::wstring command_line =
      QuoteWindowsArgument(helper_path) + L" --session-helper --session-id " +
      std::to_wstring(session_id) + L" --stop-event " +
      QuoteWindowsArgument(stop_event_name) + L" --service-lifetime " +
      QuoteWindowsArgument(lifetime_mutex_name_);
  std::wstring mutable_command_line = command_line;

  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);
  startup_info.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
  PROCESS_INFORMATION process_info{};
  BOOL created = FALSE;

  if (console_mode_ && process_session_id_ == session_id) {
    created = CreateProcessW(helper_path.c_str(), mutable_command_line.data(),
                             nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                             nullptr, &startup_info, &process_info);
  } else {
    HANDLE user_token = nullptr;
    HANDLE primary_token = nullptr;
    ScopedEnvironmentBlock environment_block;

    if (!WTSQueryUserToken(session_id, &user_token)) {
      DWORD error = GetLastError();
      CloseHandle(stop_event_handle);
      std::lock_guard<std::mutex> lock(state_mutex_);
      session_helper_last_error_ = "wts_query_user_token_failed";
      session_helper_last_error_code_ = error;
      return false;
    }

    BOOL duplicated = DuplicateTokenEx(
        user_token,
        TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY |
            TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
        nullptr, SecurityImpersonation, TokenPrimary, &primary_token);
    CloseHandle(user_token);
    if (!duplicated) {
      DWORD error = GetLastError();
      CloseHandle(stop_event_handle);
      std::lock_guard<std::mutex> lock(state_mutex_);
      session_helper_last_error_ = "duplicate_token_failed";
      session_helper_last_error_code_ = error;
      return false;
    }

    CreateEnvironmentBlock(&environment_block.environment, primary_token,
                           FALSE);
    created = CreateProcessAsUserW(
        primary_token, helper_path.c_str(), mutable_command_line.data(),
        nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
        environment_block.environment, nullptr, &startup_info, &process_info);
    DWORD error = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(primary_token);
    if (!created) {
      CloseHandle(stop_event_handle);
      std::lock_guard<std::mutex> lock(state_mutex_);
      session_helper_last_error_ = "create_process_as_user_failed";
      session_helper_last_error_code_ = error;
      return false;
    }
  }

  if (!created) {
    CloseHandle(stop_event_handle);
    LOG_ERROR("Could not launch a supervised session helper, session={}",
              session_id);
    return false;
  }

  CloseHandle(process_info.hThread);
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    session_helper_process_handle_ = process_info.hProcess;
    session_helper_stop_event_ = stop_event_handle;
    session_helper_process_id_ = process_info.dwProcessId;
    session_helper_session_id_ = session_id;
    session_helper_exit_code_ = STILL_ACTIVE;
    session_helper_last_error_code_ = 0;
    session_helper_last_error_.clear();
    session_helper_running_ = true;
    session_helper_started_at_tick_ = GetTickCount64();
  }

  LOG_INFO("Session helper started: session_id={}, pid={}", session_id,
           process_info.dwProcessId);
  return true;
}

bool CrossDeskServiceHost::LaunchSecureInputHelper(
    DWORD session_id, const std::string& interactive_stage,
    const std::string& interactive_desktop) {
  std::wstring helper_path = GetSecureInputHelperPath();
  if (helper_path.empty() || !std::filesystem::exists(helper_path)) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    secure_input_helper_last_error_ = "secure_input_helper_binary_missing";
    secure_input_helper_last_error_code_ = ERROR_FILE_NOT_FOUND;
    return false;
  }

  std::wstring stop_event_name = GetSecureInputHelperStopEventName(session_id);
  KernelObjectSecurityAttributes event_security;
  SECURITY_ATTRIBUTES* event_attributes = nullptr;
  if (event_security.Initialize()) {
    event_attributes = event_security.get();
  }

  HANDLE stop_event_handle =
      CreateEventW(event_attributes, TRUE, FALSE, stop_event_name.c_str());
  if (stop_event_handle == nullptr) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    secure_input_helper_last_error_ =
        "create_secure_input_helper_stop_event_failed";
    secure_input_helper_last_error_code_ = GetLastError();
    return false;
  }

  std::wstring command_line =
      QuoteWindowsArgument(helper_path) +
      L" --secure-input-helper --session-id " + std::to_wstring(session_id) +
      L" --stop-event " + QuoteWindowsArgument(stop_event_name) +
      L" --service-lifetime " + QuoteWindowsArgument(lifetime_mutex_name_);
  std::wstring mutable_command_line = command_line;

  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);
  std::wstring secure_input_helper_desktop =
      SecureInputHelperDesktopForStage(interactive_stage, interactive_desktop);
  startup_info.lpDesktop =
      const_cast<LPWSTR>(secure_input_helper_desktop.c_str());
  PROCESS_INFORMATION process_info{};
  BOOL created = FALSE;

  if (console_mode_ && process_session_id_ == session_id) {
    created = CreateProcessW(helper_path.c_str(), mutable_command_line.data(),
                             nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                             nullptr, &startup_info, &process_info);
  } else {
    HANDLE primary_token = nullptr;
    ScopedEnvironmentBlock environment_block;
    DWORD error = 0;
    if (!CreateSessionSystemToken(session_id, &primary_token, &error)) {
      CloseHandle(stop_event_handle);
      std::lock_guard<std::mutex> lock(state_mutex_);
      secure_input_helper_last_error_ = "create_session_system_token_failed";
      secure_input_helper_last_error_code_ = error;
      return false;
    }

    CreateEnvironmentBlock(&environment_block.environment, primary_token,
                           FALSE);
    created = CreateProcessAsUserW(
        primary_token, helper_path.c_str(), mutable_command_line.data(),
        nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
        environment_block.environment, nullptr, &startup_info, &process_info);
    error = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(primary_token);
    if (!created) {
      CloseHandle(stop_event_handle);
      std::lock_guard<std::mutex> lock(state_mutex_);
      secure_input_helper_last_error_ = "create_secure_input_helper_failed";
      secure_input_helper_last_error_code_ = error;
      return false;
    }
  }

  if (!created) {
    CloseHandle(stop_event_handle);
    LOG_ERROR("Could not launch a supervised session helper, session={}",
              session_id);
    return false;
  }

  CloseHandle(process_info.hThread);
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    secure_input_helper_process_handle_ = process_info.hProcess;
    secure_input_helper_stop_event_ = stop_event_handle;
    secure_input_helper_process_id_ = process_info.dwProcessId;
    secure_input_helper_session_id_ = session_id;
    secure_input_helper_exit_code_ = STILL_ACTIVE;
    secure_input_helper_last_error_code_ = 0;
    secure_input_helper_last_error_.clear();
    secure_input_helper_running_ = true;
    secure_input_helper_started_at_tick_ = GetTickCount64();
    secure_input_helper_interactive_stage_ = interactive_stage;
    secure_input_helper_interactive_desktop_ = interactive_desktop;
  }

  LOG_INFO(
      "Secure input helper started: session_id={}, pid={}, stage='{}', "
      "interactive_desktop='{}', desktop='{}'",
      session_id, process_info.dwProcessId, interactive_stage,
      interactive_desktop, WideToUtf8(secure_input_helper_desktop));
  return true;
}

void CrossDeskServiceHost::EnsureSessionHelper() {
  ReapSessionHelper();

  DWORD target_session_id = 0xFFFFFFFF;
  bool has_active_user = false;
  bool already_running = false;

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    target_session_id = active_session_id_;
    has_active_user = !prelogin_;
    already_running = session_helper_running_ &&
                      session_helper_session_id_ == target_session_id;
  }

  if (already_running) {
    return;
  }

  if (target_session_id == 0xFFFFFFFF || !has_active_user) {
    StopSessionHelper();
    std::lock_guard<std::mutex> lock(state_mutex_);
    session_helper_last_error_ = target_session_id == 0xFFFFFFFF
                                     ? "no_active_console_session"
                                     : "no_active_user_session";
    session_helper_last_error_code_ = 0;
    return;
  }

  StopSessionHelper();
  LaunchSessionHelper(target_session_id);
}

void CrossDeskServiceHost::RefreshSessionHelperReportedState() {
  DWORD target_session_id = 0xFFFFFFFF;
  bool helper_running = false;
  uint64_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    target_session_id = session_helper_session_id_;
    helper_running = session_helper_running_;
    generation = session_state_generation_;
  }

  if (!helper_running || target_session_id == 0xFFFFFFFF) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (generation != session_state_generation_) return;
    ResetSessionHelperReportedStateLocked("helper_not_running", 0);
    return;
  }

  std::string response = QueryNamedPipeMessage(
      GetCrossDeskSessionHelperPipeName(target_session_id),
      kCrossDeskSessionHelperStatusCommand, 300);
  Json json = Json::parse(response, nullptr, false);
  if (json.is_discarded()) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (generation != session_state_generation_) return;
    ResetSessionHelperReportedStateLocked("invalid_helper_status_json", 0);
    return;
  }

  if (!json.value("ok", false)) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (generation != session_state_generation_) return;
    const std::string error =
        json.value("error", std::string("helper_status_failed"));
    ResetSessionHelperReportedStateLocked(
        error.c_str(), json.value("code", static_cast<DWORD>(0)));
    return;
  }

  std::lock_guard<std::mutex> lock(state_mutex_);
  if (generation != session_state_generation_) return;
  if (target_session_id != active_session_id_ ||
      target_session_id != session_helper_session_id_ ||
      json.value("session_id", static_cast<DWORD>(0xFFFFFFFF)) !=
          target_session_id ||
      json.value("process_id", 0u) != session_helper_process_id_) {
    ResetSessionHelperReportedStateLocked("helper_session_mismatch", 0);
    return;
  }
  if (!IsCurrentDesktopSample(
          json.value("sample_started_tick", uint64_t{0}),
          last_session_transition_tick_, session_lock_state_known_,
          session_locked_, json.value("session_locked", false))) {
    if (session_helper_status_error_ != "stale_helper_state") {
      LOG_DEBUG("Ignoring stale helper desktop sample: sample_tick={}, "
                "transition_tick={}, helper_locked={}, session_locked={}",
                json.value("sample_started_tick", uint64_t{0}),
                last_session_transition_tick_, json.value("session_locked", false),
                session_locked_);
    }
    ResetSessionHelperReportedStateLocked("stale_helper_state", 0);
    return;
  }
  session_helper_status_ok_ = true;
  session_helper_status_error_.clear();
  session_helper_status_error_code_ = 0;
  session_helper_report_session_id_ =
      json.value("session_id", static_cast<DWORD>(0xFFFFFFFF));
  session_helper_report_process_id_ = json.value("process_id", 0u);
  session_helper_report_session_locked_ = json.value("session_locked", false);
  session_helper_report_input_desktop_available_ =
      json.value("input_desktop_available", false);
  session_helper_report_input_desktop_error_code_ =
      json.value("input_desktop_error_code", 0u);
  session_helper_report_input_desktop_ =
      json.value("input_desktop", std::string());
  session_helper_report_lock_app_visible_ =
      json.value("lock_app_visible", false);
  session_helper_report_logon_ui_visible_ =
      json.value("logon_ui_visible", false);
  session_helper_report_consent_ui_visible_ =
      json.value("consent_ui_visible", false);
  session_helper_report_secure_desktop_active_ =
      json.value("secure_desktop_active", false);
  session_helper_report_credential_ui_visible_ =
      json.value("credential_ui_visible", false);
  session_helper_report_unlock_ui_visible_ =
      json.value("unlock_ui_visible", false);
  session_helper_report_interactive_stage_ =
      json.value("interactive_stage", std::string());
  session_helper_report_state_age_ms_ = json.value("state_age_ms", 0ull);
  session_helper_report_uptime_ms_ = json.value("uptime_ms", 0ull);
  UpdateSasSecureDesktopGraceLocked(session_helper_report_interactive_stage_);
}

void CrossDeskServiceHost::RecordSessionEvent(DWORD event_type,
                                              DWORD session_id) {
  if (event_type == WTS_SESSION_LOGOFF) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (session_id == active_session_id_) ++unattended_logoff_generation_;
  }
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    last_session_event_type_ = event_type;
    ++session_state_generation_;
    last_session_event_session_id_ = session_id;
    const DWORD previous_session_id = active_session_id_;
    active_session_id_ = WTSGetActiveConsoleSessionId();
    if (active_session_id_ != previous_session_id) {
      ResetSessionHelperReportedStateLocked("session_changed", 0);
      sas_secure_desktop_until_tick_ = 0;
      sas_secure_desktop_seen_ = false;
      user_desktop_input_until_tick_ = 0;
    }
    if (session_id == active_session_id_) {
      if (event_type == WTS_SESSION_LOGOFF) {
        user_desktop_input_until_tick_ = 0;
      }
      // Do not let an older helper sample override a session notification.
      ResetSessionHelperReportedStateLocked("session_event", 0);
      last_session_transition_tick_ = GetTickCount64();
      if (event_type == WTS_SESSION_LOCK) {
        session_locked_ = true;
        session_lock_state_known_ = true;
      }
      if (event_type == WTS_SESSION_UNLOCK || event_type == WTS_SESSION_LOGON) {
        session_locked_ = false;
        session_lock_state_known_ = true;
        sas_secure_desktop_until_tick_ = 0;
        sas_secure_desktop_seen_ = false;
      }
    }
  }

  LOG_INFO("Session event: type={}, session_id={}, active_session_id={}",
           SessionEventToString(event_type), session_id, WTSGetActiveConsoleSessionId());
  WakeSessionState();
}

std::string CrossDeskServiceHost::HandleIpcCommand(const std::string& command,
                                                 DWORD client_session_id) {
  std::string normalized = ToLower(Trim(command));
  if (normalized == "ping") {
    return "{\"ok\":true,\"reply\":\"pong\"}";
  }
  if (normalized == "status") {
    return BuildStatusResponse(client_session_id);
  }
  if (normalized == "sas") {
    return SendSecureAttentionSequence(client_session_id);
  }
  if (normalized == "cancel-consent") {
    return CancelPendingConsent(client_session_id);
  }
  int key_code = 0;
  bool is_down = false;
  uint32_t scan_code = 0;
  bool extended = false;
  if (ParseSecureDesktopKeyboardIpcCommand(normalized, &key_code, &is_down,
                                           &scan_code, &extended)) {
    return SendSecureDesktopKeyboardInput(client_session_id, key_code, is_down,
                                           scan_code, extended);
  }
  SecureDesktopMouseRequest mouse_request;
  if (ParseSecureDesktopMouseIpcCommand(normalized, &mouse_request)) {
    return SendSecureDesktopMouseInput(client_session_id, mouse_request.x,
                                        mouse_request.y, mouse_request.wheel,
                                        mouse_request.flag);
  }
  return BuildErrorJson("unknown_command");
}

void CrossDeskServiceHost::WakeSessionState() {
  if (session_state_event_) SetEvent(session_state_event_);
}

void CrossDeskServiceHost::SessionStateLoop() {
  HANDLE events[] = {stop_event_, session_state_event_};
  while (WaitForSingleObject(stop_event_, 0) != WAIT_OBJECT_0) {
    RefreshInteractiveState();
    if (WaitForMultipleObjects(2, events, FALSE, 250) == WAIT_OBJECT_0) break;
  }
}

void CrossDeskServiceHost::RefreshInteractiveState() {
  ReapSecureInputHelper();
  ReapSessionHelper();
  RefreshSessionState();
  EnsureSessionHelper();
  RefreshSessionHelperReportedState();
  bool keep_secure_input_helper = false;
  bool launch_secure_input_helper = false;
  DWORD secure_input_target_session_id = 0xFFFFFFFF;
  std::string secure_input_interactive_stage;
  std::string secure_input_interactive_desktop;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    secure_input_target_session_id = active_session_id_;
    secure_input_interactive_stage = ResolveInteractiveStageLocked();
    secure_input_interactive_desktop =
        ResolveInteractiveDesktopLocked(secure_input_interactive_stage);
    keep_secure_input_helper =
        ShouldKeepSecureInputHelperLocked(secure_input_target_session_id);
    launch_secure_input_helper =
        keep_secure_input_helper &&
        (!secure_input_helper_running_ ||
         secure_input_helper_session_id_ != secure_input_target_session_id);
  }

  if (keep_secure_input_helper) {
    if (launch_secure_input_helper) {
      StopSecureInputHelper();
      LaunchSecureInputHelper(secure_input_target_session_id,
                              secure_input_interactive_stage,
                              secure_input_interactive_desktop);
    }
  } else {
    StopSecureInputHelper();
  }
}

std::string CrossDeskServiceHost::BuildStatusResponse(DWORD client_session_id) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (client_session_id == 0xFFFFFFFF ||
      active_session_id_ != client_session_id ||
      client_session_id != WTSGetActiveConsoleSessionId()) {
    return BuildErrorJson("service_session_mismatch");
  }
  std::string username_utf8 = EscapeJsonString(session_username_);
  std::string input_desktop = EscapeJsonString(input_desktop_name_);
  std::string last_sas_error = EscapeJsonString(last_sas_error_);
  std::string session_helper_last_error =
      EscapeJsonString(session_helper_last_error_);
  std::string session_helper_status_error =
      EscapeJsonString(session_helper_status_error_);
  std::string session_helper_path =
      EscapeJsonString(WideToUtf8(GetSessionHelperPath()));
  std::string secure_input_helper_path =
      EscapeJsonString(WideToUtf8(GetSecureInputHelperPath()));
  std::string helper_input_desktop =
      EscapeJsonString(session_helper_report_input_desktop_);
  std::string secure_input_helper_last_error =
      EscapeJsonString(secure_input_helper_last_error_);
  std::string secure_input_helper_interactive_stage =
      EscapeJsonString(secure_input_helper_interactive_stage_);
  std::string secure_input_helper_interactive_desktop =
      EscapeJsonString(secure_input_helper_interactive_desktop_);
  bool interactive_state_ready = session_helper_status_ok_;
  const bool sas_secure_desktop_grace_active =
      IsSasSecureDesktopGraceActiveLocked();
  const char* interactive_state_source =
      interactive_state_ready ? "session-helper" : "service-host";
  const bool effective_session_locked = GetEffectiveSessionLockedLocked();
  const bool interactive_lock_screen_visible =
      interactive_state_ready
          ? (effective_session_locked && IsHelperReportingLockScreenLocked())
          : false;
  const bool service_host_credential_ui_visible =
      IsCredentialUiVisible(prelogin_, session_locked_, logon_ui_visible_,
                            consent_ui_visible_,
                            input_desktop_available_,
                            secure_desktop_active_);
  bool credential_ui_visible =
      interactive_state_ready ? session_helper_report_credential_ui_visible_
                              : service_host_credential_ui_visible;
  bool unlock_ui_visible = interactive_state_ready
                               ? session_helper_report_unlock_ui_visible_
                               : (credential_ui_visible ||
                                  secure_desktop_active_);
  unlock_ui_visible = unlock_ui_visible || sas_secure_desktop_grace_active;
  bool interactive_secure_desktop_active =
      interactive_state_ready ? session_helper_report_secure_desktop_active_
                              : secure_desktop_active_;
  interactive_secure_desktop_active =
      interactive_secure_desktop_active || sas_secure_desktop_grace_active;
  bool interactive_logon_ui_visible =
      credential_ui_visible;
  bool interactive_session_locked = effective_session_locked ||
                                    interactive_lock_screen_visible ||
                                    unlock_ui_visible ||
                                    sas_secure_desktop_grace_active;
  std::string interactive_input_desktop = EscapeJsonString(
      interactive_state_ready ? session_helper_report_input_desktop_
                              : input_desktop_name_);
  std::string raw_interactive_stage = ResolveInteractiveStageLocked();
  std::string interactive_stage = EscapeJsonString(raw_interactive_stage);
  std::ostringstream stream;
  stream << "{\"ok\":true,\"service\":\"CrossDeskService\""
         << ",\"active_session_id\":" << active_session_id_
         << ",\"process_session_id\":" << process_session_id_
         << ",\"session_locked\":" << (session_locked_ ? "true" : "false")
         << ",\"interactive_state_ready\":"
         << (interactive_state_ready ? "true" : "false")
         << ",\"interactive_state_source\":\"" << interactive_state_source
         << "\""
         << ",\"interactive_session_locked\":"
         << (interactive_session_locked ? "true" : "false")
         << ",\"interactive_stage\":\"" << interactive_stage << "\""
         << ",\"interactive_input_desktop\":\"" << interactive_input_desktop
         << "\""
         << ",\"interactive_lock_screen_visible\":"
         << (interactive_lock_screen_visible ? "true" : "false")
         << ",\"interactive_logon_ui_visible\":"
         << (interactive_logon_ui_visible ? "true" : "false")
         << ",\"interactive_secure_desktop_active\":"
         << (interactive_secure_desktop_active ? "true" : "false")
         << ",\"sas_secure_desktop_grace_active\":"
         << (sas_secure_desktop_grace_active ? "true" : "false")
         << ",\"unlock_ui_visible\":" << (unlock_ui_visible ? "true" : "false")
         << ",\"credential_ui_visible\":"
         << (credential_ui_visible ? "true" : "false")
         << ",\"consent_ui_visible\":"
         << (consent_ui_visible_ ? "true" : "false")
         << ",\"password_box_visible\":"
         << (credential_ui_visible ? "true" : "false")
         << ",\"logon_ui_visible\":" << (logon_ui_visible_ ? "true" : "false")
         << ",\"secure_desktop_active\":"
         << (secure_desktop_active_ ? "true" : "false")
         << ",\"input_desktop_available\":"
         << (input_desktop_available_ ? "true" : "false")
         << ",\"input_desktop_error_code\":" << input_desktop_error_code_
         << ",\"input_desktop\":\"" << input_desktop << "\""
         << ",\"prelogin\":" << (prelogin_ ? "true" : "false")
         << ",\"session_user\":\"" << username_utf8 << "\""
         << ",\"session_helper_path\":\"" << session_helper_path << "\""
         << ",\"session_helper_running\":"
         << (session_helper_running_ ? "true" : "false")
         << ",\"session_helper_pid\":" << session_helper_process_id_
         << ",\"session_helper_session_id\":" << session_helper_session_id_
         << ",\"session_helper_exit_code\":" << session_helper_exit_code_
         << ",\"session_helper_last_error\":\"" << session_helper_last_error
         << "\""
         << ",\"session_helper_last_error_code\":"
         << session_helper_last_error_code_ << ",\"session_helper_status_ok\":"
         << (session_helper_status_ok_ ? "true" : "false")
         << ",\"session_helper_status_error\":\"" << session_helper_status_error
         << "\""
         << ",\"session_helper_status_error_code\":"
         << session_helper_status_error_code_
         << ",\"session_helper_report_session_id\":"
         << session_helper_report_session_id_
         << ",\"session_helper_report_process_id\":"
         << session_helper_report_process_id_
         << ",\"session_helper_report_session_locked\":"
         << (session_helper_report_session_locked_ ? "true" : "false")
         << ",\"session_helper_report_input_desktop_available\":"
         << (session_helper_report_input_desktop_available_ ? "true" : "false")
         << ",\"session_helper_report_input_desktop_error_code\":"
         << session_helper_report_input_desktop_error_code_
         << ",\"session_helper_report_input_desktop\":\""
         << helper_input_desktop << "\""
         << ",\"session_helper_report_lock_app_visible\":"
         << (session_helper_report_lock_app_visible_ ? "true" : "false")
         << ",\"session_helper_report_logon_ui_visible\":"
         << (session_helper_report_logon_ui_visible_ ? "true" : "false")
         << ",\"session_helper_report_consent_ui_visible\":"
         << (session_helper_report_consent_ui_visible_ ? "true" : "false")
         << ",\"session_helper_report_secure_desktop_active\":"
         << (session_helper_report_secure_desktop_active_ ? "true" : "false")
         << ",\"session_helper_report_credential_ui_visible\":"
         << (session_helper_report_credential_ui_visible_ ? "true" : "false")
         << ",\"session_helper_report_unlock_ui_visible\":"
         << (session_helper_report_unlock_ui_visible_ ? "true" : "false")
         << ",\"session_helper_report_interactive_stage\":\""
         << EscapeJsonString(session_helper_report_interactive_stage_) << "\""
         << ",\"session_helper_report_state_age_ms\":"
         << session_helper_report_state_age_ms_
         << ",\"session_helper_report_uptime_ms\":"
         << session_helper_report_uptime_ms_ << ",\"session_helper_uptime_ms\":"
         << (session_helper_started_at_tick_ >= started_at_tick_
                 ? (GetTickCount64() - session_helper_started_at_tick_)
                 : 0)
         << ",\"secure_input_helper_path\":\"" << secure_input_helper_path
         << "\""
         << ",\"secure_input_helper_running\":"
         << (secure_input_helper_running_ ? "true" : "false")
         << ",\"secure_input_helper_pid\":" << secure_input_helper_process_id_
         << ",\"secure_input_helper_session_id\":"
         << secure_input_helper_session_id_
         << ",\"secure_input_helper_exit_code\":"
         << secure_input_helper_exit_code_
         << ",\"secure_input_helper_last_error\":\""
         << secure_input_helper_last_error << "\""
         << ",\"secure_input_helper_last_error_code\":"
         << secure_input_helper_last_error_code_
         << ",\"secure_input_helper_stage\":\""
         << secure_input_helper_interactive_stage << "\""
         << ",\"secure_input_helper_desktop\":\""
         << secure_input_helper_interactive_desktop << "\""
         << ",\"secure_input_helper_uptime_ms\":"
         << (secure_input_helper_started_at_tick_ >= started_at_tick_
                 ? (GetTickCount64() - secure_input_helper_started_at_tick_)
                 : 0)
         << ",\"last_sas_success\":" << (last_sas_success_ ? "true" : "false")
         << ",\"last_sas_error\":\"" << last_sas_error << "\""
         << ",\"last_sas_error_code\":" << last_sas_error_code_
         << ",\"last_sas_uptime_ms\":"
         << (last_sas_tick_ >= started_at_tick_
                 ? (last_sas_tick_ - started_at_tick_)
                 : 0)
         << ",\"last_session_event\":\""
         << SessionEventToString(last_session_event_type_) << "\""
         << ",\"last_session_event_id\":" << last_session_event_type_
         << ",\"last_session_id\":" << last_session_event_session_id_
         << ",\"uptime_ms\":"
         << (GetTickCount64() >= started_at_tick_
                 ? (GetTickCount64() - started_at_tick_)
                 : 0)
         << "}";
  return stream.str();
}

std::string CrossDeskServiceHost::SendSecureAttentionSequence(
    DWORD client_session_id) {
  if (client_session_id == 0xFFFFFFFF ||
      client_session_id != WTSGetActiveConsoleSessionId()) {
    return BuildErrorJson("service_session_mismatch");
  }
  LOG_INFO("Received SAS request for session_id={}", client_session_id);
  SasResult result = SendSasNow();
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    const ULONGLONG now = GetTickCount64();
    last_sas_tick_ = now;
    last_sas_success_ = result.success;
    last_sas_error_code_ = result.error_code;
    last_sas_error_ = result.error;
    sas_secure_desktop_until_tick_ =
        result.success ? now + kSasSecureDesktopGraceMs : 0;
    sas_secure_desktop_seen_ = false;
  }

  if (!result.success) {
    return BuildErrorJson(result.error.c_str(), result.error_code);
  }
  WakeSessionState();
  return "{\"ok\":true,\"sent\":\"sas\"}";
}

std::string CrossDeskServiceHost::ResolveSecureInputTarget(
    DWORD client_session_id, SecureInputTarget& target) {
  bool helper_running = false;
  bool can_inject = false;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    target.session_id = active_session_id_;
    if (target.session_id == 0xFFFFFFFF) {
      return BuildErrorJson("no_active_console_session");
    }
    if (target.session_id != client_session_id ||
        client_session_id != WTSGetActiveConsoleSessionId()) {
      return BuildErrorJson("service_session_mismatch");
    }
    target.stage = ResolveInteractiveStageLocked();
    target.desktop = ResolveInteractiveDesktopLocked(target.stage);
    helper_running = secure_input_helper_running_ &&
                     secure_input_helper_session_id_ == target.session_id;
    can_inject = GetEffectiveSessionLockedLocked() || HasSecureInputUiLocked();
    if (target.stage == "user-desktop") {
      // Ordinary GUI input can be blocked by an elevated foreground window.
      // Only a Windows-validated client in the active console session may
      // start/retain this helper; do not misreport the desktop as UAC/locked.
      user_desktop_input_until_tick_ =
          GetTickCount64() + kUserDesktopInputIdleMs;
      can_inject = true;
    }
  }

  if (!can_inject) {
    WakeSessionState();
    return BuildErrorJson("secure_input_not_active");
  }

  if (!helper_running) {
    WakeSessionState();
    return BuildErrorJson("secure_input_helper_not_ready", ERROR_NOT_READY);
  }
  return {};
}

std::string CrossDeskServiceHost::CancelPendingConsent(
    DWORD client_session_id) {
  RefreshSessionState();
  if (client_session_id == 0xFFFFFFFF ||
      client_session_id != WTSGetActiveConsoleSessionId()) {
    return BuildErrorJson("service_session_mismatch");
  }

  DWORD consent_pid = 0;
  if (!FindProcessInSession(L"Consent.exe", active_session_id_,
                            &consent_pid)) {
    // Nothing is waiting for a decision; report success so the caller can stop
    // showing the prompt instead of surfacing a failure.
    return "{\"ok\":true,\"dismissed\":false}";
  }
  if (!IsProcessInWindowsSystemDirectory(consent_pid)) {
    LOG_WARN("Refusing to cancel consent: pid={} in session={} is not a system "
             "Consent.exe",
             consent_pid, active_session_id_);
    return BuildErrorJson("consent_image_path_rejected");
  }

  HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, consent_pid);
  if (process == nullptr) {
    const DWORD error = GetLastError();
    LOG_WARN("OpenProcess for consent pid={} failed, error={}", consent_pid,
             error);
    return BuildErrorJson("consent_open_failed", error);
  }

  const BOOL terminated = TerminateProcess(process, 0);
  const DWORD error = terminated == FALSE ? GetLastError() : 0;
  CloseHandle(process);
  if (terminated == FALSE) {
    LOG_WARN("TerminateProcess for consent pid={} failed, error={}",
             consent_pid, error);
    return BuildErrorJson("consent_terminate_failed", error);
  }

  LOG_INFO("Cancelled pending consent dialog: pid={}, session_id={}",
           consent_pid, active_session_id_);
  return "{\"ok\":true,\"dismissed\":true}";
}

std::string CrossDeskServiceHost::SendSecureDesktopKeyboardInput(
    DWORD client_session_id, int key_code, bool is_down,
    uint32_t scan_code, bool extended) {
  SecureInputTarget target;
  const auto error = ResolveSecureInputTarget(client_session_id, target);
  if (!error.empty()) return error;

  return QueryNamedPipeMessage(
      GetCrossDeskSecureInputHelperPipeName(target.session_id),
      BuildSecureInputHelperKeyboardCommand(key_code, is_down, scan_code,
                                            extended, target.stage,
                                            target.desktop),
      1000);
}

std::string CrossDeskServiceHost::SendSecureDesktopMouseInput(
    DWORD client_session_id, int x, int y, int wheel, int flag) {
  SecureInputTarget target;
  const auto error = ResolveSecureInputTarget(client_session_id, target);
  if (!error.empty()) return error;

  return QueryNamedPipeMessage(
      GetCrossDeskSecureInputHelperPipeName(target.session_id),
      BuildSecureInputHelperMouseCommand(x, y, wheel, flag, target.stage,
                                         target.desktop),
      1000);
}

bool InstallCrossDeskService(const std::wstring& binary_path) {
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (manager == nullptr) {
    LOG_ERROR("OpenSCManagerW failed, error={}", GetLastError());
    return false;
  }

  std::wstring service_command = L"\"" + binary_path + L"\" --service";
  const DWORD start_type =
      IsUnattendedEnabled() ? SERVICE_AUTO_START : SERVICE_DEMAND_START;
  SC_HANDLE service = CreateServiceW(
      manager, kCrossDeskServiceName, kCrossDeskServiceDisplayName,
      SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, start_type,
      SERVICE_ERROR_NORMAL, service_command.c_str(), nullptr, nullptr, nullptr,
      nullptr, nullptr);

  if (service == nullptr) {
    DWORD error = GetLastError();
    if (error != ERROR_SERVICE_EXISTS) {
      LOG_ERROR("CreateServiceW failed, error={}", error);
      CloseServiceHandle(manager);
      return false;
    }

    service = OpenServiceW(manager, kCrossDeskServiceName,
                           SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS |
                               SERVICE_START | READ_CONTROL | WRITE_DAC);
    if (service == nullptr) {
      LOG_ERROR("OpenServiceW failed, error={}", GetLastError());
      CloseServiceHandle(manager);
      return false;
    }

    if (!ChangeServiceConfigW(service, SERVICE_NO_CHANGE, start_type,
                              SERVICE_NO_CHANGE, service_command.c_str(),
                              nullptr, nullptr, nullptr, nullptr, nullptr,
                              kCrossDeskServiceDisplayName)) {
      LOG_ERROR("ChangeServiceConfigW failed, error={}", GetLastError());
      CloseServiceHandle(service);
      CloseServiceHandle(manager);
      return false;
    }
  }

  if (!GrantCrossDeskServiceStartAccessToAuthenticatedUsers(service)) {
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return false;
  }

  SC_ACTION actions[] = {{SC_ACTION_RESTART, 5000},
                         {SC_ACTION_RESTART, 15000},
                         {SC_ACTION_RESTART, 60000}};
  SERVICE_FAILURE_ACTIONSW recovery{};
  recovery.dwResetPeriod = 86400;
  recovery.cActions = static_cast<DWORD>(sizeof(actions) / sizeof(actions[0]));
  recovery.lpsaActions = actions;
  if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS,
                             &recovery)) {
    LOG_ERROR("Could not configure service recovery, error={}", GetLastError());
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return false;
  }

  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  return true;
}

bool IsCrossDeskServiceInstalled() {
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (manager == nullptr) {
    LOG_ERROR("OpenSCManagerW failed, error={}", GetLastError());
    return false;
  }

  SC_HANDLE service =
      OpenServiceW(manager, kCrossDeskServiceName, SERVICE_QUERY_STATUS);
  if (service == nullptr) {
    DWORD error = GetLastError();
    CloseServiceHandle(manager);
    if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
      return false;
    }
    if (error == ERROR_ACCESS_DENIED) {
      return true;
    }
    LOG_ERROR("OpenServiceW failed, error={}", error);
    return false;
  }

  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  return true;
}

bool StartCrossDeskService() {
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (manager == nullptr) {
    LOG_ERROR("OpenSCManagerW failed, error={}", GetLastError());
    return false;
  }

  SC_HANDLE service =
      OpenServiceW(manager, kCrossDeskServiceName, SERVICE_START);
  if (service == nullptr) {
    DWORD error = GetLastError();
    if (error != ERROR_SERVICE_DOES_NOT_EXIST) {
      LOG_ERROR("OpenServiceW failed, error={}", error);
    }
    CloseServiceHandle(manager);
    return false;
  }

  bool success = StartServiceW(service, 0, nullptr) != FALSE;
  DWORD error = success ? ERROR_SUCCESS : GetLastError();
  if (!success && error != ERROR_SERVICE_ALREADY_RUNNING) {
    LOG_ERROR("StartServiceW failed, error={}", error);
  }

  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  return success || error == ERROR_SERVICE_ALREADY_RUNNING;
}

bool StopCrossDeskService(DWORD timeout_ms) {
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (manager == nullptr) {
    LOG_ERROR("OpenSCManagerW failed, error={}", GetLastError());
    return false;
  }

  SC_HANDLE service = OpenServiceW(manager, kCrossDeskServiceName,
                                   SERVICE_STOP | SERVICE_QUERY_STATUS);
  if (service == nullptr) {
    DWORD error = GetLastError();
    CloseServiceHandle(manager);
    if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
      return true;
    }
    LOG_ERROR("OpenServiceW failed, error={}", error);
    return false;
  }

  SERVICE_STATUS_PROCESS status{};
  DWORD bytes_needed = 0;
  QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                       reinterpret_cast<LPBYTE>(&status), sizeof(status),
                       &bytes_needed);

  if (status.dwCurrentState != SERVICE_STOPPED &&
      status.dwCurrentState != SERVICE_STOP_PENDING) {
    SERVICE_STATUS service_status{};
    if (!ControlService(service, SERVICE_CONTROL_STOP, &service_status)) {
      DWORD error = GetLastError();
      if (error != ERROR_SERVICE_NOT_ACTIVE) {
        LOG_ERROR("ControlService failed, error={}", error);
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return false;
      }
    }
  }

  DWORD deadline = GetTickCount() + timeout_ms;
  while (GetTickCount() < deadline) {
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<LPBYTE>(&status), sizeof(status),
                              &bytes_needed)) {
      LOG_ERROR("QueryServiceStatusEx failed, error={}", GetLastError());
      CloseServiceHandle(service);
      CloseServiceHandle(manager);
      return false;
    }

    if (status.dwCurrentState == SERVICE_STOPPED) {
      CloseServiceHandle(service);
      CloseServiceHandle(manager);
      return true;
    }
    Sleep(200);
  }

  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  return false;
}

bool UninstallCrossDeskService() {
  if (!SetUnattendedEnabled(false)) return false;
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (manager == nullptr) {
    LOG_ERROR("OpenSCManagerW failed, error={}", GetLastError());
    return false;
  }

  SC_HANDLE service =
      OpenServiceW(manager, kCrossDeskServiceName,
                   DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);
  if (service == nullptr) {
    DWORD error = GetLastError();
    CloseServiceHandle(manager);
    if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
      return true;
    }
    LOG_ERROR("OpenServiceW failed, error={}", error);
    return false;
  }

  if (!StopCrossDeskService()) {
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return false;
  }

  bool success = DeleteService(service) != FALSE;
  if (!success) {
    LOG_ERROR("DeleteService failed, error={}", GetLastError());
  }

  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  return success;
}

std::string QueryCrossDeskService(const std::string& command,
                                  DWORD timeout_ms) {
  return QueryNamedPipeMessage(kCrossDeskServicePipeName, command, timeout_ms);
}

namespace {
std::string QueryDesktopInput(const std::string& command, DWORD timeout_ms) {
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  for (;;) {
    const auto now = GetTickCount64();
    if (now >= deadline) return BuildErrorJson("secure_input_helper_not_ready", ERROR_NOT_READY);
    const std::string response = QueryCrossDeskService(
        command, static_cast<DWORD>(deadline - now));
    const auto json = Json::parse(response, nullptr, false);
    if (!json.is_object() || json.value("ok", false)) return response;
    const std::string error = json.value("error", "");
    // Retry only explicit pre-injection rejections during helper startup.
    // Never replay an uncertain timeout or a partially injected mouse action.
    // The SYSTEM helper may now be starting for an elevated application on
    // Default too. Old services still reject that with secure_input_not_active;
    // preserve their ordinary-desktop return instead of retrying indefinitely.
    if (!IsDesktopInputSetupPending(error) ||
        (error == "secure_input_not_active" &&
         IsCurrentSessionUserDesktopActive())) {
      return response;
    }
    const auto remaining = pipe_deadline_detail::Remaining(deadline);
    if (remaining == 0) return response;
    Sleep((std::min)(DWORD{10}, remaining));
  }
}
}  // namespace

std::string SendCrossDeskSecureDesktopKeyInput(int key_code, bool is_down,
                                               uint32_t scan_code,
                                               bool extended,
                                               DWORD timeout_ms) {
  return QueryDesktopInput(BuildSecureDesktopKeyboardIpcCommand(
                                   key_code, is_down, scan_code, extended),
                               timeout_ms);
}

std::string SendCrossDeskSecureDesktopMouseInput(int x, int y, int wheel,
                                                 int flag, DWORD timeout_ms) {
  return QueryDesktopInput(
      BuildSecureDesktopMouseIpcCommand(x, y, wheel, flag), timeout_ms);
}

}  // namespace crossdesk
