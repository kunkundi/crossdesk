#include "autostart.h"

#include "platform/autostart_backend.h"

namespace crossdesk {

bool EnableAutostart(const std::string& app_name) {
  const std::string executable_path = platform::GetAutostartExecutablePath();
  return !executable_path.empty() &&
         platform::EnableAutostart(app_name, executable_path);
}

bool DisableAutostart(const std::string& app_name) {
  return platform::DisableAutostart(app_name);
}

bool IsAutostartEnabled(const std::string& app_name) {
  return platform::IsAutostartEnabled(app_name);
}

}  // namespace crossdesk
