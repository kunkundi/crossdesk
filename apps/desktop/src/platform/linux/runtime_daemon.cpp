#include "platform/linux/runtime_daemon.h"

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

extern char** environ;

namespace crossdesk::platform {
namespace {

constexpr int kControlSocket = 3;
constexpr int kStartupTimeoutMs = 3000;

// A lost GUI socket can mean either a crash or desktop logout. Remember the
// owner's login session before it disappears, and never carry a restart into
// another login that happens to reuse the same DISPLAY (notably under XRDP).
class LoginSession {
 public:
  explicit LoginSession(pid_t owner) {
    library_ = dlopen("libsystemd.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (!library_) return;  // Non-logind desktops retain crash monitoring.
    const auto pid_session = reinterpret_cast<int (*)(pid_t, char**)>(
        dlsym(library_, "sd_pid_get_session"));
    const auto uid_display = reinterpret_cast<int (*)(uid_t, char**)>(
        dlsym(library_, "sd_uid_get_display"));
    state_ = reinterpret_cast<int (*)(const char*, char**)>(
        dlsym(library_, "sd_session_get_state"));
    if (!state_) return;
    char* id = nullptr;
    if (pid_session && pid_session(owner, &id) >= 0 && id) {
      session_ = id;
    }
    std::free(id);
    // GNOME can start applications in its user manager rather than directly
    // in the session scope. Use the supplied session or the user's graphical
    // session in that case; a process's actual scope always takes precedence.
    if (session_.empty()) {
      if (const char* inherited = std::getenv("XDG_SESSION_ID"))
        session_ = inherited;
    }
    if (session_.empty() && uid_display) {
      id = nullptr;
      if (uid_display(geteuid(), &id) >= 0 && id) session_ = id;
      std::free(id);
    }
  }

  ~LoginSession() {
    if (library_) dlclose(library_);
  }

  bool Alive() const {
    if (!state_ || session_.empty()) return true;
    char* state = nullptr;
    const int result = state_(session_.c_str(), &state);
    // "online" includes a locked/background session and must keep monitoring.
    // A removed or closing session must never be restarted.
    const bool alive = result >= 0 && state && std::strcmp(state, "closing") != 0;
    std::free(state);
    return alive;
  }

 private:
  void* library_ = nullptr;
  int (*state_)(const char*, char**) = nullptr;
  std::string session_;
};

bool Send(int socket, char command) {
  ssize_t count;
  do {
    count = send(socket, &command, 1, MSG_NOSIGNAL);
  } while (count < 0 && errno == EINTR);
  return count == 1;
}

bool Receive(int socket, char expected) {
  pollfd fd{socket, POLLIN, 0};
  int result;
  do {
    result = poll(&fd, 1, kStartupTimeoutMs);
  } while (result < 0 && errno == EINTR);
  if (result <= 0) return false;
  char reply = 0;
  return recv(socket, &reply, 1, 0) == 1 && reply == expected;
}

void Reap(int child) {
  if (child < 0) return;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto result = waitpid(child, nullptr, WNOHANG);
    if (result == child || (result < 0 && errno != EINTR)) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  // This is an unreaped direct child, so its PID cannot have been reused.
  kill(child, SIGKILL);
  while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
}

bool CloseInheritedDescriptors() {
  // Libraries may own descriptors without CLOEXEC. Keeping those alive in the
  // watcher could retain network ports or locks after the GUI crashes.
  DIR* directory = opendir("/proc/self/fd");
  if (!directory) return false;
  while (const auto* entry = readdir(directory)) {
    char* end = nullptr;
    const long fd = std::strtol(entry->d_name, &end, 10);
    if (*end == '\0' && fd > kControlSocket && fd != dirfd(directory))
      close(static_cast<int>(fd));
  }
  closedir(directory);
  return true;
}

}  // namespace

LinuxRuntimeDaemon& LinuxRuntimeDaemon::Instance() {
  static LinuxRuntimeDaemon daemon;
  return daemon;
}

LinuxRuntimeDaemon::~LinuxRuntimeDaemon() { Shutdown(); }

void LinuxRuntimeDaemon::Initialize(int argc, char* argv[], bool allowed) {
  std::lock_guard<std::mutex> lock(mutex_);
  Stop();
  allowed_ = allowed;
  arguments_.clear();
  char path[PATH_MAX]{};
  const ssize_t count = readlink("/proc/self/exe", path, sizeof(path));
  if (count <= 0 || count >= static_cast<ssize_t>(sizeof(path))) return;
  arguments_.emplace_back(path, static_cast<size_t>(count));
  arguments_.emplace_back(kLinuxDaemonArgument);
  bool no_headless = false;
  for (int i = 1; i < argc; ++i) {
    arguments_.emplace_back(argv[i]);
    no_headless |= arguments_.back() == "--no-headless";
  }
  // A desktop watchdog must never create a headless display after logout.
  if (!no_headless) arguments_.emplace_back("--no-headless");
}

bool LinuxRuntimeDaemon::SetEnabled(bool enabled, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error.clear();
  // Configuration-only callers and the headless supervisor do not own a GUI
  // watchdog. Their preference is still saved for the next desktop launch.
  if (!allowed_ || !enabled) {
    Stop();
    return true;
  }
  if (child_ >= 0) {
    pollfd fd{socket_, POLLIN, 0};
    if (poll(&fd, 1, 0) == 0) return true;
    Stop();
  }
  if (arguments_.empty()) {
    error = "Cannot resolve the executable for the daemon";
    return false;
  }
  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != 0) {
    error = std::strerror(errno);
    return false;
  }

  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attributes;
  int result = posix_spawn_file_actions_init(&actions);
  const bool actions_ready = result == 0;
  bool attributes_ready = false;
  if (result == 0) {
    result = posix_spawnattr_init(&attributes);
    attributes_ready = result == 0;
  }
  if (result == 0)
    result = posix_spawn_file_actions_adddup2(&actions, sockets[1], kControlSocket);
  if (result == 0 && sockets[0] != kControlSocket)
    result = posix_spawn_file_actions_addclose(&actions, sockets[0]);
  if (result == 0 && sockets[1] != kControlSocket)
    result = posix_spawn_file_actions_addclose(&actions, sockets[1]);
  sigset_t empty, defaults;
  sigemptyset(&empty);
  sigfillset(&defaults);
  if (result == 0) result = posix_spawnattr_setsigmask(&attributes, &empty);
  if (result == 0) result = posix_spawnattr_setsigdefault(&attributes, &defaults);
  if (result == 0)
    result = posix_spawnattr_setflags(
        &attributes, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK |
                         POSIX_SPAWN_SETSIGDEF);
  std::vector<char*> args;
  for (auto& argument : arguments_) args.push_back(argument.data());
  args.push_back(nullptr);
  pid_t child = -1;
  if (result == 0)
    result = posix_spawn(&child, "/proc/self/exe", &actions, &attributes,
                         args.data(), environ);
  if (attributes_ready) posix_spawnattr_destroy(&attributes);
  if (actions_ready) posix_spawn_file_actions_destroy(&actions);
  close(sockets[1]);
  if (result != 0) {
    close(sockets[0]);
    error = "Cannot start daemon: " + std::string(std::strerror(result));
    return false;
  }
  socket_ = sockets[0];
  child_ = child;
  // Do not report an enabled setting until the helper is ready and armed.
  if (!Receive(socket_, 'R') || !Send(socket_, 'A') || !Receive(socket_, 'A')) {
    error = "Daemon did not acknowledge startup";
    Stop();
    return false;
  }
  return true;
}

void LinuxRuntimeDaemon::Stop() {
  if (socket_ >= 0) {
    Send(socket_, 'S');
    close(socket_);
    socket_ = -1;
  }
  Reap(child_);
  child_ = -1;
}

void LinuxRuntimeDaemon::Shutdown(bool restart) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!restart) {
    Stop();
  } else {
    if (socket_ >= 0) close(socket_);
    socket_ = -1;
    child_ = -1;
  }
}

int RunLinuxRuntimeDaemon(int argc, char* argv[]) {
  int type = 0;
  socklen_t size = sizeof(type);
  ucred peer{};
  socklen_t peer_size = sizeof(peer);
  if (argc < 2 || argv[0][0] != '/' ||
      getsockopt(kControlSocket, SOL_SOCKET, SO_TYPE, &type, &size) != 0 ||
      type != SOCK_SEQPACKET ||
      getsockopt(kControlSocket, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) != 0 ||
      peer.uid != geteuid() || !CloseInheritedDescriptors()) return 1;
  LoginSession session(peer.pid);
  if (!session.Alive()) return 1;
  if (fcntl(kControlSocket, F_SETFD, FD_CLOEXEC) != 0 ||
      !Send(kControlSocket, 'R')) return 1;

  // Track the original process, not a potentially reused PID. A failed render
  // can close the socket before its destructors have released instance locks.
  int owner = -1;
#ifdef SYS_pidfd_open
  owner = static_cast<int>(syscall(SYS_pidfd_open, peer.pid, 0));
#endif

  bool armed = false;
  for (;;) {
    if (!session.Alive()) return 0;
    pollfd control{kControlSocket, POLLIN, 0};
    const int ready = poll(&control, 1, 1000);
    if (ready < 0 && errno == EINTR) continue;
    if (ready < 0) return 1;
    if (ready == 0) continue;
    char command = 0;
    const auto count = recv(kControlSocket, &command, 1, 0);
    if (count < 0 && errno == EINTR) continue;
    if (count == 0 || (count < 0 && errno == ECONNRESET)) break;
    if (count < 0) return 1;
    if (command == 'S') return 0;
    if (command != 'A' || armed) return 1;
    armed = true;
    // Even if the GUI disappears before receiving this acknowledgement, keep
    // reading: a queued stop disarms us; otherwise the lost owner is restarted.
    Send(kControlSocket, 'A');
  }
  close(kControlSocket);
  if (!armed) return 0;
  if (owner >= 0) {
    pollfd fd{owner, POLLIN, 0};
    int result;
    do {
      if (!session.Alive()) return 0;
      result = poll(&fd, 1, 1000);
    } while (result == 0 || (result < 0 && errno == EINTR));
    close(owner);
    if (result < 0) return 1;
  }
  // Let process teardown release its single-instance leases, and rate-limit
  // repeated startup failures. The new GUI acquires the normal leases itself.
  std::this_thread::sleep_for(std::chrono::seconds(1));
  if (!session.Alive()) return 0;
  std::vector<char*> arguments{argv[0]};
  for (int i = 2; i < argc; ++i) arguments.push_back(argv[i]);
  arguments.push_back(nullptr);
  execv(argv[0], arguments.data());
  return 1;
}

}  // namespace crossdesk::platform
