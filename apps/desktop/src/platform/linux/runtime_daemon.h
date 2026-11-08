#ifndef CROSSDESK_PLATFORM_LINUX_RUNTIME_DAEMON_H_
#define CROSSDESK_PLATFORM_LINUX_RUNTIME_DAEMON_H_

#include <mutex>
#include <string>
#include <vector>

namespace crossdesk::platform {

inline constexpr char kLinuxDaemonArgument[] = "--linux-daemon";

// A separate process watches this GUI over a private socket. It can be attached
// after the GUI has started, without forking its threads or restarting sessions.
class LinuxRuntimeDaemon {
 public:
  static LinuxRuntimeDaemon& Instance();
  ~LinuxRuntimeDaemon();

  void Initialize(int argc, char* argv[], bool allowed);
  bool SetEnabled(bool enabled, std::string& error);
  // Normal exit disarms the watcher. An unsuccessful application exit lets it
  // restart, just like an unexpected loss of the control socket.
  void Shutdown(bool restart = false);

 private:
  LinuxRuntimeDaemon() = default;
  void Stop();
  std::mutex mutex_;
  bool allowed_ = false;
  std::vector<std::string> arguments_;
  int socket_ = -1;
  int child_ = -1;
};

// Internal entry point, dispatched before logging, display or instance locks.
int RunLinuxRuntimeDaemon(int argc, char* argv[]);

}  // namespace crossdesk::platform

#endif
