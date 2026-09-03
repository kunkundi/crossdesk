/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CURSOR_STATE_PROVIDER_H_
#define _CURSOR_STATE_PROVIDER_H_

#include <memory>
#include <vector>

#include <remote_action.h>

#include "display_info.h"

namespace crossdesk {

// Samples the cursor that is currently displayed by the controlled desktop
// and converts platform-specific cursor handles to protocol cursor shapes.
class CursorStateProvider {
 public:
  CursorStateProvider();
  ~CursorStateProvider();

  CursorStateProvider(const CursorStateProvider&) = delete;
  CursorStateProvider& operator=(const CursorStateProvider&) = delete;

  bool Sample(const std::vector<DisplayInfo>& displays,
              int preferred_display, CursorState* state);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crossdesk

#endif