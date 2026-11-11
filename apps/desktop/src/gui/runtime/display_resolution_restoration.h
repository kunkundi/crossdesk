/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DISPLAY_RESOLUTION_RESTORATION_H_
#define _DISPLAY_RESOLUTION_RESTORATION_H_

#include <functional>
#include <map>
#include <string>
#include <utility>

#include <display_resolution.h>

namespace crossdesk {

// Application-thread state. Each callback owns the original native display
// mode, so restoration does not depend on capture still running or slot order.
class DisplayResolutionRestoration {
public:
  using Restore = std::function<bool()>;

  template <typename Capture, typename Change>
  bool Apply(const std::string &key, Capture capture, Change change) {
    if (originals_.count(key))
      return change();
    if (originals_.size() >= kMaxResolutionDisplays)
      return false;
    auto restore = capture();
    if (!restore)
      return false;
    // Save before changing anything, including a failed change whose native
    // rollback might fail. Subsequent requests never replace this baseline.
    originals_.emplace(key, std::move(restore));
    const bool accepted = change();
    if (!accepted && originals_.at(key)())
      originals_.erase(key);
    return accepted;
  }

  bool RestoreAll() {
    for (auto it = originals_.begin(); it != originals_.end();) {
      if (it->second())
        it = originals_.erase(it);
      else
        ++it;
    }
    return originals_.empty();
  }

  bool Empty() const { return originals_.empty(); }

private:
  std::map<std::string, Restore> originals_;
};

} // namespace crossdesk

#endif
