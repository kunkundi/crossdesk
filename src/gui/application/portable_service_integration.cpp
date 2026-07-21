#include "application/gui_application.h"

#if _WIN32 && CROSSDESK_PORTABLE

#include <Windows.h>
#include <shellapi.h>

#include <filesystem>
#include <vector>

#include "rd_log.h"
#include "service_host.h"

namespace crossdesk {
namespace {

std::filesystem::path GetCurrentExecutablePath() {
  std::vector<wchar_t> buffer(MAX_PATH);
  while (true) {
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return {};
    }
    if (length < buffer.size()) {
      return std::filesystem::path(buffer.data(), buffer.data() + length);
    }
    if (buffer.size() >= 32768) {
      return {};
    }
    buffer.resize(buffer.size() * 2);
  }
}

bool InstallServiceWithElevation() {
  const std::filesystem::path executable_path = GetCurrentExecutablePath();
  if (executable_path.empty()) {
    LOG_ERROR("Portable service install failed: current executable not found");
    return false;
  }

  const std::filesystem::path service_path =
      executable_path.parent_path() / L"crossdesk_service.exe";
  const std::filesystem::path helper_path =
      executable_path.parent_path() / L"crossdesk_session_helper.exe";
  if (!std::filesystem::exists(service_path) ||
      !std::filesystem::exists(helper_path)) {
    LOG_ERROR("Portable service install failed: service binaries missing, "
              "service={}, helper={}",
              service_path.string(), helper_path.string());
    return false;
  }

  const std::wstring executable = executable_path.wstring();
  const std::wstring working_dir = executable_path.parent_path().wstring();
  constexpr wchar_t parameters[] = L"--service-install";

  SHELLEXECUTEINFOW execute_info{};
  execute_info.cbSize = sizeof(execute_info);
  execute_info.fMask = SEE_MASK_NOCLOSEPROCESS;
  execute_info.lpVerb = L"runas";
  execute_info.lpFile = executable.c_str();
  execute_info.lpParameters = parameters;
  execute_info.lpDirectory = working_dir.c_str();
  execute_info.nShow = SW_HIDE;

  if (!ShellExecuteExW(&execute_info)) {
    LOG_ERROR("Portable service install failed: ShellExecuteExW error={}",
              GetLastError());
    return false;
  }

  const DWORD wait_result =
      WaitForSingleObject(execute_info.hProcess, INFINITE);
  DWORD exit_code = 1;
  if (wait_result == WAIT_OBJECT_0) {
    GetExitCodeProcess(execute_info.hProcess, &exit_code);
  } else {
    LOG_ERROR("Portable service install wait failed, result={}", wait_result);
  }
  CloseHandle(execute_info.hProcess);

  if (exit_code != 0) {
    LOG_ERROR("Portable service install command failed, exit_code={}",
              exit_code);
    return false;
  }

  const bool started = StartCrossDeskService();
  if (!started) {
    LOG_WARN("Portable service installed but start failed");
  }
  return IsCrossDeskServiceInstalled() && started;
}

} // namespace

void GuiApplication::CheckPortableWindowsService() {
  if (portable_service_prompt_checked_) {
    return;
  }
  portable_service_prompt_checked_ = true;
  portable_service_installed_ = IsCrossDeskServiceInstalled();
  if (portable_service_installed_ || portable_service_prompt_suppressed_) {
    return;
  }

  portable_service_install_state_.store(PortableServiceInstallState::idle,
                                        std::memory_order_relaxed);
  show_portable_service_install_window_ = true;
}

void GuiApplication::StartPortableWindowsServiceInstall() {
  portable_service_do_not_remind_ = false;
  PortableServiceInstallState expected = PortableServiceInstallState::idle;
  if (!portable_service_install_state_.compare_exchange_strong(
          expected, PortableServiceInstallState::installing,
          std::memory_order_acq_rel)) {
    if (expected != PortableServiceInstallState::failed) {
      return;
    }
    portable_service_install_state_.store(
        PortableServiceInstallState::installing, std::memory_order_release);
  }

  JoinPortableWindowsServiceInstallThread();
  portable_service_install_thread_ = std::thread([this]() {
    const bool installed = InstallServiceWithElevation();
    portable_service_install_state_.store(
        installed ? PortableServiceInstallState::succeeded
                  : PortableServiceInstallState::failed,
        std::memory_order_release);
  });
}

void GuiApplication::JoinPortableWindowsServiceInstallThread() {
  if (portable_service_install_thread_.joinable()) {
    portable_service_install_thread_.join();
  }
}

} // namespace crossdesk

#endif
