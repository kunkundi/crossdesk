/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SESSION_END_OBSERVER_H_
#define _SESSION_END_OBSERVER_H_

#include <Windows.h>

namespace crossdesk {

// Owned and pumped by the UI thread. A query is permission to end the session,
// not a request to tear down transport/capture: another application may still
// cancel shutdown. This observer never registers a shutdown-blocking reason.
class SessionEndObserver {
 public:
  SessionEndObserver() = default;
  ~SessionEndObserver();
  SessionEndObserver(const SessionEndObserver&) = delete;
  SessionEndObserver& operator=(const SessionEndObserver&) = delete;

  bool Start();
  bool Ending() const { return ending_; }

 private:
  bool ReportError(const char* operation);
  static LRESULT CALLBACK WindowProc(HWND window, UINT message,
                                    WPARAM wparam, LPARAM lparam);

  HWND window_ = nullptr;
  bool ending_ = false;
  ULONGLONG last_error_tick_ = 0;
};

}  // namespace crossdesk

#endif