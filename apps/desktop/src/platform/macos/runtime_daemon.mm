#include "platform/macos/runtime_daemon.h"

#import <AppKit/AppKit.h>
#import <CoreFoundation/CoreFoundation.h>

#include <fcntl.h>
#include <mach-o/dyld.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

extern char** environ;

namespace crossdesk::platform {
namespace {

constexpr int kControlSocket = 3;
constexpr int kStartupTimeoutMs = 3000;
constexpr int kPollIntervalMs = 100;

struct Descriptor {
  int value = -1;
  ~Descriptor() { if (value >= 0) close(value); }
};

std::string ExecutablePath() {
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> path(size);
  if (!size || _NSGetExecutablePath(path.data(), &size) != 0) return {};
  char* resolved = realpath(path.data(), nullptr);
  if (!resolved) return {};
  const std::string result(resolved);
  std::free(resolved);
  return result;
}

bool ConfigureSocket(int socket) {
  const int enabled = 1;
  return fcntl(socket, F_SETFD, FD_CLOEXEC) == 0 &&
         setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                    sizeof(enabled)) == 0;
}

bool Send(int socket, char command) {
  ssize_t count;
  do {
    count = send(socket, &command, 1, 0);
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
  // Still an unreaped direct child: its PID cannot have been reused.
  kill(child, SIGKILL);
  while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
}

class LoginSession {
 public:
  LoginSession() {
    // NSWorkspace does not require creating NSApplication or another Dock icon.
    // Only logout/poweroff ends monitoring; sleep, lock and user switching do
    // not end the login session.
    center_ = NSWorkspace.sharedWorkspace.notificationCenter;
    const auto ending = ending_;
    observer_ = [center_ addObserverForName:NSWorkspaceWillPowerOffNotification
                                  object:nil queue:nil
                              usingBlock:^(NSNotification*) {
      ending->store(true);
    }];
  }
  ~LoginSession() { if (observer_) [center_ removeObserver:observer_]; }

  bool Alive() {
    // The watcher has no GUI event loop. Deliver workspace notifications even
    // while waiting for process teardown or delaying a crash restart.
    @autoreleasepool {
      while (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true) ==
             kCFRunLoopRunHandledSource) {}
      return observer_ != nil && !ending_->load();
    }
  }

 private:
  std::shared_ptr<std::atomic<bool>> ending_ =
      std::make_shared<std::atomic<bool>>(false);
  NSNotificationCenter* center_ = nil;
  id observer_ = nil;
};

int Watch(int argc, char* argv[]) {
  Descriptor control{kControlSocket};
  int type = 0;
  socklen_t size = sizeof(type);
  uid_t user = 0;
  gid_t group = 0;
  const pid_t owner = getppid();
  if (owner <= 1 ||
      getsockopt(control.value, SOL_SOCKET, SO_TYPE, &type, &size) != 0 ||
      type != SOCK_STREAM || getpeereid(control.value, &user, &group) != 0 ||
      user != geteuid() || !ConfigureSocket(control.value)) return 1;

  Descriptor events{kqueue()};
  struct kevent change{};
  EV_SET(&change, owner, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
  if (events.value < 0 || fcntl(events.value, F_SETFD, FD_CLOEXEC) != 0 ||
      kevent(events.value, &change, 1, nullptr, 0, nullptr) != 0) return 1;

  LoginSession session;
  if (!session.Alive() || !Send(control.value, 'R')) return 1;
  bool armed = false;
  for (;;) {
    if (!session.Alive()) return 0;
    pollfd fd{control.value, POLLIN, 0};
    const int ready = poll(&fd, 1, kPollIntervalMs);
    if (ready < 0 && errno == EINTR) continue;
    if (ready < 0) return 1;
    if (ready == 0) continue;
    char command = 0;
    const auto count = recv(control.value, &command, 1, 0);
    if (count < 0 && errno == EINTR) continue;
    if (count == 0 || (count < 0 && errno == ECONNRESET)) break;
    if (count < 0) return 1;
    if (command == 'S') return 0;
    if (command != 'A' || armed) return 1;
    armed = true;
    // Drain a queued stop even if the GUI disappears before receiving this.
    Send(control.value, 'A');
  }
  if (!armed) return 0;

  // Closing the control socket can precede the GUI's destructors. kqueue
  // watches the original process instance, so PID reuse cannot fool this wait.
  for (;;) {
    if (!session.Alive()) return 0;
    struct kevent event{};
    const timespec timeout{0, kPollIntervalMs * 1000000};
    const int count = kevent(events.value, nullptr, 0, &event, 1, &timeout);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 || (count > 0 && (event.flags & EV_ERROR))) return 1;
    if (count > 0 && (event.fflags & NOTE_EXIT)) break;
  }
  const auto restart_at = std::chrono::steady_clock::now() +
                          std::chrono::seconds(1);
  do {
    if (!session.Alive()) return 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
  } while (std::chrono::steady_clock::now() < restart_at);
  if (!session.Alive()) return 0;

  std::vector<char*> arguments{argv[0]};
  for (int i = 2; i < argc; ++i) arguments.push_back(argv[i]);
  arguments.push_back(nullptr);
  execv(argv[0], arguments.data());
  return 1;
}

}  // namespace

MacRuntimeDaemon& MacRuntimeDaemon::Instance() {
  static MacRuntimeDaemon daemon;
  return daemon;
}

MacRuntimeDaemon::~MacRuntimeDaemon() { Shutdown(); }

void MacRuntimeDaemon::Initialize(int argc, char* argv[]) {
  std::lock_guard<std::mutex> lock(mutex_);
  Stop();
  initialized_ = true;
  arguments_.clear();
  const auto executable = ExecutablePath();
  if (executable.empty()) return;
  arguments_.push_back(executable);
  arguments_.emplace_back(kMacDaemonArgument);
  for (int i = 1; i < argc; ++i) arguments_.emplace_back(argv[i]);
}

bool MacRuntimeDaemon::SetEnabled(bool enabled, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error.clear();
  // Configuration-only callers do not own a GUI watcher.
  if (!initialized_) return true;
  if (!enabled) {
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
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
    error = std::strerror(errno);
    return false;
  }
  // Keep IPC away from stdio and the fixed child descriptor, including when
  // the application was launched with one or more standard streams closed.
  Descriptor parent{fcntl(sockets[0], F_DUPFD_CLOEXEC, kControlSocket + 1)};
  Descriptor child_socket{fcntl(sockets[1], F_DUPFD_CLOEXEC, kControlSocket + 1)};
  const int duplicate_error = errno;
  close(sockets[0]);
  close(sockets[1]);
  if (parent.value < 0 || child_socket.value < 0) {
    error = std::strerror(duplicate_error);
    return false;
  }
  if (!ConfigureSocket(parent.value) || !ConfigureSocket(child_socket.value)) {
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
    result = posix_spawn_file_actions_adddup2(&actions, child_socket.value,
                                             kControlSocket);
  // Keep only the control socket and standard streams. In particular, never
  // retain GUI sockets, files or single-instance locks after a crash.
  for (int fd = 0; result == 0 && fd <= STDERR_FILENO; ++fd) {
    result = fcntl(fd, F_GETFD) >= 0
        ? posix_spawn_file_actions_addinherit_np(&actions, fd)
        : posix_spawn_file_actions_addopen(&actions, fd, "/dev/null", O_RDWR, 0);
  }
  sigset_t empty, defaults;
  sigemptyset(&empty);
  sigfillset(&defaults);
  if (result == 0) result = posix_spawnattr_setsigmask(&attributes, &empty);
  if (result == 0) result = posix_spawnattr_setsigdefault(&attributes, &defaults);
  if (result == 0) result = posix_spawnattr_setpgroup(&attributes, 0);
  if (result == 0)
    result = posix_spawnattr_setflags(&attributes,
        POSIX_SPAWN_CLOEXEC_DEFAULT | POSIX_SPAWN_SETPGROUP |
        POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
  std::vector<char*> args;
  for (auto& argument : arguments_) args.push_back(argument.data());
  args.push_back(nullptr);
  pid_t child = -1;
  if (result == 0)
    result = posix_spawn(&child, arguments_[0].c_str(), &actions, &attributes,
                         args.data(), environ);
  if (attributes_ready) posix_spawnattr_destroy(&attributes);
  if (actions_ready) posix_spawn_file_actions_destroy(&actions);
  if (result != 0) {
    error = "Cannot start daemon: " + std::string(std::strerror(result));
    return false;
  }
  close(child_socket.value);
  child_socket.value = -1;
  socket_ = parent.value;
  parent.value = -1;
  child_ = child;
  if (!Receive(socket_, 'R') || !Send(socket_, 'A') || !Receive(socket_, 'A')) {
    error = "Daemon did not acknowledge startup";
    Stop();
    return false;
  }
  return true;
}

void MacRuntimeDaemon::Stop() {
  if (socket_ >= 0) {
    Send(socket_, 'S');
    close(socket_);
    socket_ = -1;
  }
  Reap(child_);
  child_ = -1;
}

void MacRuntimeDaemon::Shutdown(bool restart) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!restart) {
    Stop();
  } else {
    if (socket_ >= 0) close(socket_);
    socket_ = -1;
    child_ = -1;
  }
}

int RunMacRuntimeDaemon(int argc, char* argv[]) {
  if (argc < 2 || std::strcmp(argv[1], kMacDaemonArgument) != 0 ||
      argv[0][0] != '/') return 1;
  @autoreleasepool {
    return Watch(argc, argv);
  }
}

}  // namespace crossdesk::platform
