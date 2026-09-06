/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-07
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _NAMED_PIPE_DEADLINE_H_
#define _NAMED_PIPE_DEADLINE_H_

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace crossdesk {

namespace pipe_deadline_detail {
struct Handle {
  explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  HANDLE value = INVALID_HANDLE_VALUE;
  ~Handle() {
    if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value);
  }
};

inline DWORD Remaining(ULONGLONG deadline) {
  const ULONGLONG now = GetTickCount64();
  return now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
}

inline bool Transfer(HANDLE pipe, HANDLE event, bool write, void* data,
                     DWORD size, ULONGLONG deadline, DWORD* transferred,
                     DWORD* error) {
  *transferred = 0;
  if (Remaining(deadline) == 0) {
    *error = ERROR_SEM_TIMEOUT;
    return false;
  }
  ResetEvent(event);
  OVERLAPPED operation{};
  operation.hEvent = event;
  const BOOL complete =
      write ? WriteFile(pipe, data, size, transferred, &operation)
            : ReadFile(pipe, data, size, transferred, &operation);
  *error = complete ? ERROR_SUCCESS : GetLastError();
  if (*error == ERROR_IO_PENDING) {
    const DWORD wait = WaitForSingleObject(event, Remaining(deadline));
    if (wait != WAIT_OBJECT_0) {
      *error = wait == WAIT_TIMEOUT ? ERROR_SEM_TIMEOUT : GetLastError();
      // Keep the OVERLAPPED and buffer alive until cancellation completes.
      CancelIoEx(pipe, &operation);
      DWORD ignored = 0;
      GetOverlappedResult(pipe, &operation, &ignored, TRUE);
      return false;
    }
  } else if (*error != ERROR_SUCCESS && *error != ERROR_MORE_DATA) {
    return false;
  }
  // Immediate MORE_DATA also needs GetOverlappedResult: ReadFile may leave
  // its byte-count output at zero even though part of the message was read.
  const BOOL result = GetOverlappedResult(pipe, &operation, transferred, FALSE);
  *error = result ? ERROR_SUCCESS : GetLastError();
  return result != FALSE;
}
}  // namespace pipe_deadline_detail

// One deadline covers connection, write, and every response chunk. A connect
// timeout alone does not bound a synchronous ReadFile on an unresponsive peer.
inline bool QueryNamedPipeWithDeadline(const std::wstring& name,
                                       const std::string& command,
                                       DWORD timeout_ms,
                                       std::vector<uint8_t>* response,
                                       std::string* error, DWORD* error_code) {
  using namespace pipe_deadline_detail;
  response->clear();
  error->clear();
  *error_code = ERROR_SUCCESS;
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  Handle pipe;
  while (true) {
    const DWORD remaining = Remaining(deadline);
    if (remaining == 0) break;
    if (WaitNamedPipeW(name.c_str(), remaining)) {
      pipe.value =
          CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
      if (pipe.value != INVALID_HANDLE_VALUE) break;
    }
    *error_code = GetLastError();
    if (*error_code != ERROR_FILE_NOT_FOUND && *error_code != ERROR_PIPE_BUSY &&
        *error_code != ERROR_SEM_TIMEOUT)
      break;
    Sleep((std::min)(DWORD{10}, Remaining(deadline)));
  }
  if (pipe.value == INVALID_HANDLE_VALUE) {
    *error = "pipe_unavailable";
    if (*error_code == ERROR_SUCCESS) *error_code = ERROR_SEM_TIMEOUT;
    return false;
  }
  DWORD mode = PIPE_READMODE_MESSAGE;
  if (!SetNamedPipeHandleState(pipe.value, &mode, nullptr, nullptr)) {
    *error = "pipe_connect_failed";
    *error_code = GetLastError();
    return false;
  }
  Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  if (event.value == nullptr) {
    *error = "pipe_connect_failed";
    *error_code = GetLastError();
    return false;
  }
  DWORD transferred = 0;
  if (!Transfer(pipe.value, event.value, true,
                const_cast<char*>(command.data()),
                static_cast<DWORD>(command.size()), deadline, &transferred,
                error_code) ||
      transferred != command.size()) {
    *error = "pipe_write_failed";
    return false;
  }
  std::vector<uint8_t> chunk(64 * 1024);
  while (true) {
    const bool complete = Transfer(pipe.value, event.value, false, chunk.data(),
                                   static_cast<DWORD>(chunk.size()), deadline,
                                   &transferred, error_code);
    if (!complete && *error_code != ERROR_MORE_DATA) {
      response->clear();
      *error = "pipe_read_failed";
      return false;
    }
    response->insert(response->end(), chunk.begin(),
                     chunk.begin() + transferred);
    if (complete) return true;
  }
}

}  // namespace crossdesk

#endif