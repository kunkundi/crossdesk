#include "daemon.h"

#include <chrono>
#include <iostream>
#include <thread>
#include <utility>

#include "platform/daemon_backend.h"

namespace {

constexpr int kRestartDelayMs = 1000;

}  // namespace

Daemon::Daemon(const std::string& name) : name_(name), running_(false) {}

void Daemon::stop() {
  running_.store(false);
  crossdesk::platform::RequestDaemonStop();
}

bool Daemon::isRunning() const {
  return running_.load() && !crossdesk::platform::DaemonStopRequested();
}

bool Daemon::start(MainLoopFunc loop) {
  crossdesk::platform::ResetDaemonStop();
  if (!crossdesk::platform::PrepareDaemon()) return false;
  running_.store(true);
  return runWithRestart(std::move(loop));
}

bool Daemon::runWithRestart(MainLoopFunc loop) {
  int restart_count = 0;
  const std::string executable_path =
      crossdesk::platform::GetDaemonExecutablePath();
  if (executable_path.empty()) {
    std::cerr
        << "Failed to get executable path, falling back to direct execution"
        << std::endl;
    while (isRunning()) {
      try {
        loop();
        break;
      } catch (...) {
        ++restart_count;
        std::cerr << "Exception caught, restarting... (attempt "
                  << restart_count << ")" << std::endl;
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kRestartDelayMs));
      }
    }
    return true;
  }

  while (isRunning()) {
    const crossdesk::platform::DaemonChildResult result =
        crossdesk::platform::RunDaemonChild(
            executable_path, [this] { return isRunning(); });
    if (!isRunning() || result.normal_exit) break;

    ++restart_count;
    if (!result.started) {
      std::cerr << "Failed to create child process";
    } else if (result.terminated_by_signal) {
      std::cerr << "Child process crashed with signal " << result.exit_code;
    } else {
      std::cerr << "Child process exited with code " << result.exit_code;
    }
    std::cerr << ", restarting... (attempt " << restart_count << ")"
              << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(kRestartDelayMs));
  }
  return true;
}
