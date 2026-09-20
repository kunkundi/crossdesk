/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-21
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PRIVACY_TOPOLOGY_H_
#define _PRIVACY_TOPOLOGY_H_

#include <algorithm>
#include <vector>

#include "privacy_band_ipc.h"

namespace crossdesk {

inline bool SameRect(const RECT& a, const RECT& b) {
  return a.left == b.left && a.top == b.top && a.right == b.right &&
         a.bottom == b.bottom;
}

inline bool MatchPrivacyTopology(const std::vector<RECT>& monitors,
                                 const std::vector<PrivacyBandWindow>& covers) {
  return monitors.size() == covers.size() &&
      std::all_of(monitors.begin(), monitors.end(), [&](const RECT& monitor) {
        return std::any_of(covers.begin(), covers.end(), [&](const auto& cover) {
          return SameRect(cover.rect, monitor);
        });
      });
}

}  // namespace crossdesk

#endif