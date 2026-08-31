#ifndef CROSSDESK_PLATFORM_DAEMON_BACKEND_H_
#define CROSSDESK_PLATFORM_DAEMON_BACKEND_H_

#include <csignal>
#include <functional>
#include <string>

namespace crossdesk::platform {

extern volatile std::sig_atomic_t g_daemon_stop_requested;

struct DaemonChildResult {
  bool started = false;
  bool normal_exit = false;
  bool terminated_by_signal = false;
  int exit_code = 0;
};

void ResetDaemonStop();
void RequestDaemonStop();
bool DaemonStopRequested();
bool PrepareDaemon();
std::string GetDaemonExecutablePath();
DaemonChildResult RunDaemonChild(const std::string& executable_path,
                                 const std::function<bool()>& keep_running);

}  // namespace crossdesk::platform

#endif  // CROSSDESK_PLATFORM_DAEMON_BACKEND_H_
