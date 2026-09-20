/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-21
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _STABLE_DISPLAY_SLOTS_H_
#define _STABLE_DISPLAY_SLOTS_H_

#include <algorithm>
#include <string>
#include <vector>

namespace crossdesk {

// Logical DisplayN slots live for one capture session. A missing monitor keeps
// its slot; newly attached outputs cannot replace it just by becoming primary.
class StableDisplaySlots {
 public:
  void Reset() { identities_.clear(); }

  std::vector<int> Update(const std::vector<std::string>& current) {
    for (const auto& identity : current) {
      if (std::find(identities_.begin(), identities_.end(), identity) ==
          identities_.end()) identities_.push_back(identity);
    }
    std::vector<int> indices(identities_.size(), -1);
    for (size_t slot = 0; slot < identities_.size(); ++slot) {
      const auto found = std::find(current.begin(), current.end(), identities_[slot]);
      if (found != current.end()) indices[slot] = static_cast<int>(found - current.begin());
    }
    return indices;
  }

 private:
  std::vector<std::string> identities_;
};

}  // namespace crossdesk

#endif