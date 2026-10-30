#ifndef _WIN32
#include "terminal_io.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace crossdesk::cli {
std::atomic<int> interrupted{0};
namespace {
void Signal(int value) { interrupted.store(value, std::memory_order_relaxed); }
} // namespace
void InitializeIo() {
  struct sigaction action{};
  action.sa_handler = Signal;
  sigemptyset(&action.sa_mask);
  for (int sig : {SIGINT, SIGTERM, SIGHUP})
    sigaction(sig, &action, nullptr);
  std::signal(SIGPIPE, SIG_IGN);
}
const char *Platform() {
#ifdef __APPLE__
  return "macos";
#else
  return "linux";
#endif
}
bool IsTty(int fd) { return isatty(fd); }
std::ptrdiff_t Read(int fd, void *buffer, size_t size) {
  return read(fd, buffer, size);
}
std::ptrdiff_t Write(int fd, const void *buffer, size_t size) {
  return write(fd, buffer, size);
}
void Close(int fd) { close(fd); }
int Duplicate(int fd) { return dup(fd); }
int Redirect(int from, int to) { return dup2(from, to); }
int OpenLog(const std::string &path) {
  return open(path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
}
bool WaitReadable(int fd, int timeout_ms) {
  pollfd p{fd, POLLIN, 0};
  int result = poll(&p, 1, timeout_ms);
  if (result < 0 && errno != EINTR)
    throw std::runtime_error("cannot poll input descriptor");
  if (p.revents & POLLNVAL) {
    // Darwin's /dev/tty may reject poll even though the descriptor is valid.
    // select supports that controlling-terminal device without a blocking read.
    if (fd < 0 || fd >= FD_SETSIZE || fcntl(fd, F_GETFL) < 0)
      throw std::runtime_error("input descriptor unavailable");
    fd_set readers;
    FD_ZERO(&readers);
    FD_SET(fd, &readers);
    timeval timeout{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    result = select(fd + 1, &readers, nullptr, nullptr, &timeout);
    if (result < 0 && errno != EINTR)
      throw std::runtime_error("cannot wait for terminal input");
    return result > 0;
  }
  return result > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR));
}
void WaitIo(int input_fd, int output_fd, int timeout_ms) {
  pollfd fds[2] = {{input_fd, POLLIN, 0}, {output_fd, POLLOUT, 0}};
  poll(fds, 2, timeout_ms);
}
TerminalSize GetTerminalSize(int fd) {
  winsize ws{};
  TerminalSize size;
  if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row) {
    size.columns = ws.ws_col;
    size.rows = ws.ws_row;
    size.pixel_width = ws.ws_xpixel;
    size.pixel_height = ws.ws_ypixel;
  }
  return size;
}
struct SharedImage::State {
  std::string name;
  void *data = MAP_FAILED;
  size_t size = 0;
};
SharedImage::SharedImage() : state_(std::make_unique<State>()) {}
SharedImage::~SharedImage() { Reset(); }
const std::string &SharedImage::Name() const { return state_->name; }
void SharedImage::Reset() {
  if (state_->data != MAP_FAILED)
    munmap(state_->data, state_->size);
  if (!state_->name.empty())
    shm_unlink(state_->name.c_str());
  state_->data = MAP_FAILED;
  state_->size = 0;
  state_->name.clear();
}
bool SharedImage::Assign(const uint8_t *data, size_t size) {
  Reset();
  if (!data || !size || size > 1920 * 1080 * 3)
    return false;
  static std::atomic<uint32_t> sequence{0};
  int fd = -1;
  for (int attempt = 0; attempt < 16; ++attempt) {
    // Fits Darwin's 31-byte name limit. O_EXCL prevents opening another
    // process's object, including a stale name from a recycled process ID.
    auto name = "/cdx-" + std::to_string(getpid()) + "-" +
                std::to_string(++sequence);
    fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
      state_->name = std::move(name);
      break;
    }
    if (errno != EEXIST)
      return false;
  }
  if (fd < 0)
    return false;
  fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef __linux__
  // Reserve tmpfs space before touching the mapping; a successful ftruncate
  // alone can still SIGBUS later when /dev/shm is full.
  if (posix_fallocate(fd, 0, size) != 0) {
    close(fd);
    Reset();
    return false;
  }
#endif
  if (ftruncate(fd, size) == 0) {
    state_->data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    state_->size = size;
  }
  close(fd);
  if (state_->data == MAP_FAILED) {
    Reset();
    return false;
  }
  std::memcpy(state_->data, data, size);
  return true;
}
struct NonBlocking::State {
  int fd, flags;
};
NonBlocking::NonBlocking(int fd, bool) : state_(std::make_unique<State>()) {
  state_->fd = fd;
  state_->flags = fcntl(fd, F_GETFL);
  if (state_->flags < 0 || fcntl(fd, F_SETFL, state_->flags | O_NONBLOCK) < 0)
    throw std::runtime_error("cannot configure nonblocking descriptor");
}
NonBlocking::~NonBlocking() { fcntl(state_->fd, F_SETFL, state_->flags); }
struct RawTerminal::State {
  int fd, flags;
  termios saved;
};
RawTerminal::RawTerminal(int input, int) : state_(std::make_unique<State>()) {
  state_->fd = input;
  state_->flags = fcntl(input, F_GETFL);
  if (state_->flags < 0 || tcgetattr(input, &state_->saved) != 0)
    throw std::runtime_error("cannot read terminal attributes");
  termios mode = state_->saved;
  cfmakeraw(&mode);
  if (tcsetattr(input, TCSAFLUSH, &mode) != 0)
    throw std::runtime_error("cannot enter terminal raw mode");
  if (fcntl(input, F_SETFL, state_->flags | O_NONBLOCK) < 0) {
    tcsetattr(input, TCSAFLUSH, &state_->saved);
    throw std::runtime_error("cannot configure terminal input");
  }
}
RawTerminal::~RawTerminal() {
  // TCSAFLUSH waits for queued output, which can hang exit when the terminal
  // stops consuming a partially written image. Restore immediately and flush
  // only pending input so protocol replies do not reach the next shell prompt.
  tcsetattr(state_->fd, TCSANOW, &state_->saved);
  tcflush(state_->fd, TCIFLUSH);
  fcntl(state_->fd, F_SETFL, state_->flags);
}
std::string Password() {
  int fd = open("/dev/tty", O_RDWR | O_NOCTTY);
  if (fd < 0)
    throw std::runtime_error("password requires an interactive TTY");
  struct Restore {
    int fd;
    termios mode{};
    bool active = false;
    ~Restore() {
      if (active) {
        tcsetattr(fd, TCSAFLUSH, &mode);
        write(fd, "\n", 1);
      }
      close(fd);
    }
  } restore{fd};
  if (tcgetattr(fd, &restore.mode))
    throw std::runtime_error("cannot read password TTY");
  auto mode = restore.mode;
  mode.c_lflag &= ~ECHO;
  if (tcsetattr(fd, TCSAFLUSH, &mode))
    throw std::runtime_error("cannot disable password echo");
  restore.active = true;
  const char prompt[] = "Remote password: ";
  write(fd, prompt, sizeof(prompt) - 1);
  std::string result;
  while (!interrupted) {
    if (!WaitReadable(fd, 100))
      continue;
    char c;
    auto n = read(fd, &c, 1);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0 || result.size() > 256)
      break;
    if (c == '\n' || c == '\r')
      return result;
    result += c;
  }
  throw std::runtime_error("password entry cancelled or too long");
}
std::string Logs() {
  char path[] = "/tmp/crossdesk-cli-XXXXXX";
  char *dir = mkdtemp(path);
  if (!dir)
    throw std::runtime_error("cannot create private log directory");
  return dir;
}
} // namespace crossdesk::cli
#endif
