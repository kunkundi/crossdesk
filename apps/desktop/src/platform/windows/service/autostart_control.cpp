#include "platform/windows/service/autostart_control.h"

#include <windows.h>

#include <cstring>
#include <filesystem>

#include "SimpleIni.h"
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

namespace {

enum class StartupChange { enable, disable, migrate_at_install, migrate_in_gui };

bool ApplyServiceAutostart(StartupChange change, std::string& error) {
  error.clear();
  const bool migrate = change == StartupChange::migrate_at_install ||
                       change == StartupChange::migrate_in_gui;
  if (!SupportsServiceAutostart()) {
    if (migrate) return true;
    if (change == StartupChange::enable) {
      error = "Unattended startup requires the installed Release build";
      return false;
    }
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
  if (change == StartupChange::disable) {
    // Disarm the host without stopping the lock-screen service used by GUI
    // sessions. A missing service/binary must not prevent disabling startup.
    if (SetUnattendedEnabled(false) && SetCrossDeskServiceAutoStart(false) &&
        platform::RemoveInstalledAutostart("CrossDesk")) return true;
    error = "Could not disable unattended startup";
    return false;
  }

  std::optional<bool> previous;
  if (!QueryUnattendedPreference(previous)) {
    error = "Could not read the unattended startup preference";
    return false;
  }
  PathManager paths("CrossDesk");
  const auto config = paths.GetCachePath() / "config.ini";
  if (migrate) {
    // A machine-level decision (including a later CLI disable) supersedes the
    // older per-user switch. Only an explicit legacy opt-in promotes a host.
    if (previous == false) {
      if (platform::RemoveInstalledAutostart("CrossDesk")) return true;
      error = "Could not clean up disabled login startup";
      return false;
    }
    if (previous == true) {
      if (!platform::MigrateLegacyAutostart("CrossDesk")) {
        error = "Could not update the existing login entry";
        return false;
      }
      if (ServiceAutostartMatches(true)) return true;
    }
    std::error_code ec;
    if (!std::filesystem::exists(config, ec)) {
      if (!ec) return true;
      error = ec.message();
      return false;
    }
    CSimpleIniA legacy;
    legacy.SetUnicode(true);
    if (legacy.LoadFile(config.c_str()) < 0) {
      error = "Could not read the previous CrossDesk settings";
      return false;
    }
    // SimpleIni's generic boolean reader also accepts truncated or malformed
    // words by their first letter. Enable pre-login access only for a saved true.
    const char* saved = legacy.GetValue("Settings", "enable_autostart", "");
    if (_stricmp(saved, "true") != 0 && std::strcmp(saved, "1") != 0) return true;
  }

  const auto executable = platform::GetAutostartExecutablePath();
  const auto service = platform::SiblingExecutablePath(L"crossdesk_service.exe");
  std::error_code file_error;
  if (executable.empty() || !std::filesystem::is_regular_file(service, file_error) ||
      !std::filesystem::is_regular_file(service.parent_path() / L"crossdesk_session_helper.exe", file_error)) {
    error = "The Windows service or session helper is missing. Repair the installation";
    return false;
  }
  // An old credential cache may need conversion by the GUI. Preserve the
  // user's login opt-in so that login itself can finish the unattended setup.
  if (migrate && !platform::EnableAutostart("CrossDesk", executable)) {
    error = "Could not configure the user-session login task";
    return false;
  }
  platform::SingleInstanceGuard bootstrap_client;
  if (change != StartupChange::migrate_in_gui && !SharedHostIdentity::GuiPresent() &&
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
  ConfigCenter source(config.string());
  if (!SharedHostIdentity::Prepare(paths.GetCachePath(), paths.GetConfigPath() / "trusted-devices",
                                   source.ExportSharedHostConfig(), error)) return false;
  if (!current && registration != ServiceRegistrationState::not_installed &&
      !StopCrossDeskService(20000)) {
    error = "Could not stop the old service to configure shared identity";
    return false;
  }
  ResolveVirtualDisplayMode();  // Persist the console mode while enabling boot startup.
  if (SetUnattendedEnabled(true) &&
      ((current && start_type == SERVICE_AUTO_START) || InstallCrossDeskService(service.wstring())) &&
      StartCrossDeskService() &&
      (migrate || platform::EnableAutostart("CrossDesk", executable))) return true;

  const bool restored_flag = SetUnattendedEnabled(previous);
  const bool restored_start = SetCrossDeskServiceAutoStart(start_type == SERVICE_AUTO_START);
  if (!restored_flag || !restored_start)
    LOG_ERROR("Could not restore the previous unattended startup configuration");
  error = "Could not enable persistent host startup and the user-session GUI";
  return false;
}

}  // namespace

bool SetServiceAutostart(bool enabled, std::string& error) {
  return ApplyServiceAutostart(enabled ? StartupChange::enable : StartupChange::disable, error);
}

bool MigrateServiceAutostart(bool running_gui) {
  std::string error;
  if (ApplyServiceAutostart(running_gui ? StartupChange::migrate_in_gui
                                       : StartupChange::migrate_at_install, error)) return true;
  LOG_WARN("Could not migrate the previous autostart setting: {}", error);
  return false;
}

}  // namespace crossdesk
