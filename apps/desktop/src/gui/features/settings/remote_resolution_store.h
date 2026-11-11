/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _REMOTE_RESOLUTION_STORE_H_
#define _REMOTE_RESOLUTION_STORE_H_

#include <filesystem>
#include <map>
#include <mutex>

#include <display_resolution.h>

namespace crossdesk {

class RemoteResolutionStore {
public:
  bool Load(const std::filesystem::path &path);
  std::vector<DisplayResolutionPreference> Get(const std::string &device) const;
  bool Remember(const std::string &device,
                const DisplayResolutionPreference &preference);

private:
  mutable std::mutex mutex_;
  std::filesystem::path path_;
  std::map<std::string, std::vector<DisplayResolutionPreference>> devices_;
};

} // namespace crossdesk

#endif
