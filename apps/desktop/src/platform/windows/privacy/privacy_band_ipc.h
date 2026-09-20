/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-10
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PRIVACY_BAND_IPC_H_
#define _PRIVACY_BAND_IPC_H_

#include <Windows.h>

#include <cstdint>

namespace crossdesk {

constexpr uint32_t kPrivacyBandMagic = 0x43534231;
constexpr uint32_t kPrivacyBandVersion = 3;
constexpr DWORD kPrivacyWindowBand = 18;  // ZBID_ABOVELOCK_UX, current desktop.
constexpr size_t kPrivacyMaxMonitors = 64;
constexpr wchar_t kPrivacyBandBootstrap[] = L"CROSSDESK_PRIVACY_BAND_BOOTSTRAP";
constexpr wchar_t kPrivacyBandDll[] = L"crossdesk_privacy_window.dll";

struct PrivacyBandWindow {
  uint64_t hwnd = 0;
  RECT rect{};
};

// Unnamed mapping and synchronization/process handles are inherited only by
// the newly created broker. No global window-name/PID discovery or named IPC.
struct PrivacyBandShared {
  uint32_t magic = kPrivacyBandMagic;
  uint32_t version = kPrivacyBandVersion;
  uint32_t size = sizeof(PrivacyBandShared);
  DWORD parent_pid = 0;
  DWORD broker_pid = 0;
  volatile LONG sequence = 0;
  volatile LONG error = ERROR_IO_PENDING;
  volatile LONG active = 0;
  volatile LONG topology_pending = 0;
  alignas(8) volatile LONG64 heartbeat = 0;
  wchar_t operation[128]{};
  wchar_t unlock_hint[128]{};
  uint32_t count = 0;
  PrivacyBandWindow windows[kPrivacyMaxMonitors]{};
};

using CreatePrivacyWindowInBand = HWND(WINAPI*)(DWORD, ATOM, LPCWSTR, DWORD,
                                                int, int, int, int, HWND, HMENU,
                                                HINSTANCE, LPVOID, DWORD);
using GetPrivacyWindowBand = BOOL(WINAPI*)(HWND, DWORD*);

}  // namespace crossdesk

#endif  // _PRIVACY_BAND_IPC_H_
