#include "headless_session.h"
#include "desktop_session_selection.h"
#include "rd_log.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace crossdesk {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
volatile sig_atomic_t stop_signal = 0;
void RequestStop(int signal) { stop_signal = signal; }

// Bootstrap runs before GUI initialization, and may fork. Reuse the existing
// application logger when the headless console has silenced standard streams.
// Callers without a prepared console retain their normal stderr diagnostics.
bool log_diagnostics_to_file = false;
class Diagnostic {
 public:
  explicit Diagnostic(spdlog::level::level_enum level = spdlog::level::err)
      : level_(level) {}
  template <typename T> Diagnostic& operator<<(const T& value) {
    text_ << value;
    return *this;
  }
  ~Diagnostic() {
    std::string message = text_.str();
    if (!log_diagnostics_to_file) {
      std::cerr << message;
      return;
    }
    if (!message.empty() && message.back() == '\n') message.pop_back();
    get_logger()->log(level_, "{}", message);
  }
 private:
  spdlog::level::level_enum level_;
  std::ostringstream text_;
};

bool HasEnv(const char* name) {
  const char* value = std::getenv(name);
  return value && *value;
}

bool ParseDimension(const std::string& value, int& result) {
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
         result >= 320 && result <= 8192 && result % 2 == 0;
}

struct Options {
  bool force = false;
  bool disabled = false;
  bool help = false;
  bool new_session = false;
  std::string size = "1920x1080";
  std::string session;
  std::string display;
};

bool ParseOptions(int argc, char* argv[], Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--headless") {
      options.force = true;
    } else if (arg == "--no-headless") {
      options.disabled = true;
    } else if (arg == "--headless-help") {
      options.help = true;
    } else if (arg == "--headless-size" || arg == "--headless-session" ||
               arg == "--headless-display") {
      if (++i == argc || argv[i][0] == '\0') {
        Diagnostic() << arg << " requires a value\n";
        return false;
      }
      if (arg == "--headless-size") {
        options.size = argv[i];
        options.new_session = true;
      } else if (arg == "--headless-session") {
        options.session = argv[i];
        options.new_session = true;
      } else {
        options.display = argv[i];
      }
      options.force = true;
    } else if (arg.rfind("--headless", 0) == 0) {
      Diagnostic() << "Unknown option: " << arg << '\n';
      return false;
    }
  }
  int width = 0, height = 0;
  const auto separator = options.size.find('x');
  if (separator == std::string::npos ||
      !ParseDimension(options.size.substr(0, separator), width) ||
      !ParseDimension(options.size.substr(separator + 1), height)) {
    Diagnostic() << "--headless-size requires WIDTHxHEIGHT with even dimensions "
                 "between 320 and 8192\n";
    return false;
  }
  if (options.force && options.disabled) {
    Diagnostic() << "--no-headless conflicts with --headless options\n";
    return false;
  }
  if (!options.display.empty() && options.new_session) {
    Diagnostic() << "--headless-display reuses an existing desktop and cannot be "
                 "combined with --headless-size or --headless-session\n";
    return false;
  }
  return true;
}

struct ExistingDesktop {
  std::string display;
  std::optional<std::string> authority;
};

std::vector<DesktopLoginSession> GetDesktopLoginSessions() {
  // Optional runtime integration: non-systemd distributions and containers
  // still discover X11 desktops without a build/runtime dependency on logind.
  struct Library {
    void* handle = dlopen("libsystemd.so.0", RTLD_LAZY | RTLD_LOCAL);
    ~Library() { if (handle) dlclose(handle); }
  } library;
  if (!library.handle) return {};
  using UserSessions = int (*)(uid_t, int, char***);
  using SessionString = int (*)(const char*, char**);
  using SessionFlag = int (*)(const char*);
  using UserDisplay = int (*)(uid_t, char**);
  using PidSession = int (*)(pid_t, char**);
  const auto list = reinterpret_cast<UserSessions>(
      dlsym(library.handle, "sd_uid_get_sessions"));
  const auto type = reinterpret_cast<SessionString>(
      dlsym(library.handle, "sd_session_get_type"));
  const auto session_class = reinterpret_cast<SessionString>(
      dlsym(library.handle, "sd_session_get_class"));
  const auto state = reinterpret_cast<SessionString>(
      dlsym(library.handle, "sd_session_get_state"));
  const auto display = reinterpret_cast<SessionString>(
      dlsym(library.handle, "sd_session_get_display"));
  const auto active = reinterpret_cast<SessionFlag>(
      dlsym(library.handle, "sd_session_is_active"));
  const auto remote = reinterpret_cast<SessionFlag>(
      dlsym(library.handle, "sd_session_is_remote"));
  const auto primary = reinterpret_cast<UserDisplay>(
      dlsym(library.handle, "sd_uid_get_display"));
  const auto current = reinterpret_cast<PidSession>(
      dlsym(library.handle, "sd_pid_get_session"));
  if (!list || !type || !session_class || !state || !display) return {};
  auto read = [](SessionString function, const char* id) {
    char* value = nullptr;
    const int status = function(id, &value);
    std::string result = status >= 0 && value ? value : "";
    free(value);
    return result;
  };
  char* value = nullptr;
  std::string primary_id, current_id;
  if (primary && primary(getuid(), &value) >= 0 && value) primary_id = value;
  free(value);
  value = nullptr;
  if (current && current(0, &value) >= 0 && value) current_id = value;
  free(value);
  if (current_id.empty() && HasEnv("XDG_SESSION_ID")) {
    current_id = std::getenv("XDG_SESSION_ID");
  }
  char** ids = nullptr;
  const int count = list(getuid(), 0, &ids);
  if (count < 0 || !ids) return {};
  std::vector<DesktopLoginSession> sessions;
  for (int i = 0; i < count; ++i) {
    const std::string kind = read(session_class, ids[i]);
    const std::string status = read(state, ids[i]);
    if (read(type, ids[i]) == "x11" &&
        (kind == "user" || kind == "user-early") &&
        (status == "online" || status == "active")) {
      const auto address = NormalizeDesktopDisplay(read(display, ids[i]));
      // Only local displays, never greeters, SSH forwarding or other users.
      if (address.size() > 1 && address[0] == ':' &&
          address.find_first_not_of("0123456789.", 1) == std::string::npos) {
        sessions.push_back({address, current_id == ids[i], primary_id == ids[i],
                            active && active(ids[i]) > 0,
                            remote && remote(ids[i]) > 0});
      }
    }
    free(ids[i]);
  }
  free(ids);
  return sessions;
}

void SetAuthority(const std::optional<std::string>& authority) {
  if (authority) {
    setenv("XAUTHORITY", authority->c_str(), 1);
  } else {
    unsetenv("XAUTHORITY");
  }
}

Window ReadWindowProperty(Display* display, Window window, Atom property) {
  Atom type = None;
  int format = 0;
  unsigned long count = 0, remaining = 0;
  unsigned char* data = nullptr;
  Window result = None;
  if (XGetWindowProperty(display, window, property, 0, 1, False, XA_WINDOW,
                         &type, &format, &count, &remaining, &data) == Success &&
      type == XA_WINDOW && format == 32 && count == 1 && data) {
    result = *reinterpret_cast<Window*>(data);
  }
  if (data) XFree(data);
  return result;
}

bool HasDesktop(const std::string& address) {
  Display* display = XOpenDisplay(address.c_str());
  if (!display) return false;
  // A root framebuffer alone may be the empty Xvfb that caused the black
  // background. Require a live EWMH window manager, including its self-check.
  // Discovery runs before any threads; a WM disappearing mid-probe is benign.
  auto previous = XSetErrorHandler([](Display*, XErrorEvent*) { return 0; });
  const Window root = DefaultRootWindow(display);
  XWindowAttributes geometry{};
  const Atom property = XInternAtom(display, "_NET_SUPPORTING_WM_CHECK", True);
  const Window manager = property == None ? None
                                         : ReadWindowProperty(display, root, property);
  const bool valid = manager != None &&
                     ReadWindowProperty(display, manager, property) == manager &&
                     XGetWindowAttributes(display, root, &geometry) &&
                     geometry.width > 1 && geometry.height > 1;
  XCloseDisplay(display);
  XSetErrorHandler(previous);
  return valid;
}

std::vector<ExistingDesktop> FindExistingDesktops(
    const std::string& requested,
    const std::vector<DesktopLoginSession>& sessions) {
  const char* value = std::getenv("XAUTHORITY");
  const std::optional<std::string> original =
      value ? std::optional<std::string>(value) : std::nullopt;
  std::vector<std::optional<std::string>> authorities{original};
  // An explicit authority must be respected, including for automation and
  // isolated tests. Otherwise also try GDM's per-user authority file.
  if (!original) {
    const std::string gdm = "/run/user/" + std::to_string(getuid()) +
                            "/gdm/Xauthority";
    struct stat info{};
    if (stat(gdm.c_str(), &info) == 0 && info.st_uid == getuid() &&
        S_ISREG(info.st_mode) && access(gdm.c_str(), R_OK) == 0) {
      authorities.emplace_back(gdm);
    }
  }
  auto probe = [&](const std::string& address) -> std::optional<ExistingDesktop> {
    for (const auto& authority : authorities) {
      SetAuthority(authority);
      if (HasDesktop(address)) return ExistingDesktop{address, authority};
    }
    return std::nullopt;
  };
  std::vector<ExistingDesktop> matches;
  if (!requested.empty()) {
    if (auto match = probe(requested)) matches.push_back(*match);
  } else if (HasEnv("DISPLAY")) {
    // An explicit, usable desktop is unambiguous even with several logins.
    if (auto match = probe(std::getenv("DISPLAY"))) matches.push_back(*match);
  }
  if (requested.empty() && matches.empty()) {
    std::vector<std::string> candidates;
    // logind may identify a desktop whose X socket is owned by root. Its
    // session still belongs to our uid and must pass the same X11/WM probes.
    for (const auto& session : sessions) candidates.push_back(session.display);
    std::error_code error;
    std::filesystem::directory_iterator it("/tmp/.X11-unix", error), end;
    for (; !error && it != end; it.increment(error)) {
      const std::string name = it->path().filename().string();
      struct stat info{};
      if (name.size() > 1 && name[0] == 'X' &&
          name.find_first_not_of("0123456789", 1) == std::string::npos &&
          lstat(it->path().c_str(), &info) == 0 && S_ISSOCK(info.st_mode) &&
          info.st_uid == getuid()) {
        candidates.push_back(":" + name.substr(1));
      }
    }
    std::sort(candidates.begin(), candidates.end(), DesktopDisplayLess);
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                      candidates.end());
    for (const auto& address : candidates) {
      if (auto match = probe(address)) matches.push_back(*match);
    }
  }
  SetAuthority(original);
  return matches;
}

void ConfigureHeadlessX11(const std::string& display) {
  // Capture, input and all GUI components must use the same X11 desktop,
  // including when inheriting Wayland/DRM or NVIDIA PRIME hints from a shell.
  setenv("DISPLAY", display.c_str(), 1);
  setenv("XDG_SESSION_TYPE", "x11", 1);
  unsetenv("WAYLAND_DISPLAY");
  unsetenv("WAYLAND_SOCKET");
  setenv("CROSSDESK_SCREEN_BACKEND", "x11", 1);
  setenv("SDL_VIDEODRIVER", "x11", 1);
  setenv("SLINT_BACKEND", "winit-software", 1);
  setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
  unsetenv("__NV_PRIME_RENDER_OFFLOAD");
  unsetenv("__NV_PRIME_RENDER_OFFLOAD_PROVIDER");
  unsetenv("__GLX_VENDOR_LIBRARY_NAME");
}

std::string FindExecutable(const std::string& name) {
  if (name.find('/') != std::string::npos) {
    return access(name.c_str(), X_OK) == 0 ? name : std::string{};
  }
  const char* path = std::getenv("PATH");
  std::string paths = path ? path : "/usr/bin:/bin";
  size_t start = 0;
  do {
    const auto end = paths.find(':', start);
    const std::string directory = paths.substr(start, end - start);
    const std::string candidate =
        (directory.empty() ? "." : directory) + "/" + name;
    if (access(candidate.c_str(), X_OK) == 0) return candidate;
    if (end == std::string::npos) break;
    start = end + 1;
  } while (start <= paths.size());
  return {};
}

// Xauthority's binary format consists of a family and four counted strings,
// all lengths in network byte order. FamilyWild and an empty display number
// allow the cookie to be prepared before Xvfb allocates its display number.
class PrivateAuthority {
 public:
  bool Create() {
    char directory[] = "/tmp/crossdesk-headless-XXXXXX";
    if (!mkdtemp(directory)) return false;  // mode 0700
    directory_ = directory;
    path_ = directory_ + "/Xauthority";
    unsigned char cookie[16];
    size_t offset = 0;
    while (offset < sizeof(cookie)) {
      const ssize_t count = getrandom(cookie + offset, sizeof(cookie) - offset, 0);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) return false;
      offset += count;
    }
    const int fd = open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                        S_IRUSR | S_IWUSR);
    if (fd < 0) return false;
    FILE* file = fdopen(fd, "wb");
    if (!file) {
      close(fd);
      return false;
    }
    auto word = [file](unsigned int value) {
      return fputc((value >> 8) & 255, file) != EOF &&
             fputc(value & 255, file) != EOF;
    };
    constexpr char protocol[] = "MIT-MAGIC-COOKIE-1";
    const bool written = word(65535) && word(0) && word(0) &&
                         word(sizeof(protocol) - 1) &&
                         fwrite(protocol, 1, sizeof(protocol) - 1, file) ==
                             sizeof(protocol) - 1 &&
                         word(sizeof(cookie)) &&
                         fwrite(cookie, 1, sizeof(cookie), file) == sizeof(cookie);
    const bool closed = fclose(file) == 0;
    return written && closed;
  }
  ~PrivateAuthority() {
    if (!path_.empty()) unlink(path_.c_str());
    if (!directory_.empty()) rmdir(directory_.c_str());
  }
  const std::string& path() const { return path_; }

 private:
  std::string directory_;
  std::string path_;
};

class SignalGuard {
 public:
  SignalGuard() {
    stop_signal = 0;
    struct sigaction action {};
    action.sa_handler = RequestStop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, &old_int_);
    sigaction(SIGTERM, &action, &old_term_);
    sigaction(SIGHUP, &action, &old_hup_);
  }
  ~SignalGuard() {
    sigaction(SIGINT, &old_int_, nullptr);
    sigaction(SIGTERM, &old_term_, nullptr);
    sigaction(SIGHUP, &old_hup_, nullptr);
  }

 private:
  struct sigaction old_int_ {}, old_term_ {}, old_hup_ {};
};

class Child {
 public:
  Child() = default;
  Child(const Child&) = delete;
  Child& operator=(const Child&) = delete;
  ~Child() { Stop(); }

  bool Start(const std::function<int()>& run, bool own_group = true) {
    own_group_ = own_group;
    const pid_t parent = getpid();
    pid_ = fork();
    if (pid_ == 0) {
      if (own_group_) setpgid(0, 0);
      signal(SIGINT, SIG_DFL);
      signal(SIGTERM, SIG_DFL);
      signal(SIGHUP, SIG_DFL);
      // Also stop direct children if the supervisor itself is killed.
      if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 || getppid() != parent) _exit(1);
      _exit(run());
    }
    if (pid_ < 0) return false;
    if (own_group_) setpgid(pid_, pid_);
    return true;
  }

  bool Exited(int* code = nullptr) const {
    if (pid_ <= 0) return false;
    siginfo_t info{};
    // Leave the child waitable until Stop(), preventing pid/group reuse.
    if (waitid(P_PID, pid_, &info, WEXITED | WNOHANG | WNOWAIT) != 0 ||
        info.si_pid == 0) return false;
    if (code) {
      *code = info.si_code == CLD_EXITED ? info.si_status : 128 + info.si_status;
    }
    return true;
  }

  void Stop() {
    if (pid_ <= 0) return;
    const pid_t target = own_group_ ? -pid_ : pid_;
    kill(target, SIGTERM);
    const auto deadline = Clock::now() + 2s;
    while (!Exited() && Clock::now() < deadline) std::this_thread::sleep_for(25ms);
    kill(target, SIGKILL);  // Include descendants of desktop session scripts.
    while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {}
    pid_ = -1;
  }

 private:
  pid_t pid_ = -1;
  bool own_group_ = true;
};

int Exec(const std::vector<std::string>& arguments) {
  std::vector<char*> argv;
  for (const auto& arg : arguments) argv.push_back(const_cast<char*>(arg.c_str()));
  argv.push_back(nullptr);
  execv(argv[0], argv.data());
  Diagnostic() << "Cannot start " << arguments[0] << ": " << std::strerror(errno)
            << '\n';
  return 127;
}

bool StartServer(Child& server, const std::string& executable,
                 const Options& options, const PrivateAuthority& authority,
                 std::string& display) {
  int pipe_fds[2];
  if (pipe2(pipe_fds, O_CLOEXEC | O_NONBLOCK) != 0) return false;
  const bool started = server.Start([&] {
    close(pipe_fds[0]);
    fcntl(pipe_fds[1], F_SETFD, 0);
    return Exec({executable, "-displayfd", std::to_string(pipe_fds[1]),
                 "-screen", "0", options.size + "x24", "-nolisten", "tcp",
                 "-auth", authority.path(), "-noreset"});
  });
  close(pipe_fds[1]);
  std::string number;
  const auto deadline = Clock::now() + 10s;
  while (started && !stop_signal && !server.Exited() && Clock::now() < deadline) {
    struct pollfd descriptor { pipe_fds[0], POLLIN, 0 };
    if (poll(&descriptor, 1, 50) <= 0) continue;
    char buffer[32];
    const ssize_t size = read(pipe_fds[0], buffer, sizeof(buffer));
    if (size <= 0) break;
    number.append(buffer, size);
    if (number.find('\n') != std::string::npos || number.size() > 16) break;
  }
  close(pipe_fds[0]);
  if (number.size() < 2 || number.back() != '\n') return false;
  number.pop_back();
  if (number.find_first_not_of("0123456789") != std::string::npos) return false;
  display = ":" + number;
  return true;
}

int RunHeadless(const Options& options, const std::function<int()>& run) {
  const std::string xvfb = FindExecutable("Xvfb");
  if (xvfb.empty()) {
    Diagnostic() << "No usable display. Headless mode requires Xvfb; on Debian/Ubuntu "
                 "install it with: sudo apt install xvfb\n";
    return 1;
  }
  std::string session;
  std::string dbus;
  if (!options.session.empty() && options.session != "none") {
    session = FindExecutable(options.session);
    dbus = FindExecutable("dbus-run-session");
    if (session.empty() || dbus.empty()) {
      Diagnostic() << "Headless desktop requires an executable --headless-session "
                   "and dbus-run-session (Debian/Ubuntu: dbus-daemon)\n";
      return 1;
    }
  }
  SignalGuard signals;
  PrivateAuthority authority;
  if (!authority.Create()) {
    Diagnostic() << "Cannot create private headless Xauthority: "
              << std::strerror(errno) << '\n';
    return 1;
  }
  Child server;
  std::string display;
  if (!StartServer(server, xvfb, options, authority, display)) {
    Diagnostic() << "Xvfb failed to become ready within 10 seconds\n";
    return stop_signal ? 128 + stop_signal : 1;
  }
  ConfigureHeadlessX11(display);
  setenv("XAUTHORITY", authority.path().c_str(), 1);
  setenv("CROSSDESK_HEADLESS_ACTIVE", "1", 1);

  Display* probe = XOpenDisplay(nullptr);
  if (!probe) {
    Diagnostic() << "Cannot connect to the private Xvfb display\n";
    return 1;
  }
  XSetScreenSaver(probe, 0, 0, DefaultBlanking, DefaultExposures);
  XCloseDisplay(probe);
  Diagnostic(spdlog::level::info) << "CrossDesk headless display " << display << " (" << options.size
            << "), XAUTHORITY=" << authority.path() << '\n';
  Child desktop;
  if (!session.empty() && !desktop.Start([&] {
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
        unsetenv("SESSION_MANAGER");
        return Exec({dbus, "--", session});
      })) return 1;
  Child application;
  // Keep an interactive application in the foreground terminal group so its
  // console can read commands. Xvfb and the optional desktop remain isolated.
  if (!application.Start(run, !isatty(STDIN_FILENO))) return 1;
  int result = 1;
  while (!stop_signal) {
    if (application.Exited(&result)) return result;
    if (server.Exited() || desktop.Exited()) {
      Diagnostic() << "Headless display or desktop session exited; stopping CrossDesk\n";
      return 1;
    }
    std::this_thread::sleep_for(50ms);
  }
  return 128 + stop_signal;
}
}  // namespace

int RunWithLinuxDisplay(int argc, char* argv[],
                        const std::function<int()>& run_application,
                        const std::function<bool()>& prepare_headless) {
  log_diagnostics_to_file = false;
  Options options;
  if (!ParseOptions(argc, argv, options)) return 2;
  if (options.help) {
    std::cout << "Linux headless options:\n"
                 "  --headless                  Reuse an existing desktop when available\n"
                 "  --headless-display DISPLAY  Advanced: override automatic desktop selection\n"
                 "  --headless-size WIDTHxHEIGHT  New Xvfb; default 1920x1080; even, 320..8192\n"
                 "  --headless-session EXECUTABLE New desktop (startxfce4, or none for bare Xvfb)\n"
                 "  --no-headless               Disable automatic Xvfb fallback\n"
                 "Normal startup automatically discovers and selects your existing "
                 "desktop. No display number is required. A new Xvfb does not "
                 "contain your existing apps.\n";
    return 0;
  }
  XInitThreads();
  if (!options.force) {
    if (options.disabled || HasEnv("WAYLAND_DISPLAY") || HasEnv("WAYLAND_SOCKET")) {
      return run_application();
    }
    // Preserve normal graphical launches before SSH/headless discovery.
    if (HasEnv("DISPLAY")) {
      if (Display* display = XOpenDisplay(nullptr)) {
        XCloseDisplay(display);
        return run_application();
      }
    }
  }
  if (prepare_headless) {
    if (!prepare_headless()) return 1;
    log_diagnostics_to_file = true;
  }
  if (!options.new_session) {
    const auto sessions = GetDesktopLoginSessions();
    const auto desktops = FindExistingDesktops(options.display, sessions);
    std::vector<std::string> available;
    for (const auto& desktop : desktops) available.push_back(desktop.display);
    if (auto selected = SelectDesktopSession(available, sessions)) {
      const auto& desktop = desktops[*selected];
      SetAuthority(desktop.authority);
      ConfigureHeadlessX11(desktop.display);
      Diagnostic(spdlog::level::info) << "CrossDesk reusing existing desktop " << desktop.display
                << "; Dock, panels and running applications are preserved\n";
      if (desktops.size() > 1) {
        Diagnostic(spdlog::level::info) << "Automatically selected from " << desktops.size()
                  << " available desktops using login-session priority\n";
      }
      // The desktop belongs to the user, not this process. Never supervise or
      // terminate its X server, window manager or applications on exit.
      return run_application();
    }
    if (!options.display.empty()) {
      Diagnostic() << "No accessible desktop/window manager on " << options.display
                << ". Check DISPLAY and XAUTHORITY; no new desktop was created.\n";
      return 1;
    }
  }
  if (options.session.empty() || options.session == "none") {
    Diagnostic(spdlog::level::warn) << "Starting a separate bare Xvfb display: it has no Dock, panels "
                 "or existing applications. To include a desktop environment, "
                 "use --headless-session startxfce4 after installing Xfce.\n";
  }
  return RunHeadless(options, run_application);
}
}  // namespace crossdesk
