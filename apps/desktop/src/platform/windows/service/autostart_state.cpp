#include "platform/windows/service/autostart_control.h"

#include "platform/autostart_backend.h"
#include "platform/windows/autostart_task.h"
#include "platform/windows/executable_path.h"
#include "service_host.h"
#include "shared_identity.h"
#include "unattended_config.h"

namespace crossdesk {
namespace {

std::wstring ServiceExecutable() {
  return platform::SiblingExecutablePath(L"crossdesk_service.exe").wstring();
}

bool SharedHostArmed() {
  return SupportsServiceAutostart() && IsUnattendedEnabled() &&
         IsSharedIdentityServiceConfigured() && SharedHostIdentity::Configured();
}

}  // namespace

bool IsServiceAutostartEnabled() {
  DWORD start_type = SERVICE_DEMAND_START;
  return SharedHostArmed() &&
         QueryCrossDeskServiceRegistration(ServiceExecutable(), nullptr, &start_type) !=
             ServiceRegistrationState::query_failed &&
         start_type == SERVICE_AUTO_START;
}

bool ServiceAutostartMatches(bool enabled) {
  DWORD start_type = SERVICE_DEMAND_START;
  if (!enabled) {
    bool unattended = false;
    if (!QueryUnattendedEnabled(unattended) || unattended) return false;
    const auto registration =
        QueryCrossDeskServiceRegistration(ServiceExecutable(), nullptr, &start_type);
    if (registration == ServiceRegistrationState::query_failed) return false;
    if (registration != ServiceRegistrationState::not_installed &&
        start_type != SERVICE_DEMAND_START && start_type != SERVICE_DISABLED)
      return false;
    // The flag can already be off after a partially failed disable. Only skip
    // the retry after checking the same registrations that cleanup removes.
    return platform::IsInstalledAutostartAbsent("CrossDesk");
  }
  return SharedHostArmed() &&
         QueryCrossDeskServiceRegistration(ServiceExecutable(), nullptr, &start_type) ==
             ServiceRegistrationState::current &&
         start_type == SERVICE_AUTO_START && platform::IsAutostartEnabled("CrossDesk");
}

}  // namespace crossdesk
