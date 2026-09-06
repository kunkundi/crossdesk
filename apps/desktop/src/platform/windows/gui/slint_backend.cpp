#include "platform/windows/gui/slint_backend.h"

#include <windows.h>

#include <cstdlib>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace crossdesk {
namespace {

struct HandleCloser {
  void operator()(HANDLE handle) const noexcept {
    if (handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
  }
};
using ScopedHandle = std::unique_ptr<void, HandleCloser>;

std::string ErrorCode(DWORD code) {
  std::ostringstream text;
  text << "0x" << std::hex << std::uppercase << code;
  return text.str();
}

std::string ProbeSlintRenderer(DWORD timeout_ms) {
  std::wstring executable(32768, L'\0');
  const DWORD length = GetModuleFileNameW(
      nullptr, executable.data(), static_cast<DWORD>(executable.size()));
  if (length == 0 || length >= executable.size()) {
    return "Unable to locate renderer probe executable";
  }
  executable.resize(length);

  // A job also cleans up the probe if the application is closed while the
  // graphics driver is stuck. Create it suspended so assignment cannot race.
  ScopedHandle job(CreateJobObjectW(nullptr, nullptr));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job ||
      !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                               &limits, sizeof(limits))) {
    return "Unable to create renderer probe job: " + ErrorCode(GetLastError());
  }

  constexpr std::string_view argument = kSlintRendererProbeArgument;
  std::wstring command = L"\"" + executable + L"\" ";
  command.append(argument.begin(), argument.end());
  STARTUPINFOW startup = {sizeof(startup)};
  PROCESS_INFORMATION process = {};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                      FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                      nullptr, &startup, &process)) {
    return "Unable to start renderer probe: " + ErrorCode(GetLastError());
  }
  ScopedHandle process_handle(process.hProcess);
  ScopedHandle thread_handle(process.hThread);
  if (!AssignProcessToJobObject(job.get(), process.hProcess) ||
      ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
    const DWORD error = GetLastError();
    TerminateProcess(process.hProcess, 1);
    return "Unable to run renderer probe: " + ErrorCode(error);
  }

  const DWORD wait = WaitForSingleObject(process.hProcess, timeout_ms);
  if (wait != WAIT_OBJECT_0) {
    // Closing the job kills only our probe, including any stuck driver thread.
    return wait == WAIT_TIMEOUT ? "Slint renderer probe timed out"
                                : "Waiting for renderer probe failed: " +
                                      ErrorCode(GetLastError());
  }
  DWORD exit_code = 0;
  if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
    return "Unable to read renderer probe result: " + ErrorCode(GetLastError());
  }
  return exit_code == 0
             ? std::string{}
             : "Slint renderer probe exited with " + ErrorCode(exit_code);
}

}  // namespace

SlintBackendSelection ConfigureWindowsSlintBackend(uint32_t probe_timeout_ms) {
  if (const char* requested = std::getenv("SLINT_BACKEND");
      requested && requested[0] != '\0') {
    return {true, requested, "Using explicitly requested Slint backend"};
  }

  // Slint creates its own GL context. A successful SDL/WGL capability check
  // cannot validate it, and a Rust panic cannot be caught by C++ exceptions.
  // Exercise Slint in a separate process without touching GL in this process.
  const std::string error = ProbeSlintRenderer(probe_timeout_ms);
  if (error.empty()) {
    return {true, "default", "Slint renderer probe rendered successfully"};
  }

  // _putenv_s updates the CRT and the OS environment read by slint_cpp.dll.
  // This is process-local so a later launch can retry hardware rendering.
  if (_putenv_s("SLINT_BACKEND", "winit-software") != 0) {
    return {false, {}, "Unable to select Slint software rendering: " + error};
  }
  return {true, "winit-software", error};
}

}  // namespace crossdesk
