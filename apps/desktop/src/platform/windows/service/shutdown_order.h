/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-22
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SHUTDOWN_ORDER_H_
#define _SHUTDOWN_ORDER_H_

#include <Windows.h>

namespace crossdesk {

enum class WindowsShutdownRole { host, helper };

// Windows shuts processes down from high to low (the default is 0x280).
// Keep the remote host alive while ordinary applications ask the user to
// resolve a blocked shutdown. The helpers must outlive the host. Stay inside
// the application range; 0x000-0x0ff is reserved for the operating system.
inline constexpr DWORD WindowsShutdownLevel(WindowsShutdownRole role) {
  return role == WindowsShutdownRole::host ? 0x101 : 0x100;
}

inline DWORD ConfigureWindowsShutdownOrder(WindowsShutdownRole role) {
  // Change only the order, without vetoing shutdown or extending its timeout.
  // SCM shutdown notifications still govern the service-owned host/helpers;
  // these levels do not override an explicit stop from their owner.
  if (SetProcessShutdownParameters(WindowsShutdownLevel(role), 0))
    return ERROR_SUCCESS;
  const DWORD error = GetLastError();
  return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
}

}  // namespace crossdesk

#endif
