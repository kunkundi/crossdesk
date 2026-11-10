#include "platform/windows/service/autostart_control.h"

#include <windows.h>

#include <filesystem>

#include "config_center.h"
#include "path_manager.h"
#include "platform/autostart_backend.h"
#include "platform/single_instance.h"
#include "platform/windows/autostart_task.h"
#include "platform/windows/executable_path.h"
#include "platform/windows/virtual_display/usbmmidd_virtual_display.h"
#include "rd_log.h"
#include "service_host.h"
#include "shared_identity.h"
#include "unattended_config.h"

namespace crossdesk {

bool SupportsServiceAutostart() {
  return platform::kAutostartRequiresElevation;
}

bool CleanupLegacyServiceAutostart() {
  if (!SupportsServiceAutostart()) return true;
  // Enabling sets the flag and disabling removes every task, so a task without
  // the flag predates service startup. Remove it rather than silently turning
  // it into pre-login access. With the flag set, only fold a Run entry back
  // into the elevated handoff task.
  return IsUnattendedEnabled() ? platform::MigrateLegacyAutostart("CrossDesk")
                               : platform::RemoveInstalledAutostart("CrossDesk");
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
  if (!enabled) {
    // Disarm the host without stopping the lock-screen service used by GUI
    // sessions. A missing service/binary must not prevent disabling startup.
    if (SetUnattendedEnabled(false) && SetCrossDeskServiceAutoStart(false) &&
        platform::RemoveInstalledAutostart("CrossDesk")) return true;
    error = "Could not disable unattended startup";
    return false;
  }

  const auto executable = platform::GetAutostartExecutablePath();
  const auto service = platform::SiblingExecutablePath(L"crossdesk_service.exe");
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
  DWORD start_type = SERVICE_DEMAND_START;
  const auto registration =
      QueryCrossDeskServiceRegistration(service.wstring(), nullptr, &start_type);
  if (registration == ServiceRegistrationState::query_failed) {
    error = "Could not query Windows service startup configuration";
    return false;
  }
  const bool current = IsSharedIdentityServiceConfigured() &&
                       registration == ServiceRegistrationState::current;
  if (!current && registration != ServiceRegistrationState::not_installed &&
      !StopCrossDeskService(20000)) {
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
  const bool restored_start = SetCrossDeskServiceAutoStart(start_type == SERVICE_AUTO_START);
  if (!restored_flag || !restored_start)
    LOG_ERROR("Could not restore the previous unattended startup configuration");
  error = "Could not enable persistent host startup and the user-session GUI";
  return false;
}

}  // namespace crossdesk
