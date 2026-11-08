#ifndef CROSSDESK_PLATFORM_WINDOWS_RUNTIME_DAEMON_H_
#define CROSSDESK_PLATFORM_WINDOWS_RUNTIME_DAEMON_H_

#include <windows.h>

#include <mutex>
#include <string>
#include <vector>

namespace crossdesk::platform {

inline constexpr char kWindowsDaemonArgument[] = "--windows-daemon";

// Watches the GUI from a separate process, so settings can start/stop crash
// recovery without replacing the GUI or interrupting its sessions.
class WindowsRuntimeDaemon {
 public:
  static WindowsRuntimeDaemon& Instance();
  ~WindowsRuntimeDaemon();

  void Initialize();
  bool SetEnabled(bool enabled, std::string& error);
  void Shutdown(bool restart = false);

 private:
  WindowsRuntimeDaemon() = default;
  bool Stop(std::string& error);
  std::mutex mutex_;
  bool initialized_ = false;
  std::wstring executable_;
  std::vector<std::wstring> arguments_;
  HANDLE process_ = nullptr;
  HANDLE stop_ = nullptr;
};

// Dispatch before logging, configuration, service startup and instance locks.
// Reads the Unicode command line itself, including the original GUI arguments.
int RunWindowsRuntimeDaemon();

}  // namespace crossdesk::platform

#endif
