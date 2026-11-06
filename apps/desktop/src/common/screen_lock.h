/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-25
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_LOCK_H_
#define _SCREEN_LOCK_H_

namespace crossdesk {

// Called on the application thread. True means the OS accepted the request,
// not that the asynchronous transition to the lock screen has finished.
bool LockScreen();
enum class ScreenLockState { unknown, unlocked, locked };
ScreenLockState QueryScreenLock();

}  // namespace crossdesk

#endif
