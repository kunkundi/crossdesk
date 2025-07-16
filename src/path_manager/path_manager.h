/*
 * @Author: DI JUNKUN
 * @Date: 2025-07-16
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PATH_MANAGER_H_
#define _PATH_MANAGER_H_

#include <filesystem>
#include <string>
#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

class PathManager {
 public:
  explicit PathManager(const std::string& app_name);

  std::filesystem::path GetConfigPath();

  std::filesystem::path GetCachePath();

  std::filesystem::path GetLogPath();

  bool CreateDirectories(const std::filesystem::path& p);

 private:
#ifdef _WIN32
  std::filesystem::path GetKnownFolder(REFKNOWNFOLDERID id);
#endif

  std::string GetHome();
  std::filesystem::path GetEnvOrDefault(const char* env_var,
                                        const std::string& def);

 private:
  std::string app_name_;
};

#endif