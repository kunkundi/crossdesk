#include "platform/daemon_backend.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

namespace crossdesk::platform {
namespace {

std::atomic<bool> g_stop_requested{false};

}  // namespace

void ResetDaemonStop() { g_stop_requested.store(false); }

void RequestDaemonStop() { g_stop_requested.store(true); }

bool DaemonStopRequested() { return g_stop_requested.load(); }

bool PrepareDaemon() { return true; }

std::string GetDaemonExecutablePath() {
  wchar_t path[32768] = {};
  const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
  return length > 0 && length < 32768
             ? std::filesystem::path(path, path + length).u8string()
             : std::string();
}

DaemonChildResult RunDaemonChild(const std::string& executable_path,
                                 const std::function<bool()>& keep_running) {
  DaemonChildResult result;
  STARTUPINFOW startup = {sizeof(startup)};
  PROCESS_INFORMATION process = {};
  const auto path = std::filesystem::u8path(executable_path).wstring();
  std::wstring command = L"\"" + path + L"\" --child";
  if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
                      0, nullptr, nullptr, &startup, &process)) {
    return result;
  }

  result.started = true;
  while (keep_running()) {
    const DWORD wait_result = WaitForSingleObject(process.hProcess, 200);
    if (wait_result == WAIT_OBJECT_0 || wait_result == WAIT_FAILED) break;
  }
  if (!keep_running()) {
    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, 3000);
  }

  DWORD exit_code = 0;
  GetExitCodeProcess(process.hProcess, &exit_code);
  result.exit_code = static_cast<int>(exit_code);
  result.normal_exit = exit_code == 0;
  CloseHandle(process.hProcess);
  CloseHandle(process.hThread);
  return result;
}

}  // namespace crossdesk::platform
