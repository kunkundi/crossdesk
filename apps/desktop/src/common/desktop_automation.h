/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-06
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DESKTOP_AUTOMATION_H_
#define _DESKTOP_AUTOMATION_H_

#include <chrono>
#include <deque>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace crossdesk {
struct DesktopSnapshot;
struct DesktopItem {
  nlohmann::json data;
  std::function<nlohmann::json(const std::string&, const std::string&)> perform;
  std::function<DesktopSnapshot(int, int)> inspect;
};
struct DesktopSnapshot {
  std::vector<DesktopItem> items;
  bool truncated = false;
};
// Platform functions execute on the desktop UI thread. References remain owned
// by a bounded, connection-scoped snapshot until expiry or a mutating action.
nlohmann::json DesktopCapabilities();
DesktopSnapshot DesktopWindows();
DesktopSnapshot DesktopTree(int pid, int limit, int depth);
nlohmann::json DesktopLaunch(const std::string& application);
nlohmann::json DesktopOpenUrl(const std::string& url);
std::string DesktopText(std::string text, size_t limit = 256);
class DesktopAutomation {
 public:
  nlohmann::json Execute(const std::string& owner, const nlohmann::json& request);
 private:
  struct Snapshot {
    std::string id, owner;
    std::chrono::steady_clock::time_point expires;
    DesktopSnapshot content;
  };
  std::deque<Snapshot> snapshots_;
  uint64_t sequence_ = 0;
};
} // namespace crossdesk

#endif
