/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-25
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#include "screen_lock.h"
#include "service/interactive_desktop.h"

#include <windows.h>

#include "rd_log.h"

namespace crossdesk {

ScreenLockState QueryScreenLock() {
  std::string reason;
  if (IsCurrentSessionUnlocked(&reason)) return ScreenLockState::unlocked;
  return reason.rfind("session_not_unlocked", 0) == 0 ? ScreenLockState::locked
                                                     : ScreenLockState::unknown;
}

bool LockScreen() {
  if (LockWorkStation()) return true;
  LOG_WARN("LockWorkStation failed: {}", GetLastError());
  return false;
}

}  // namespace crossdesk
