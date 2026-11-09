/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-08
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SHARED_IDENTITY_H_
#define _SHARED_IDENTITY_H_

#include <windows.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace crossdesk {

// Credential snapshots live in the administrator/SYSTEM-only machine root.
// Each process imports them into its own user-only identity store. Only SYSTEM
// takes the exclusive network lease. The GUI marker records frontend presence.
class SharedHostIdentity {
 public:
  ~SharedHostIdentity();
  static bool Configured();
  static bool GuiPresent();
  static bool Prepare(const std::filesystem::path& cache,
                      const std::filesystem::path& trusted_devices,
                      const std::string& host_config, std::string& error);
  static std::unique_ptr<SharedHostIdentity> AttachGui(
      const std::filesystem::path& cache,
      const std::filesystem::path& trusted_devices, bool import_existing,
      std::string& error);
  static std::unique_ptr<SharedHostIdentity> AcquireHost(std::string& error);

  bool Publish(const std::string& host_config, std::string& error);
  const std::string& HostConfig() const { return host_config_; }
  const std::filesystem::path& CachePath() const { return cache_; }
  const std::filesystem::path& TrustedDevicesPath() const { return trusted_; }

 private:
  SharedHostIdentity(std::filesystem::path cache, std::filesystem::path trusted,
                     HANDLE lease, bool gui);
  void Import();
  std::filesystem::path cache_, trusted_;
  HANDLE lease_ = INVALID_HANDLE_VALUE;
  bool gui_ = false;
  std::string host_config_;
  std::mutex mutex_;
};

}  // namespace crossdesk

#endif
