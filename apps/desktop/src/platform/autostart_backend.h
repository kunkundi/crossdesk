#ifndef CROSSDESK_PLATFORM_AUTOSTART_BACKEND_H_
#define CROSSDESK_PLATFORM_AUTOSTART_BACKEND_H_

#include <string>

namespace crossdesk::platform {

std::string GetAutostartExecutablePath();
bool EnableAutostart(const std::string& app_name,
                     const std::string& executable_path);
bool DisableAutostart(const std::string& app_name);
bool IsAutostartEnabled(const std::string& app_name);

}  // namespace crossdesk::platform

#endif  // CROSSDESK_PLATFORM_AUTOSTART_BACKEND_H_
