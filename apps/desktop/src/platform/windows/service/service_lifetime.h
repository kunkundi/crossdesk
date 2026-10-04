#pragma once

#include <Windows.h>

#include <string>
#include <thread>

namespace crossdesk {

// The service's main thread owns a Global mutex for its entire lifetime.
// Unlike a process handle or a job created in Session 0, this can be waited
// on by both SYSTEM and ordinary-user helpers in another Windows session.
class ServiceLifetimeWatcher {
 public:
  ServiceLifetimeWatcher() = default;
  ServiceLifetimeWatcher(const ServiceLifetimeWatcher&) = delete;
  ServiceLifetimeWatcher& operator=(const ServiceLifetimeWatcher&) = delete;
  ~ServiceLifetimeWatcher() {
    if (cancel_) SetEvent(cancel_);
    if (thread_.joinable()) thread_.join();
    if (cancel_) CloseHandle(cancel_);
    if (lifetime_) CloseHandle(lifetime_);
  }

  bool Start(const std::wstring& name) {
    if (lifetime_ || name.empty()) return false;
    lifetime_ = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
    if (!lifetime_) return false;
    cancel_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!cancel_) return false;
    thread_ = std::thread([this] {
      HANDLE handles[] = {lifetime_, cancel_};
      const DWORD result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
      if (result != WAIT_OBJECT_0 + 1) {
        // Includes abandoned ownership after a service crash. Do not wait for
        // transport/capture cleanup that could otherwise leave an orphan
        // online.
        TerminateProcess(GetCurrentProcess(), ERROR_BROKEN_PIPE);
      }
    });
    return true;
  }

 private:
  HANDLE lifetime_ = nullptr;
  HANDLE cancel_ = nullptr;
  std::thread thread_;
};
}  // namespace crossdesk
