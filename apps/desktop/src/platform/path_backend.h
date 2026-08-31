#ifndef CROSSDESK_PLATFORM_PATH_BACKEND_H_
#define CROSSDESK_PLATFORM_PATH_BACKEND_H_

#include <filesystem>
#include <string>

namespace crossdesk::platform {

std::filesystem::path GetExecutableDirectory();
std::filesystem::path GetConfigPath(const std::string& app_name);
std::filesystem::path GetCachePath(const std::string& app_name);
std::filesystem::path GetLogPath(const std::string& app_name);

}  // namespace crossdesk::platform

#endif  // CROSSDESK_PLATFORM_PATH_BACKEND_H_
