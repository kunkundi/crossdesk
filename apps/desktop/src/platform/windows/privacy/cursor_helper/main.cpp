/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#include <Windows.h>

#include <cerrno>
#include <cstdint>
#include <cwchar>
#include <limits>

#include "../privacy_cursor_api.h"

namespace {
HANDLE ParseHandle(const wchar_t* text) {
  wchar_t* end = nullptr;
  errno = 0;
  const auto value = std::wcstoull(text, &end, 10);
  if (errno || !text[0] || *end || text[0] == L'-' || value == 0 ||
      value > (std::numeric_limits<uintptr_t>::max)())
    return nullptr;
  return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
}
DWORD FailureCode() {
  const DWORD code = GetLastError();
  return code ? code : ERROR_GEN_FAILURE;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 5 && argc != 6) return ERROR_INVALID_PARAMETER;
  HANDLE parent = ParseHandle(argv[1]);
  HANDLE stop = ParseHandle(argv[2]);
  HANDLE ready = ParseHandle(argv[3]);
  HANDLE mapping = ParseHandle(argv[4]);
  HANDLE capture_owner = argc == 6 ? ParseHandle(argv[5]) : nullptr;
  // Observe the inherited process OBJECT, never a reused PID or a process name.
  if (!parent || !stop || !ready || !mapping ||
      WaitForSingleObject(parent, 0) != WAIT_TIMEOUT ||
      WaitForSingleObject(stop, 0) != WAIT_TIMEOUT ||
      (argc == 6 && (!capture_owner ||
                    WaitForSingleObject(capture_owner, 0) != WAIT_TIMEOUT)))
    return ERROR_INVALID_HANDLE;
  auto shared = static_cast<crossdesk::PrivacyCursorReady*>(MapViewOfFile(
      mapping, FILE_MAP_WRITE, 0, 0, sizeof(crossdesk::PrivacyCursorReady)));
  if (!shared) return static_cast<int>(FailureCode());

  crossdesk::PrivacyCursorApi cursor;
  DWORD error = ERROR_SUCCESS;
  if (!cursor.Initialize() || !cursor.Hide()) error = FailureCode();
  InterlockedExchange(&shared->error, static_cast<LONG>(error));
  if (!SetEvent(ready)) error = FailureCode();
  if (!error) {
    HANDLE watched[] = {parent, stop, capture_owner};
    const DWORD count = capture_owner ? 3 : 2;
    for (;;) {
      const DWORD result =
          MsgWaitForMultipleObjects(count, watched, FALSE, 250, QS_ALLINPUT);
      if (result < WAIT_OBJECT_0 + count) break;
      if (result == WAIT_OBJECT_0 + count) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
          TranslateMessage(&message);
          DispatchMessageW(&message);
        }
      } else if (result != WAIT_TIMEOUT) {
        error = FailureCode();
        break;
      }
      if (!cursor.Hide()) {
        error = FailureCode();
        break;
      }
    }
  }
  // Also runs when the parent is force-terminated or crashes. The helper does
  // not own any input hooks or privacy HWNDs and never changes magnification.
  if (!cursor.RestoreAndClose()) error = FailureCode();
  UnmapViewOfFile(shared);
  CloseHandle(mapping);
  CloseHandle(ready);
  CloseHandle(stop);
  CloseHandle(parent);
  if (capture_owner) CloseHandle(capture_owner);
  return static_cast<int>(error);
}
