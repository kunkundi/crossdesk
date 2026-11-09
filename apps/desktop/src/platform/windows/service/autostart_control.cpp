#include "platform/windows/service/autostart_control.h"

#include <windows.h>

#include <filesystem>

#include "config_center.h"
#include "path_manager.h"
#include "platform/single_instance.h"
#include "platform/windows/autostart_task.h"
#include "platform/windows/virtual_display/usbmmidd_virtual_display.h"
#include "platform/autostart_backend.h"
#include "service_host.h"
#include "shared_identity.h"
#include "rd_log.h"
#include "unattended_config.h"

namespace crossdesk {
namespace {
struct ServiceHandle {
  SC_HANDLE value;
  ~ServiceHandle() { if (value) CloseServiceHandle(value); }
};

bool ReadServiceStartType(DWORD& start_type) {
  start_type = SERVICE_DEMAND_START;
  ServiceHandle manager{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
  if (!manager.value) return false;
  ServiceHandle service{OpenServiceW(manager.value, L"CrossDeskService", SERVICE_QUERY_CONFIG)};
  const DWORD open_error = service.value ? ERROR_SUCCESS : GetLastError();
  bool read = open_error == ERROR_SERVICE_DOES_NOT_EXIST;
  if (service.value) {
    alignas(QUERY_SERVICE_CONFIGW) BYTE buffer[8192]{};
    auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer);
    DWORD size = 0;
    read = QueryServiceConfigW(service.value, config, sizeof(buffer), &size) != FALSE;
    if (read) start_type = config->dwStartType;
  }
  return read;
}

bool ConfigureServiceAutostart(bool enabled) {
  ServiceHandle manager{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
  if (!manager.value) return false;
  ServiceHandle service{OpenServiceW(manager.value, L"CrossDeskService", SERVICE_CHANGE_CONFIG)};
  if (!service.value) return !enabled && GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST;
  return ChangeServiceConfigW(service.value, SERVICE_NO_CHANGE,
      enabled ? SERVICE_AUTO_START : SERVICE_DEMAND_START, SERVICE_NO_CHANGE,
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != FALSE;
}
}  // namespace

bool SupportsServiceAutostart() {
  return platform::kAutostartRequiresElevation;
}

bool IsServiceAutostartEnabled() {
  if (!SupportsServiceAutostart() || !IsUnattendedEnabled() ||
      !IsSharedIdentityServiceConfigured() || !SharedHostIdentity::Configured()) return false;
  DWORD start_type = 0;
  return ReadServiceStartType(start_type) && start_type == SERVICE_AUTO_START;
}

bool CleanupLegacyServiceAutostart() {
  return !SupportsServiceAutostart() || !IsServiceAutostartEnabled() ||
         platform::MigrateLegacyAutostart("CrossDesk");
}

bool SetServiceAutostart(bool enabled, std::string& error) {
  error.clear();
  if (enabled && !SupportsServiceAutostart()) {
    error = "Unattended startup requires the installed Release build";
    return false;
  }
  if (!IsAdministratorProcess()) {
    error = "Administrator privileges are required to configure boot startup";
    return false;
  }
  // GUI and CLI use the same operation and command lease. No second GUI
  // executable is launched, and saving settings never stops a current service.
  platform::SingleInstanceGuard command;
  if (command.TryAcquire(platform::InstanceRole::kServiceCli, error) !=
      platform::InstanceResult::kAcquired) {
    if (error.empty()) error = "Another service configuration is in progress";
    return false;
  }
  DWORD start_type = 0;
  if (!ReadServiceStartType(start_type)) {
    error = "Could not query Windows service startup configuration";
    return false;
  }
  if (!enabled) {
    // Disarm the host without stopping the lock-screen service used by GUI
    // sessions. A missing service/binary must not prevent disabling startup.
    if (SetUnattendedEnabled(false) && ConfigureServiceAutostart(false) &&
        platform::RemoveInstalledAutostart("CrossDesk")) return true;
    error = "Could not disable unattended startup";
    return false;
  }

  const auto executable = platform::GetAutostartExecutablePath();
  const auto service = std::filesystem::u8path(executable).parent_path() / L"crossdesk_service.exe";
  std::error_code file_error;
  if (executable.empty() || !std::filesystem::is_regular_file(service, file_error) ||
      !std::filesystem::is_regular_file(service.parent_path() / L"crossdesk_session_helper.exe", file_error)) {
    error = "The Windows service or session helper is missing. Repair the installation";
    return false;
  }
  platform::SingleInstanceGuard bootstrap_client;
  if (!SharedHostIdentity::GuiPresent() &&
      bootstrap_client.TryAcquire(platform::InstanceRole::kClient, error) !=
          platform::InstanceResult::kAcquired) {
    error = "Enable boot startup from the running GUI settings, or close the GUI first";
    return false;
  }
  const bool current = IsSharedIdentityServiceConfigured() &&
      QueryCrossDeskServiceRegistration(service.wstring()) == ServiceRegistrationState::current;
  if (!current && IsCrossDeskServiceInstalled() && !StopCrossDeskService(20000)) {
    error = "Could not stop the old service to configure shared identity";
    return false;
  }
  PathManager paths("CrossDesk");
  ResolveVirtualDisplayMode();  // Persist the console mode while enabling boot startup.
  ConfigCenter source((paths.GetCachePath() / "config.ini").string());
  if (!SharedHostIdentity::Prepare(paths.GetCachePath(), paths.GetConfigPath() / "trusted-devices",
                                   source.ExportSharedHostConfig(), error)) return false;
  const bool was_enabled = IsUnattendedEnabled();
  if (SetUnattendedEnabled(true) &&
      ((current && start_type == SERVICE_AUTO_START) || InstallCrossDeskService(service.wstring())) &&
      StartCrossDeskService() && platform::EnableAutostart("CrossDesk", executable)) return true;

  const bool restored_flag = SetUnattendedEnabled(was_enabled);
  const bool restored_start = ConfigureServiceAutostart(start_type == SERVICE_AUTO_START);
  if (!restored_flag || !restored_start)
    LOG_ERROR("Could not restore the previous unattended startup configuration");
  error = "Could not enable persistent host startup and the user-session GUI";
  return false;
}

}  // namespace crossdesk
