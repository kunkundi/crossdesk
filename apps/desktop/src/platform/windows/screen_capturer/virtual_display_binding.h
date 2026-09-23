/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-23
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VIRTUAL_DISPLAY_BINDING_H_
#define _VIRTUAL_DISPLAY_BINDING_H_

#include <string>
#include <vector>

#include "display_info.h"

namespace crossdesk {

// Only for headless takeover. Preserve the registered wire slot while choosing
// IDD by identity; primary status and the old phantom's slot are irrelevant.
inline int BindVirtualDisplayToStream(const std::vector<DisplayInfo>& current,
                                      const std::string& name, int slot,
                                      std::vector<DisplayInfo>* canonical,
                                      std::vector<int>* backend_to_canonical) {
  if (slot < 0 || slot >= static_cast<int>(canonical->size())) return -1;
  backend_to_canonical->assign(current.size(), -1);
  for (auto& display : *canonical) {
    display.handle = nullptr;
    display.left = display.top = display.right = display.bottom = 0;
    display.width = display.height = 0;
    display.is_primary = false;
  }
  for (size_t i = 0; i < current.size(); ++i) {
    if (current[i].name != name || current[i].width <= 0 ||
        current[i].height <= 0)
      continue;
    (*backend_to_canonical)[i] = slot;
    (*canonical)[slot] = current[i];
    return static_cast<int>(i);
  }
  return -1;
}

}  // namespace crossdesk

#endif