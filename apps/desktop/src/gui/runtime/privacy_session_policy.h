/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-15
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PRIVACY_SESSION_POLICY_H_
#define _PRIVACY_SESSION_POLICY_H_

#include <algorithm>
#include <string>
#include <unordered_map>

namespace crossdesk {

// Guarded by GuiRuntime's connection_status_mutex_. Privacy covers the local
// displays, so every connected controller must explicitly support its controls.
class PrivacySessionPolicy {
 public:
  // Web controllers render through the browser and expose no privacy UI.
  static bool IsWebController(const std::string& remote_id) {
    return remote_id.find("web") != std::string::npos;
  }

  bool Connected(const std::string& remote_id, bool automatic_privacy) {
    const auto [entry, inserted] = controllers_.try_emplace(remote_id, false);
    if (inserted && controllers_.size() == 1 && automatic_privacy &&
        !IsWebController(remote_id)) {
      automatic_controller_ = remote_id;
    }
    const auto early = early_support_.find(remote_id);
    if (early == early_support_.end()) return false;
    const bool supported = early->second;
    early_support_.erase(early);
    return SetSupported(remote_id, supported);
  }

  // Only transport-authenticated metadata from a connecting session is cached
  // by the caller. It grants no control until Connected admits that session.
  void RememberSupport(const std::string& remote_id, bool supported) {
    early_support_[remote_id] = supported;
  }

  void Disconnected(const std::string& remote_id) {
    controllers_.erase(remote_id);
    early_support_.erase(remote_id);
    if (automatic_controller_ == remote_id) automatic_controller_.clear();
  }

  // Consume automatic activation once, on the first controller's host info.
  // Repeated metadata or a later join must not undo an explicit privacy exit.
  bool SetSupported(const std::string& remote_id, bool supported) {
    const auto entry = controllers_.find(remote_id);
    if (entry == controllers_.end()) return false;
    entry->second = supported && !IsWebController(remote_id);
    if (automatic_controller_ != remote_id) return false;
    automatic_controller_.clear();
    return CanEnable();
  }

  bool Supports(const std::string& remote_id) const {
    const auto entry = controllers_.find(remote_id);
    return entry != controllers_.end() && entry->second;
  }

  bool CanEnable() const {
    return !controllers_.empty() &&
           std::all_of(controllers_.begin(), controllers_.end(),
                       [](const auto& entry) { return entry.second; });
  }

 private:
  std::unordered_map<std::string, bool> controllers_;
  std::unordered_map<std::string, bool> early_support_;
  std::string automatic_controller_;
};

}  // namespace crossdesk

#endif
