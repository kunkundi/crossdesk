/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-19
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WINDOWS_INPUT_INJECTOR_H_
#define _WINDOWS_INPUT_INJECTOR_H_

#include <Windows.h>

namespace crossdesk {

// Executes on a worker without windows or hooks, attached to this process's
// current user input desktop. Returns a SendInput count and preserves the
// worker's failure code in the calling thread's last-error value.
UINT SendInputOnUserDesktop(const INPUT& input);

}  // namespace crossdesk

#endif