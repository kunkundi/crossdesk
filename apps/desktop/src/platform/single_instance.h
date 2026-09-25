/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-26
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SINGLE_INSTANCE_H_
#define _SINGLE_INSTANCE_H_

#include <cstdint>
#include <string>

namespace crossdesk::platform {

enum class InstanceRole { kLauncher, kClient, kServiceCli };
enum class InstanceResult { kAcquired, kAlreadyRunning, kError };

// Per-user ownership, independent of the install/portable directory. The OS
// releases ownership on process exit, including crashes; no PID files to age out.
class SingleInstanceGuard {
 public:
  explicit SingleInstanceGuard(std::string application = "CrossDesk");
  ~SingleInstanceGuard();
  SingleInstanceGuard(const SingleInstanceGuard&) = delete;
  SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

  InstanceResult TryAcquire(InstanceRole role, std::string& error);
  void Release();

 private:
  std::string application_;
  std::intptr_t handle_ = -1;
};

inline const char* InstanceRoleName(InstanceRole role) {
  switch (role) {
    case InstanceRole::kLauncher: return "launcher";
    case InstanceRole::kClient: return "client";
    case InstanceRole::kServiceCli: return "service-cli";
  }
  return "client";
}

inline bool ValidInstanceApplication(const std::string& application) {
  return !application.empty() && application.size() <= 100 &&
         application.find_first_not_of(
             "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") ==
             std::string::npos;
}

}  // namespace crossdesk::platform

#endif