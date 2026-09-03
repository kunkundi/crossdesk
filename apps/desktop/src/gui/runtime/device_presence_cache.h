/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_PRESENCE_CACHE_H_
#define _DEVICE_PRESENCE_CACHE_H_

#include <mutex>
#include <string>
#include <unordered_map>

namespace crossdesk {

class DevicePresenceCache {
public:
  void SetOnline(const std::string &device_id, bool online) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_[device_id] = online;
  }

  bool IsOnline(const std::string &device_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = cache_.find(device_id);
    return it != cache_.end() && it->second;
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
  }

private:
  std::unordered_map<std::string, bool> cache_;
  mutable std::mutex mutex_;
};

} // namespace crossdesk

#endif