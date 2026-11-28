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
#include <utility>

namespace crossdesk::updater {
namespace {
std::string LockPrefix(const fs::path& application) {
  return "/var/run/crossdesk-update-" + Sha256(fs::weakly_canonical(application).string());
}

struct ScopedFd {
  explicit ScopedFd(int fd) : value(fd) {}
  ~ScopedFd() { if (value >= 0) close(value); }
  ScopedFd(const ScopedFd&) = delete;
  ScopedFd& operator=(const ScopedFd&) = delete;
  int release() { return std::exchange(value, -1); }
  int value;
};

std::vector<char*> Argv(std::vector<std::string>& args) {
  std::vector<char*> values;
  for (auto& argument : args) values.push_back(argument.data());
  values.push_back(nullptr);
  return values;
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
  if (!IsPrivileged()) throw std::runtime_error("Update maintenance requires authorization");
  const auto prefix = LockPrefix(application);
  ScopedFd guard(open((prefix + ".lock").c_str(),
                      O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600));
  struct stat info{};
  if (guard.value < 0 || fstat(guard.value, &info) || !S_ISREG(info.st_mode) ||
      info.st_uid != 0 || (info.st_mode & 0077))
    throw std::runtime_error("Cannot open private update lock");
  if (flock(guard.value, LOCK_EX | LOCK_NB))
    throw std::runtime_error("Cannot acquire update lock; another update may be running");

  // Only root may open the read end. Other users observe its lifetime by opening
  // the write end without sending data. Their descriptors cannot keep maintenance active.
  const auto status_path = prefix + ".active";
  if (mkfifo(status_path.c_str(), 0622) && errno != EEXIST)
    throw std::runtime_error("Cannot create update status FIFO");
  ScopedFd reader(open(status_path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
  if (reader.value < 0 || fstat(reader.value, &info) || !S_ISFIFO(info.st_mode) ||
      info.st_uid != 0 || fchmod(reader.value, 0622))
    throw std::runtime_error("Cannot publish update maintenance status");
  handle_ = guard.release();
  status_reader_ = reader.release();
}

MaintenanceLock::~MaintenanceLock() {
  if (status_reader_ >= 0) close(status_reader_);
  if (handle_ != -1) close(static_cast<int>(handle_));
}

bool MaintenanceActive(const fs::path& application) {
  const auto path = LockPrefix(application) + ".active";
  ScopedFd writer(open(path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
  if (writer.value < 0) {
    if (errno == ENOENT || errno == ENXIO) return false;
    throw std::runtime_error("Cannot inspect update maintenance status");
  }
  struct stat info{};
  if (fstat(writer.value, &info) || !S_ISFIFO(info.st_mode) || info.st_uid != 0)
    throw std::runtime_error("Invalid update maintenance status");
  return true;
}
}  // namespace crossdesk::updater
