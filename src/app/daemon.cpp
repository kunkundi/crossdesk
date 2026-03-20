#include "daemon.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#include <tchar.h>
#include <windows.h>
#elif __APPLE__
#include <fcntl.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#endif

#ifndef _WIN32
volatile std::sig_atomic_t Daemon::stop_requested_ = 0;
#endif

namespace {
constexpr int kRestartDelayMs = 1000;
#ifndef _WIN32
constexpr int kWaitPollIntervalMs = 200;
#endif
}  // namespace

// get executable file path
static std::string GetExecutablePath() {
#ifdef _WIN32
  char path[32768];
  DWORD length = GetModuleFileNameA(nullptr, path, sizeof(path));
  if (length > 0 && length < sizeof(path)) {
    return std::string(path);
  }
#elif __APPLE__
  char path[PATH_MAX];
  uint32_t size = sizeof(path);
  if (_NSGetExecutablePath(path, &size) == 0) {
    char resolved_path[PATH_MAX];
    if (realpath(path, resolved_path) != nullptr) {
      return std::string(resolved_path);
    }
    return std::string(path);
  }
#else
  char path[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (count != -1) {
    path[count] = '\0';
    return std::string(path);
  }
#endif
  return "";
}

Daemon::Daemon(const std::string& name) : name_(name), running_(false) {}

void Daemon::stop() {
  running_.store(false);
#ifndef _WIN32
  stop_requested_ = 1;
#endif
}

bool Daemon::isRunning() const {
#ifndef _WIN32
  return running_.load() && (stop_requested_ == 0);
#else
  return running_.load();
#endif
}

bool Daemon::start(MainLoopFunc loop) {
#ifdef _WIN32
  running_.store(true);
  return runWithRestart(loop);
#elif __APPLE__
  // macOS: Use child process monitoring (like Windows) to preserve GUI
  stop_requested_ = 0;
  running_.store(true);
  return runWithRestart(loop);
#else
  // linux: Daemonize first, then run with restart monitoring
  stop_requested_ = 0;

  // check if running from terminal before fork
  bool from_terminal =
      (isatty(STDIN_FILENO) != 0) || (isatty(STDOUT_FILENO) != 0);

  // first fork: detach from terminal
  pid_t pid = fork();
  if (pid < 0) {
    std::cerr << "Failed to fork daemon process" << std::endl;
    return false;
  }
  if (pid > 0) _exit(0);

  if (setsid() < 0) {
    std::cerr << "Failed to create new session" << std::endl;
    return false;
  }

  pid = fork();
  if (pid < 0) {
    std::cerr << "Failed to fork daemon process (second fork)" << std::endl;
    return false;
  }
  if (pid > 0) _exit(0);

  umask(0);
  chdir("/");

  // redirect file descriptors: keep stdout/stderr if from terminal, else
  // redirect to /dev/null
  int fd = open("/dev/null", O_RDWR);
  if (fd >= 0) {
    dup2(fd, STDIN_FILENO);
    if (!from_terminal) {
      dup2(fd, STDOUT_FILENO);
      dup2(fd, STDERR_FILENO);
    }
    if (fd > 2) close(fd);
  }

  // set up signal handlers
  signal(SIGTERM, [](int) { stop_requested_ = 1; });
  signal(SIGINT, [](int) { stop_requested_ = 1; });

  // ignore SIGPIPE
  signal(SIGPIPE, SIG_IGN);

  running_.store(true);
  return runWithRestart(loop);
#endif
}

#ifdef _WIN32
static int RunLoopCatchCpp(Daemon::MainLoopFunc& loop) {
  try {
    loop();
    return 0;  // normal exit
  } catch (const std::exception& e) {
    std::cerr << "Exception caught: " << e.what() << std::endl;
    return 1;  // c++ exception
  } catch (...) {
    std::cerr << "Unknown exception caught" << std::endl;
    return 1;  // other exception
  }
}

static int RunLoopWithSEH(Daemon::MainLoopFunc& loop) {
  __try {
    return RunLoopCatchCpp(loop);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // catch system-level crashes (access violation, divide by zero, etc.)
    DWORD code = GetExceptionCode();
    std::cerr << "System crash detected (SEH exception code: 0x" << std::hex
              << code << std::dec << ")" << std::endl;
    return 2;  // System crash
  }
}
#endif

// run with restart logic: parent monitors child process and restarts on crash
bool Daemon::runWithRestart(MainLoopFunc loop) {
  int restart_count = 0;
  std::string exe_path = GetExecutablePath();
  if (exe_path.empty()) {
    std::cerr
        << "Failed to get executable path, falling back to direct execution"
        << std::endl;
    while (isRunning()) {
      try {
        loop();
        break;
      } catch (...) {
        restart_count++;
        std::cerr << "Exception caught, restarting... (attempt "
                  << restart_count << ")" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(kRestartDelayMs));
      }
    }
    return true;
  }

  while (isRunning()) {
#ifdef _WIN32
    // windows: use CreateProcess to create child process
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};

    std::string cmd_line = "\"" + exe_path + "\" --child";
    std::vector<char> cmd_line_buf(cmd_line.begin(), cmd_line.end());
    cmd_line_buf.push_back('\0');

    BOOL success = CreateProcessA(
        nullptr,  // executable file path (specified in command line)
        cmd_line_buf.data(),  // command line arguments
        nullptr,              // process security attributes
        nullptr,              // thread security attributes
        FALSE,                // don't inherit handles
        0,                    // creation flags
        nullptr,              // environment variables (inherit from parent)
        nullptr,              // current directory
        &si,                  // startup info
        &pi                   // process information
    );

    if (!success) {
      std::cerr << "Failed to create child process, error: " << GetLastError()
                << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(kRestartDelayMs));
      restart_count++;
      continue;
    }

    while (isRunning()) {
      DWORD wait_result = WaitForSingleObject(pi.hProcess, 200);
      if (wait_result == WAIT_OBJECT_0) {
        break;
      }
      if (wait_result == WAIT_FAILED) {
        std::cerr << "Failed waiting child process, error: " << GetLastError()
                  << std::endl;
        break;
      }
    }

    if (!isRunning()) {
      TerminateProcess(pi.hProcess, 1);
      WaitForSingleObject(pi.hProcess, 3000);
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (!isRunning() || exit_code == 0) {
      break;  // normal exit
    }
    restart_count++;
    std::cerr << "Child process exited with code " << exit_code
              << ", restarting... (attempt " << restart_count << ")"
              << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(kRestartDelayMs));
#else
    // linux: use fork + exec to create child process
    pid_t pid = fork();
    if (pid == 0) {
      execl(exe_path.c_str(), exe_path.c_str(), "--child", nullptr);
      _exit(1);  // exec failed
    } else if (pid > 0) {
      int status = 0;
      pid_t waited_pid = -1;
      while (isRunning()) {
        waited_pid = waitpid(pid, &status, WNOHANG);
        if (waited_pid == pid) {
          break;
        }
        if (waited_pid < 0 && errno != EINTR) {
          break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kWaitPollIntervalMs));
      }

      if (!isRunning() && waited_pid != pid) {
        kill(pid, SIGTERM);
        waited_pid = waitpid(pid, &status, 0);
      }

      if (waited_pid < 0) {
        if (!isRunning()) {
          break;
        }
        restart_count++;
        std::cerr << "waitpid failed, errno: " << errno
                  << ", restarting... (attempt " << restart_count << ")"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(kRestartDelayMs));
        continue;
      }

      if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (!isRunning() || exit_code == 0) {
          break;  // normal exit
        }
        restart_count++;
        std::cerr << "Child process exited with code " << exit_code
                  << ", restarting... (attempt " << restart_count << ")"
                  << std::endl;
      } else if (WIFSIGNALED(status)) {
        if (!isRunning()) {
          break;
        }
        restart_count++;
        std::cerr << "Child process crashed with signal " << WTERMSIG(status)
                  << ", restarting... (attempt " << restart_count << ")"
                  << std::endl;
      } else {
        restart_count++;
        std::cerr << "Child process exited with unknown status, restarting... "
                     "(attempt "
                  << restart_count << ")" << std::endl;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(kRestartDelayMs));
    } else {
      std::cerr << "Failed to fork child process" << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(kRestartDelayMs));
      restart_count++;
    }
#endif
  }

  return true;
}
