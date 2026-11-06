/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-25
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#include "screen_lock.h"

#include <dlfcn.h>
#include <ApplicationServices/ApplicationServices.h>

#include "rd_log.h"

namespace crossdesk {

ScreenLockState QueryScreenLock() {
  const auto session = CGSessionCopyCurrentDictionary();
  if (!session) return ScreenLockState::unknown;
  const auto console = CFDictionaryGetValue(session, kCGSessionOnConsoleKey);
  const auto locked = CFDictionaryGetValue(session, CFSTR("CGSSessionScreenIsLocked"));
  const auto state = locked == kCFBooleanTrue ? ScreenLockState::locked
      : console == kCFBooleanTrue ? ScreenLockState::unlocked : ScreenLockState::unknown;
  CFRelease(session);
  return state;
}

bool LockScreen() {
  // macOS has no public immediate-lock API. Resolve login.framework at
  // runtime so an OS without this private entry point fails gracefully.
  static void* framework = dlopen(
      "/System/Library/PrivateFrameworks/login.framework/login",
      RTLD_LAZY | RTLD_LOCAL);
  using LockScreenFunction = int (*)();
  static const auto lock = framework
      ? reinterpret_cast<LockScreenFunction>(
            dlsym(framework, "SACLockScreenImmediate"))
      : nullptr;
  if (!lock) {
    LOG_WARN("macOS screen lock API is unavailable");
    return false;
  }
  const int result = lock();
  if (result != 0) LOG_WARN("macOS screen lock request failed: {}", result);
  return result == 0;
}

}  // namespace crossdesk
