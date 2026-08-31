/*
 * @Author: DI JUNKUN
 * @Date: 2025-07-16
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PATH_MANAGER_H_
#define _PATH_MANAGER_H_

#include <filesystem>
#include <string>

namespace crossdesk {

class PathManager {
 public:
  explicit PathManager(const std::string& app_name);

  std::filesystem::path GetConfigPath();

  std::filesystem::path GetCachePath();

  std::filesystem::path GetLogPath();

  bool CreateDirectories(const std::filesystem::path& p);

 private:
  std::string app_name_;
};
}  // namespace crossdesk
#endif
