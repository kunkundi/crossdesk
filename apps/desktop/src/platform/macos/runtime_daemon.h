#ifndef CROSSDESK_PLATFORM_MACOS_RUNTIME_DAEMON_H_
#define CROSSDESK_PLATFORM_MACOS_RUNTIME_DAEMON_H_

#include <mutex>
#include <string>
#include <vector>

namespace crossdesk::platform {

inline constexpr char kMacDaemonArgument[] = "--macos-daemon";

// Attach a separate crash watcher to the running GUI without restarting it or
// forking its threads. The watcher shares the GUI's login session.
class MacRuntimeDaemon {
 public:
  static MacRuntimeDaemon& Instance();
  ~MacRuntimeDaemon();

  void Initialize(int argc, char* argv[]);
  bool SetEnabled(bool enabled, std::string& error);
  // Normal exit disarms the watcher; a failed startup lets it restart after
  // this process has exited and released its single-instance leases.
  void Shutdown(bool restart = false);

 private:
  MacRuntimeDaemon() = default;
  void Stop();
  std::mutex mutex_;
  bool initialized_ = false;
  std::vector<std::string> arguments_;
  int socket_ = -1;
  int child_ = -1;
};

// Dispatch on the main thread before logging, GUI setup or instance locks.
int RunMacRuntimeDaemon(int argc, char* argv[]);

}  // namespace crossdesk::platform

#endif
