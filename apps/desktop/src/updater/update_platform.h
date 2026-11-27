/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-20
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _UPDATE_PLATFORM_H_
#define _UPDATE_PLATFORM_H_

#include "updater/update_core.h"
#include <memory>
#include <vector>

namespace crossdesk::updater {
fs::path ExecutablePath();
fs::path HelperPath(const fs::path& application);
bool SupportedInstallation(const fs::path& application);
bool IsPrivileged();
bool OwnedJobDirectory(const fs::path& job);
void Launch(const fs::path& executable, const std::vector<std::string>& arguments);
int Run(const fs::path& executable, const std::vector<std::string>& arguments,
        bool elevate = false);
int RunElevated(const fs::path& executable, const fs::path& job);
void WaitForApplicationExit(const fs::path& application);
void InstallPackage(const fs::path& package, const fs::path& application);
std::vector<std::string> RestartArguments();
void Restart(const fs::path& application, const std::vector<std::string>& arguments);
void RestoreServices();
// Pin relocated helper files against changes during elevation (Windows).
std::shared_ptr<void> PinHelper(const fs::path& helper);

class MaintenanceLock {
 public:
  explicit MaintenanceLock(const fs::path& application);
  ~MaintenanceLock();
  MaintenanceLock(const MaintenanceLock&) = delete;
  MaintenanceLock& operator=(const MaintenanceLock&) = delete;
 private:
  std::intptr_t handle_ = -1;
#ifndef _WIN32
  int status_reader_ = -1;
#endif
};
bool MaintenanceActive(const fs::path& application);
}  // namespace crossdesk::updater

#endif
