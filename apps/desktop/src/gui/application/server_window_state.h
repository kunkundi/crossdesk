/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SERVER_WINDOW_STATE_H_
#define _SERVER_WINDOW_STATE_H_

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace crossdesk::server_window_state {

inline int ReconcileSelectedController(const std::vector<std::string>& ids,
                                       std::string* selected_id) {
  if (!selected_id) {
    return 0;
  }
  if (ids.empty()) {
    selected_id->clear();
    return 0;
  }

  const auto selected = std::find(ids.begin(), ids.end(), *selected_id);
  if (selected == ids.end()) {
    *selected_id = ids.front();
    return 0;
  }
  return static_cast<int>(std::distance(ids.begin(), selected));
}
} // namespace crossdesk::server_window_state

#endif