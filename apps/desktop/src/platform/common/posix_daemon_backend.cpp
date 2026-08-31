#include "platform/daemon_backend.h"

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <thread>

namespace crossdesk::platform {

volatile std::sig_atomic_t g_daemon_stop_requested = 0;

void ResetDaemonStop() { g_daemon_stop_requested = 0; }

void RequestDaemonStop() { g_daemon_stop_requested = 1; }

bool DaemonStopRequested() { return g_daemon_stop_requested != 0; }

DaemonChildResult RunDaemonChild(const std::string& executable_path,
                                 const std::function<bool()>& keep_running) {
  DaemonChildResult result;
  const pid_t child = fork();
  if (child == 0) {
    execl(executable_path.c_str(), executable_path.c_str(), "--child",
          nullptr);
    _exit(1);
  }
  if (child < 0) return result;

  result.started = true;
  int status = 0;
  pid_t waited = -1;
  while (keep_running()) {
    waited = waitpid(child, &status, WNOHANG);
    if (waited == child || (waited < 0 && errno != EINTR)) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  if (!keep_running() && waited != child) {
    kill(child, SIGTERM);
    waited = waitpid(child, &status, 0);
  }
  if (waited < 0) return result;

  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
    result.normal_exit = result.exit_code == 0;
  } else if (WIFSIGNALED(status)) {
    result.terminated_by_signal = true;
    result.exit_code = WTERMSIG(status);
  }
  return result;
}

}  // namespace crossdesk::platform
