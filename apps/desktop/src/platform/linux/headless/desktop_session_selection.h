/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-25
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DESKTOP_SESSION_SELECTION_H_
#define _DESKTOP_SESSION_SELECTION_H_

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace crossdesk {

// Metadata for an online X11 user session, already filtered to our uid.
struct DesktopLoginSession {
  std::string display;
  bool current = false;
  bool primary = false;
  bool active = false;
  bool remote = false;
};

inline std::string NormalizeDesktopDisplay(std::string display) {
  if (display.compare(0, 5, "unix:") == 0) display.erase(0, 4);
  if (display.empty() || display[0] != ':') return display;
  if (display.size() > 2 && display.compare(display.size() - 2, 2, ".0") == 0) {
    display.resize(display.size() - 2);
  }
  return display;
}

inline int DesktopSessionPriority(
    const std::string& display,
    const std::vector<DesktopLoginSession>& sessions) {
  int priority = 0;
  for (const auto& session : sessions) {
    if (NormalizeDesktopDisplay(session.display) !=
        NormalizeDesktopDisplay(display)) continue;
    const int rank = session.current ? 6
                     : session.primary && session.active ? 5
                     : session.active && !session.remote ? 4
                     : session.active ? 3
                     : session.primary ? 2 : 1;
    priority = std::max(priority, rank);
  }
  return priority;
}

inline bool DesktopDisplayLess(const std::string& left,
                                const std::string& right) {
  const auto a = NormalizeDesktopDisplay(left);
  const auto b = NormalizeDesktopDisplay(right);
  const auto is_number = [](const std::string& value) {
    return value.size() > 1 && value[0] == ':' &&
           value.find_first_not_of("0123456789", 1) == std::string::npos;
  };
  // Stable numeric order when logind is unavailable or sessions have equal
  // priority; do not depend on filesystem enumeration order (or sort :10 < :2).
  if (is_number(a) && is_number(b) && a.size() != b.size()) {
    return a.size() < b.size();
  }
  return a < b;
}

inline std::optional<size_t> SelectDesktopSession(
    const std::vector<std::string>& available,
    const std::vector<DesktopLoginSession>& sessions) {
  if (available.empty()) return std::nullopt;
  size_t selected = 0;
  for (size_t i = 1; i < available.size(); ++i) {
    const int candidate = DesktopSessionPriority(available[i], sessions);
    const int current = DesktopSessionPriority(available[selected], sessions);
    if (candidate > current ||
        (candidate == current &&
         DesktopDisplayLess(available[i], available[selected]))) selected = i;
  }
  return selected;
}

}  // namespace crossdesk

#endif