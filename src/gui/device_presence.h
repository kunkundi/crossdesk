/*
 * @Author: DI JUNKUN
 * @Date: 2026-02-28
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_PRESENCE_H_
#define _DEVICE_PRESENCE_H_

#include <mutex>
#include <string>
#include <unordered_map>

class DevicePresence {
 public:
  void SetOnline(const std::string& device_id, bool online) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_[device_id] = online;
  }

  bool IsOnline(const std::string& device_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.count(device_id) > 0 && cache_.at(device_id);
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
  }

 private:
  std::unordered_map<std::string, bool> cache_;
  mutable std::mutex mutex_;
};

#endif