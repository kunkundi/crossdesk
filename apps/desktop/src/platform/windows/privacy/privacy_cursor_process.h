/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-08
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PRIVACY_CURSOR_PROCESS_H_
#define _PRIVACY_CURSOR_PROCESS_H_

#include <Windows.h>

#include <string>

namespace crossdesk {

// Prefer an independent lifetime, so closing the launcher's job cannot kill
// the cursor helper before it restores the cursor. Some launchers (including
// scheduled tasks) forbid breakaway. In that case retain the inherited parent
// process handle/stop event and run in the job; parent-only exits still trigger
// cleanup, although terminating the entire job also terminates the helper.
inline bool CreatePrivacyCursorProcess(const wchar_t* executable,
                                       const std::wstring& command,
                                       STARTUPINFOEXW& startup,
                                       PROCESS_INFORMATION& child,
                                       bool& used_job_fallback) {
  used_job_fallback = false;
  const auto create = [&](DWORD flags) {
    // CreateProcessW may modify its command line, including on failure.
    auto mutable_command = command;
    return CreateProcessW(executable, mutable_command.data(), nullptr, nullptr,
                          TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT |
                                    flags,
                          nullptr, nullptr, &startup.StartupInfo, &child) != FALSE;
  };
  if (create(CREATE_BREAKAWAY_FROM_JOB)) return true;
  const DWORD error = GetLastError();
  BOOL in_job = FALSE;
  if (error != ERROR_ACCESS_DENIED ||
      !IsProcessInJob(GetCurrentProcess(), nullptr, &in_job) || !in_job) {
    SetLastError(error);
    return false;
  }
  if (!create(0)) return false;
  used_job_fallback = true;
  return true;
}

}  // namespace crossdesk

#endif  // _PRIVACY_CURSOR_PROCESS_H_
