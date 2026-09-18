#include "windows_input_injector.h"

#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

#include "interactive_desktop.h"
#include "rd_log.h"

namespace crossdesk {
namespace {

DWORD InjectOnUserDesktop(INPUT input) {
  if (!IsCurrentSessionUnlocked()) {
    return ERROR_REQUIRES_INTERACTIVE_WINDOWSTATION;
  }
  ScopedInteractiveDesktop desktop;
  if (!desktop.Bind(L"")) {
    const DWORD error = GetLastError();
    return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
  }
  if (!desktop.IsReceivingInput()) {
    return ERROR_REQUIRES_INTERACTIVE_WINDOWSTATION;
  }
  // Lock screens and UAC still require the session-checked service path.
  if (_wcsicmp(desktop.name().c_str(), L"Default") != 0) {
    return ERROR_ACCESS_DENIED;
  }
  SetLastError(ERROR_SUCCESS);
  if (SendInput(1, &input, sizeof(INPUT)) == 1) return ERROR_SUCCESS;
  const DWORD error = GetLastError();
  return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
}

class UserDesktopInputWorker {
 public:
  UserDesktopInputWorker() : thread_([this] { Run(); }) {}

  ~UserDesktopInputWorker() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    wake_.notify_one();
    thread_.join();
  }

  DWORD Send(INPUT input) {
    std::packaged_task<DWORD()> task(
        [input] { return InjectOnUserDesktop(input); });
    auto result = task.get_future();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_.push(std::move(task));
    }
    wake_.notify_one();
    return result.get();
  }

 private:
  void Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      wake_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
      if (pending_.empty()) return;
      auto task = std::move(pending_.front());
      pending_.pop();
      lock.unlock();
      task();
      lock.lock();
    }
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  std::queue<std::packaged_task<DWORD()>> pending_;
  bool stopping_ = false;
  std::thread thread_;
};

}  // namespace

UINT SendInputOnUserDesktop(const INPUT& input) {
  static UserDesktopInputWorker worker;
  const DWORD error = worker.Send(input);
  if (error != ERROR_SUCCESS) {
    // Include session identity even when Windows reports no useful SendInput
    // error (for example UIPI). Keep repeated mouse-move failures bounded.
    static std::mutex log_mutex;
    static ULONGLONG last_log_tick = 0;
    std::lock_guard<std::mutex> lock(log_mutex);
    const ULONGLONG now = GetTickCount64();
    if (last_log_tick == 0 || now - last_log_tick >= 2000) {
      DWORD session_id = 0xFFFFFFFF;
      ProcessIdToSessionId(GetCurrentProcessId(), &session_id);
      LOG_WARN("User desktop input unavailable: session_id={}, error={}",
               session_id, error);
      last_log_tick = now;
    }
  }
  SetLastError(error);
  return error == ERROR_SUCCESS ? 1 : 0;
}

}  // namespace crossdesk
