/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-21
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PRIVACY_CAPTURE_EXCLUSION_H_
#define _PRIVACY_CAPTURE_EXCLUSION_H_

#include <Windows.h>

namespace crossdesk {

// WDA_EXCLUDEFROMCAPTURE: show the opaque cover locally while capture receives
// the desktop behind it. WDA_MONITOR would instead produce an empty/black area.
inline constexpr DWORD kPrivacyWindowDisplayAffinity = 0x00000011;

inline bool ExcludePrivacyWindowFromCapture(HWND window) {
  if (!SetWindowDisplayAffinity(window, kPrivacyWindowDisplayAffinity))
    return false;
  DWORD affinity = 0;
  if (!GetWindowDisplayAffinity(window, &affinity)) return false;
  if (affinity == kPrivacyWindowDisplayAffinity) return true;
  SetLastError(ERROR_INVALID_STATE);
  return false;
}

}  // namespace crossdesk

#endif