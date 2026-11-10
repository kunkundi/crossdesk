/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_PRESENCE_CACHE_H_
#define _DEVICE_PRESENCE_CACHE_H_

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace crossdesk {

class DevicePresenceCache {
public:
  using Clock = std::chrono::steady_clock;
  static constexpr auto kMaxAge = std::chrono::seconds(60);

  void SetSignalConnected(bool connected) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Managed-host heartbeats repeat the current state every 250 ms. They
    // must not discard server presence or refresh its age.
    if (signal_connected_ == connected) return;
    signal_connected_ = connected;
    cache_.clear();
  }

  void SetOnline(const std::string &device_id, bool online,
                 Clock::time_point now = Clock::now()) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (signal_connected_) {
      cache_[device_id] = {online, now};
    }
  }

  std::optional<bool> GetOnline(const std::string &device_id,
                               Clock::time_point now = Clock::now()) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = cache_.find(device_id);
    if (!signal_connected_ || it == cache_.end() ||
        now - it->second.updated_at >= kMaxAge)
      return std::nullopt;
    return it->second.online;
  }

  bool IsOnline(const std::string &device_id,
                Clock::time_point now = Clock::now()) const {
    return GetOnline(device_id, now).value_or(false);
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
  }

private:
  struct Entry {
    bool online;
    Clock::time_point updated_at;
  };
  bool signal_connected_ = false;
  std::unordered_map<std::string, Entry> cache_;
  mutable std::mutex mutex_;
};

} // namespace crossdesk

#endif
