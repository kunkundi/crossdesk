#include "platform/daemon_backend.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

namespace crossdesk::platform {

bool PrepareDaemon() {
  const bool from_terminal =
      isatty(STDIN_FILENO) != 0 || isatty(STDOUT_FILENO) != 0;
  pid_t process = fork();
  if (process < 0) return false;
  if (process > 0) _exit(0);
  if (setsid() < 0) return false;

  process = fork();
  if (process < 0) return false;
  if (process > 0) _exit(0);

  umask(0);
  if (chdir("/") != 0) {
    std::cerr << "Failed to change daemon working directory to /: "
              << std::strerror(errno) << std::endl;
    return false;
  }

  const int null_fd = open("/dev/null", O_RDWR);
  if (null_fd >= 0) {
    dup2(null_fd, STDIN_FILENO);
    if (!from_terminal) {
      dup2(null_fd, STDOUT_FILENO);
      dup2(null_fd, STDERR_FILENO);
    }
    if (null_fd > STDERR_FILENO) close(null_fd);
  }

  signal(SIGTERM, [](int) { g_daemon_stop_requested = 1; });
  signal(SIGINT, [](int) { g_daemon_stop_requested = 1; });
  signal(SIGPIPE, SIG_IGN);
  return true;
}

std::string GetDaemonExecutablePath() {
  char path[PATH_MAX] = {};
  const ssize_t count = readlink("/proc/self/exe", path, sizeof(path) - 1);
  return count > 0 ? std::string(path, static_cast<size_t>(count))
                   : std::string();
}

}  // namespace crossdesk::platform
