#include "platform/daemon_backend.h"

#include <windows.h>

#include <atomic>
#include <chrono>
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
  char path[32768] = {};
  const DWORD length = GetModuleFileNameA(nullptr, path, sizeof(path));
  return length > 0 && length < sizeof(path) ? std::string(path, length)
                                             : std::string();
}

DaemonChildResult RunDaemonChild(const std::string& executable_path,
                                 const std::function<bool()>& keep_running) {
  DaemonChildResult result;
  STARTUPINFOA startup = {sizeof(startup)};
  PROCESS_INFORMATION process = {};
  std::string command = "\"" + executable_path + "\" --child";
  std::vector<char> command_buffer(command.begin(), command.end());
  command_buffer.push_back('\0');
  if (!CreateProcessA(nullptr, command_buffer.data(), nullptr, nullptr, FALSE,
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
