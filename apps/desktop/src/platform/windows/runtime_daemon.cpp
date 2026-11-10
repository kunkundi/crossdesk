#include "platform/windows/runtime_daemon.h"

#include "platform/windows/executable_path.h"

#include <shellapi.h>

#include <cerrno>
#include <cstdint>
#include <cwchar>
#include <limits>

namespace crossdesk::platform {
namespace {

constexpr DWORD kStartupTimeoutMs = 3000;
constexpr DWORD kRestartDelayMs = 1000;
constexpr wchar_t kWindowClass[] = L"CrossDesk.RuntimeDaemon";

struct Handle {
  HANDLE value = nullptr;
  ~Handle() { if (value) CloseHandle(value); }
};

struct Arguments {
  int count = 0;
  wchar_t** values = CommandLineToArgvW(GetCommandLineW(), &count);
  ~Arguments() { if (values) LocalFree(values); }
};

std::wstring QuoteArgument(const std::wstring& value) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (const wchar_t c : value) {
    if (c == L'\\') {
      ++slashes;
    } else {
      result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
      result += c;
      slashes = 0;
    }
  }
  result.append(slashes * 2, L'\\');
  return result + L'"';
}

bool Failed(const char* operation, std::string& error,
            DWORD code = GetLastError()) {
  error = std::string(operation) + " failed (Windows error " +
          std::to_string(code ? code : ERROR_GEN_FAILURE) + ")";
  return false;
}

HANDLE ParseHandle(const wchar_t* text) {
  wchar_t* end = nullptr;
  errno = 0;
  const auto value = std::wcstoull(text, &end, 10);
  if (errno || !text[0] || *end || text[0] == L'-' || !value ||
      value > (std::numeric_limits<uintptr_t>::max)()) return nullptr;
  return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
}

struct SessionWindow {
  HWND window = nullptr;
  bool query_pending = false;
  bool ending = false;

  ~SessionWindow() { if (window) DestroyWindow(window); }

  static LRESULT CALLBACK WindowProc(HWND window, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
      const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
      SetWindowLongPtrW(window, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* state = reinterpret_cast<SessionWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state && message == WM_QUERYENDSESSION) {
      state->query_pending = true;
      return TRUE;
    }
    if (state && message == WM_ENDSESSION) {
      state->query_pending = false;
      state->ending = wparam != FALSE;
      return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
  }

  bool Create(DWORD owner) {
    WNDCLASSW klass{};
    klass.lpfnWndProc = WindowProc;
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = kWindowClass;
    if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      return false;
    const auto title = std::to_wstring(owner);
    // Message-only windows do not receive session-end broadcasts.
    window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, title.c_str(),
                             WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                             klass.hInstance, this);
    return window != nullptr;
  }

  void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) ending = true;
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
};

}  // namespace

WindowsRuntimeDaemon& WindowsRuntimeDaemon::Instance() {
  static WindowsRuntimeDaemon daemon;
  return daemon;
}

WindowsRuntimeDaemon::~WindowsRuntimeDaemon() { Shutdown(); }

void WindowsRuntimeDaemon::Initialize() {
  std::lock_guard<std::mutex> lock(mutex_);
  initialized_ = true;
  executable_ = CurrentExecutablePath();
  arguments_.clear();
  Arguments arguments;
  if (!arguments.values) {
    executable_.clear();
    return;
  }
  for (int i = 1; i < arguments.count; ++i)
    arguments_.emplace_back(arguments.values[i]);
}

bool WindowsRuntimeDaemon::SetEnabled(bool enabled, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error.clear();
  // Config-only callers and the unattended service do not own a GUI watcher.
  if (!initialized_) return true;
  if (!enabled) return Stop(error);
  if (process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) return true;
  if (!Stop(error)) return false;
  if (executable_.empty())
    return Failed("Resolve daemon executable", error, ERROR_FILE_NOT_FOUND);

  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  Handle owner;
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(),
                       GetCurrentProcess(), &owner.value,
                       SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0))
    return Failed("Duplicate daemon owner", error);
  Handle stop{CreateEventW(&security, TRUE, FALSE, nullptr)};
  Handle ready{CreateEventW(&security, TRUE, FALSE, nullptr)};
  if (!stop.value || !ready.value) return Failed("Create daemon events", error);

  SIZE_T bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
  std::vector<unsigned char> attributes(bytes);
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.lpAttributeList =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
  if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes))
    return Failed("Initialize daemon handle list", error);
  // Inherit only these private handles, never GUI locks, sockets or files.
  HANDLE inherited[] = {owner.value, stop.value, ready.value};
  bool started = UpdateProcThreadAttribute(
      startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
      inherited, sizeof(inherited), nullptr, nullptr) != FALSE;
  PROCESS_INFORMATION child{};
  if (started) {
    std::wstring command = QuoteArgument(executable_) + L" --windows-daemon";
    for (HANDLE handle : inherited)
      command += L" " + std::to_wstring(reinterpret_cast<uintptr_t>(handle));
    for (const auto& argument : arguments_)
      command += L" " + QuoteArgument(argument);
    started = CreateProcessW(executable_.c_str(), command.data(), nullptr,
        nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
        nullptr, nullptr, &startup.StartupInfo, &child) != FALSE;
  }
  const DWORD code = GetLastError();
  DeleteProcThreadAttributeList(startup.lpAttributeList);
  if (!started) return Failed("Start daemon", error, code);
  CloseHandle(child.hThread);
  process_ = child.hProcess;
  stop_ = stop.value;
  stop.value = nullptr;
  HANDLE startup_events[] = {process_, ready.value};
  if (WaitForMultipleObjects(2, startup_events, FALSE, kStartupTimeoutMs) !=
      WAIT_OBJECT_0 + 1) {
    std::string cleanup;
    Stop(cleanup);
    return Failed("Wait for daemon readiness", error, ERROR_PROCESS_ABORTED);
  }
  return true;
}

bool WindowsRuntimeDaemon::Stop(std::string& error) {
  if (!process_) return true;
  SetEvent(stop_);
  if (WaitForSingleObject(process_, kStartupTimeoutMs) != WAIT_OBJECT_0) {
    // The handle still identifies our helper even if its PID has been reused.
    if (!TerminateProcess(process_, ERROR_PROCESS_ABORTED) ||
        WaitForSingleObject(process_, kStartupTimeoutMs) != WAIT_OBJECT_0)
      return Failed("Stop daemon", error);
  }
  CloseHandle(process_);
  CloseHandle(stop_);
  process_ = stop_ = nullptr;
  return true;
}

void WindowsRuntimeDaemon::Shutdown(bool restart) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!restart) {
    std::string error;
    Stop(error);
  } else {
    // Leave the helper armed until this process exits and releases its leases.
    if (process_) CloseHandle(process_);
    if (stop_) CloseHandle(stop_);
    process_ = stop_ = nullptr;
  }
}

int RunWindowsRuntimeDaemon() {
  Arguments arguments;
  if (!arguments.values || arguments.count < 5 ||
      std::wcscmp(arguments.values[1], L"--windows-daemon") != 0) return 1;
  Handle owner{ParseHandle(arguments.values[2])};
  Handle stop{ParseHandle(arguments.values[3])};
  Handle ready{ParseHandle(arguments.values[4])};
  const DWORD owner_id = GetProcessId(owner.value);
  if (!owner_id || owner_id == GetCurrentProcessId() || !stop.value ||
      !ready.value) return 1;
  for (HANDLE handle : {owner.value, stop.value, ready.value}) {
    DWORD flags = 0;
    if (!GetHandleInformation(handle, &flags) || !(flags & HANDLE_FLAG_INHERIT) ||
        !SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0)) return 1;
  }
  SessionWindow session;
  if (!session.Create(owner_id) || !SetEvent(ready.value)) return 1;

  bool exited = false;
  ULONGLONG restart_at = 0;
  HANDLE watched[] = {stop.value, owner.value};
  for (;;) {
    session.Pump();
    if (session.ending || WaitForSingleObject(stop.value, 0) == WAIT_OBJECT_0)
      return 0;
    const auto now = GetTickCount64();
    if (exited && now >= restart_at && !session.query_pending &&
        !GetSystemMetrics(SM_SHUTTINGDOWN)) break;
    const DWORD timeout = exited && now < restart_at
        ? static_cast<DWORD>(restart_at - now) : INFINITE;
    const DWORD count = exited ? 1 : 2;
    const DWORD result = MsgWaitForMultipleObjectsEx(
        count, watched, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (result == WAIT_OBJECT_0) return 0;
    if (!exited && result == WAIT_OBJECT_0 + 1) {
      exited = true;
      restart_at = GetTickCount64() + kRestartDelayMs;
    } else if (result != WAIT_OBJECT_0 + count && result != WAIT_TIMEOUT) {
      return 1;
    }
  }
  const auto executable = CurrentExecutablePath();
  if (executable.empty()) return 1;
  std::wstring command = QuoteArgument(executable);
  for (int i = 5; i < arguments.count; ++i)
    command += L" " + QuoteArgument(arguments.values[i]);
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION child{};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                      FALSE, 0, nullptr, nullptr, &startup, &child)) return 1;
  CloseHandle(child.hThread);
  CloseHandle(child.hProcess);
  return 0;
}

}  // namespace crossdesk::platform
