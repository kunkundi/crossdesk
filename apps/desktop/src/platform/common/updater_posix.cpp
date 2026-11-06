#include "updater/update_platform.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <thread>

namespace crossdesk::updater {
namespace {
std::string LockPath(const fs::path& application) {
  return "/tmp/CrossDesk-update-" + Sha256(fs::weakly_canonical(application).string()) + ".lock";
}
std::vector<char*> Argv(std::vector<std::string>& args) {
  std::vector<char*> values;
  for (auto& argument : args) values.push_back(argument.data());
  values.push_back(nullptr);
  return values;
}
int OpenLock(const std::string& path, bool create) {
  constexpr int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW;
  int fd = open(path.c_str(), flags | (create ? O_CREAT | O_EXCL : 0), 0644);
  if (fd < 0 && create && errno == EEXIST) fd = open(path.c_str(), flags);
  if (fd >= 0) {
    struct stat info{};
    if (fstat(fd, &info) || !S_ISREG(info.st_mode)) {
      close(fd); errno = EINVAL; return -1;
    }
    // A restrictive umask must not leave a stale lock file that permanently
    // prevents other users from probing whether an update is active.
    if (create && info.st_uid == geteuid() && fchmod(fd, 0644)) {
      const int error = errno; close(fd); errno = error; return -1;
    }
  }
  return fd;
}
}  // namespace

bool IsPrivileged() { return geteuid() == 0; }
bool OwnedJobDirectory(const fs::path& job) {
  struct stat info{};
  return lstat(job.c_str(), &info) == 0 && S_ISDIR(info.st_mode) &&
         info.st_uid == geteuid() && (info.st_mode & 0077) == 0;
}
fs::path PrivateDirectory() {
  // Ignore TMPDIR in privileged processes. mkdtemp creates a private 0700
  // directory; installation only consumes a verified copy inside this directory.
  std::string pattern = "/tmp/crossdesk-update-XXXXXX";
  if (!mkdtemp(pattern.data())) throw std::runtime_error("Cannot create private update directory");
  return fs::path(pattern);
}
fs::path HelperPath(const fs::path& application) {
  return application.parent_path() / "crossdesk_updater";
}
std::shared_ptr<void> PinHelper(const fs::path&) { return {}; }
void RestoreServices() {}

int Run(const fs::path& executable, const std::vector<std::string>& arguments, bool elevate) {
  // Platform-specific elevation builds a fixed argv and calls this with false.
  if (elevate) throw std::runtime_error("Use platform elevation entry point");
  std::vector<std::string> args{executable.string()};
  args.insert(args.end(), arguments.begin(), arguments.end());
  auto argv = Argv(args);
  const pid_t pid = fork();
  if (pid < 0) throw std::runtime_error("Cannot start installer");
  if (pid == 0) {
    execv(executable.c_str(), argv.data());
    _exit(127);
  }
  int status = 0;
  pid_t result;
  do { result = waitpid(pid, &status, 0); } while (result < 0 && errno == EINTR);
  return result == pid && WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

void Launch(const fs::path& executable, const std::vector<std::string>& arguments) {
  std::vector<std::string> args{executable.string()};
  args.insert(args.end(), arguments.begin(), arguments.end());
  auto argv = Argv(args);
  int errors[2];
  if (pipe(errors)) throw std::runtime_error("Cannot create launch pipe");
  fcntl(errors[0], F_SETFD, FD_CLOEXEC);
  fcntl(errors[1], F_SETFD, FD_CLOEXEC);
  const pid_t child = fork();
  if (child == 0) {
    close(errors[0]);
    if (setsid() < 0) _exit(127);
    const pid_t detached = fork();
    if (detached < 0) _exit(127);
    if (detached > 0) _exit(0);
    const int null = open("/dev/null", O_RDWR);
    if (null >= 0) {
      dup2(null, STDIN_FILENO); dup2(null, STDOUT_FILENO); dup2(null, STDERR_FILENO);
      if (null > STDERR_FILENO) close(null);
    }
    execv(executable.c_str(), argv.data());
    const int error = errno;
    // This small blocking pipe write is atomic. Retry signal interruptions so
    // the parent cannot mistake a failed exec for a successful launch.
    ssize_t written;
    do { written = write(errors[1], &error, sizeof(error)); }
    while (written < 0 && errno == EINTR);
    _exit(127);
  }
  close(errors[1]);
  if (child < 0) { close(errors[0]); throw std::runtime_error("Cannot launch update helper"); }
  int status = 0;
  while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
  int error = 0;
  ssize_t bytes;
  do { bytes = read(errors[0], &error, sizeof(error)); } while (bytes < 0 && errno == EINTR);
  close(errors[0]);
  if (bytes != 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    throw std::runtime_error("Cannot execute update helper");
}

MaintenanceLock::MaintenanceLock(const fs::path& application) {
  const int fd = OpenLock(LockPath(application), true);
  if (fd < 0) throw std::runtime_error("Cannot open update lock");
  if (flock(fd, LOCK_EX | LOCK_NB)) { close(fd); throw std::runtime_error("An update is already running"); }
  handle_ = fd;
}
MaintenanceLock::~MaintenanceLock() { if (handle_ != -1) close(static_cast<int>(handle_)); }
bool MaintenanceActive(const fs::path& application) {
  const int fd = OpenLock(LockPath(application), false);
  if (fd < 0) return errno != ENOENT;
  const bool locked = flock(fd, LOCK_SH | LOCK_NB) != 0;
  close(fd);
  return locked;
}
}  // namespace crossdesk::updater
