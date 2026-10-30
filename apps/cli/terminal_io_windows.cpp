#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "terminal_io.h"
#include <windows.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <io.h>
#include <random>
#include <sddl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>

namespace crossdesk::cli {
std::atomic<int> interrupted{0};
namespace {
int console_output = -1;
std::string input_bytes;
wchar_t high_surrogate = 0;
DWORD mouse_buttons = 0;
HANDLE Handle(int fd) {
#ifdef _MSC_VER
  // A missing optional credential fd should be an error response, not a CRT
  // invalid-parameter process termination. Keep the handler thread-local.
  auto previous = _set_thread_local_invalid_parameter_handler(
      [](const wchar_t *, const wchar_t *, const wchar_t *, unsigned,
         uintptr_t) {});
  auto value = _get_osfhandle(fd);
  _set_thread_local_invalid_parameter_handler(previous);
  return reinterpret_cast<HANDLE>(value);
#else
  return reinterpret_cast<HANDLE>(_get_osfhandle(fd));
#endif
}
void Signal(int value) { interrupted.store(value, std::memory_order_relaxed); }
BOOL WINAPI ConsoleSignal(DWORD value) {
  if (value == CTRL_C_EVENT || value == CTRL_BREAK_EVENT) {
    interrupted.store(SIGINT, std::memory_order_relaxed);
    return TRUE;
  }
  return FALSE;
}
std::string Utf8(const std::wstring &value) {
  if (value.empty())
    return {};
  int size =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          int(value.size()), nullptr, 0, nullptr, nullptr);
  if (!size)
    throw std::runtime_error("invalid Unicode input");
  std::string result(size, '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                      int(value.size()), result.data(), size, nullptr, nullptr);
  return result;
}
std::wstring Wide(const std::string &value) {
  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                 int(value.size()), nullptr, 0);
  if (!size)
    throw std::runtime_error("invalid Unicode path");
  std::wstring result(size, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      int(value.size()), result.data(), size);
  return result;
}
void Character(wchar_t c) {
  if (c >= 0xd800 && c <= 0xdbff) {
    high_surrogate = c;
    return;
  }
  if (c >= 0xdc00 && c <= 0xdfff) {
    if (high_surrogate)
      input_bytes += Utf8(std::wstring{high_surrogate, c});
  } else if (c) {
    input_bytes += Utf8(std::wstring(1, c));
  }
  high_surrogate = 0;
}
void Key(const KEY_EVENT_RECORD &key) {
  if (!key.bKeyDown)
    return;
  const char *sequence = nullptr;
  switch (key.wVirtualKeyCode) {
  case VK_UP:
    sequence = "\033[A";
    break;
  case VK_DOWN:
    sequence = "\033[B";
    break;
  case VK_RIGHT:
    sequence = "\033[C";
    break;
  case VK_LEFT:
    sequence = "\033[D";
    break;
  case VK_HOME:
    sequence = "\033[H";
    break;
  case VK_END:
    sequence = "\033[F";
    break;
  case VK_DELETE:
    sequence = "\033[3~";
    break;
  case VK_PRIOR:
    sequence = "\033[5~";
    break;
  case VK_NEXT:
    sequence = "\033[6~";
    break;
  case VK_F1:
    sequence = "\033OP";
    break;
  case VK_F2:
    sequence = "\033OQ";
    break;
  case VK_F3:
    sequence = "\033OR";
    break;
  case VK_F4:
    sequence = "\033OS";
    break;
  }
  for (int i = 0; i < std::min<int>(key.wRepeatCount, 64); ++i) {
    if (sequence)
      input_bytes += sequence;
    else {
      // Right Alt + Ctrl is AltGr, not an Alt shortcut.
      bool alt = key.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED);
      bool ctrl =
          key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);
      if (alt && !ctrl && key.uChar.UnicodeChar)
        input_bytes += '\033';
      Character(key.uChar.UnicodeChar);
    }
  }
}
void Mouse(const MOUSE_EVENT_RECORD &mouse) {
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (console_output < 0 ||
      !GetConsoleScreenBufferInfo(Handle(console_output), &info))
    return;
  int x = mouse.dwMousePosition.X - info.srWindow.Left + 1;
  int y = mouse.dwMousePosition.Y - info.srWindow.Top + 1;
  if (x < 1 || y < 1)
    return;
  auto emit = [&](int button, bool release = false) {
    input_bytes += "\033[<" + std::to_string(button) + ";" + std::to_string(x) +
                   ";" + std::to_string(y) + (release ? "m" : "M");
  };
  DWORD buttons = mouse.dwButtonState & 0xffff;
  const DWORD masks[] = {FROM_LEFT_1ST_BUTTON_PRESSED,
                         FROM_LEFT_2ND_BUTTON_PRESSED,
                         RIGHTMOST_BUTTON_PRESSED};
  for (int i = 0; i < 3; ++i)
    if ((buttons & masks[i]) != (mouse_buttons & masks[i]))
      emit(i, !(buttons & masks[i]));
  if (mouse.dwEventFlags & (MOUSE_WHEELED | MOUSE_HWHEELED)) {
    int delta = static_cast<SHORT>(HIWORD(mouse.dwButtonState));
    emit(64 + ((mouse.dwEventFlags & MOUSE_HWHEELED) ? 2 : 0) + (delta < 0));
  } else if (mouse.dwEventFlags & MOUSE_MOVED) {
    emit(32 + ((buttons & masks[0])   ? 0
               : (buttons & masks[1]) ? 1
               : (buttons & masks[2]) ? 2
                                      : 3));
  }
  mouse_buttons = buttons;
}
bool Ready(int fd) {
  if (fd < 0)
    return false;
  HANDLE handle = Handle(fd);
  if (handle == INVALID_HANDLE_VALUE)
    throw std::runtime_error("input descriptor unavailable");
  DWORD mode = 0;
  if (GetConsoleMode(handle, &mode)) {
    DWORD count = 0;
    return !input_bytes.empty() ||
           (GetNumberOfConsoleInputEvents(handle, &count) && count);
  }
  if (GetFileType(handle) == FILE_TYPE_PIPE) {
    DWORD available = 0;
    if (PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr))
      return available != 0;
    if (GetLastError() == ERROR_BROKEN_PIPE)
      return true;
    throw std::runtime_error("cannot inspect input pipe");
  }
  return true;
}
} // namespace
void InitializeIo() {
  // Preserve LF-delimited JSON and terminal escape bytes through CRT streams.
  for (int fd : {0, 1, 2})
    _setmode(fd, _O_BINARY);
  std::signal(SIGINT, Signal);
  std::signal(SIGTERM, Signal);
  SetConsoleCtrlHandler(ConsoleSignal, TRUE);
}
const char *Platform() { return "windows"; }
bool IsTty(int fd) {
  DWORD mode;
  return GetConsoleMode(Handle(fd), &mode) != 0;
}
std::ptrdiff_t Read(int fd, void *buffer, size_t size) {
  if (!size)
    return 0;
  HANDLE handle = Handle(fd);
  DWORD mode;
  if (GetConsoleMode(handle, &mode)) {
    if (input_bytes.empty()) {
      DWORD count = 0, read = 0;
      if (!GetNumberOfConsoleInputEvents(handle, &count)) {
        errno = EIO;
        return -1;
      }
      INPUT_RECORD records[128];
      if (count && ReadConsoleInputW(handle, records,
                                     std::min<DWORD>(count, 128), &read))
        for (DWORD i = 0; i < read; ++i) {
          if (records[i].EventType == KEY_EVENT)
            Key(records[i].Event.KeyEvent);
          else if (records[i].EventType == MOUSE_EVENT)
            Mouse(records[i].Event.MouseEvent);
          else if (records[i].EventType == FOCUS_EVENT &&
                   !records[i].Event.FocusEvent.bSetFocus)
            input_bytes += "\033[O";
        }
    }
    if (input_bytes.empty()) {
      errno = EAGAIN;
      return -1;
    }
    size_t count = std::min(size, input_bytes.size());
    std::copy_n(input_bytes.data(), count, static_cast<char *>(buffer));
    input_bytes.erase(0, count);
    return count;
  }
  if (GetFileType(handle) == FILE_TYPE_PIPE) {
    DWORD available = 0;
    if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
      if (GetLastError() == ERROR_BROKEN_PIPE)
        return 0;
      errno = EIO;
      return -1;
    }
    if (!available) {
      errno = EAGAIN;
      return -1;
    }
    size = std::min<size_t>(size, available);
  }
  DWORD count = 0;
  if (!ReadFile(handle, buffer, DWORD(std::min<size_t>(size, 16384)), &count,
                nullptr)) {
    if (GetLastError() == ERROR_BROKEN_PIPE)
      return 0;
    errno = EIO;
    return -1;
  }
  return count;
}
std::ptrdiff_t Write(int fd, const void *buffer, size_t size) {
  DWORD count = 0;
  if (!WriteFile(Handle(fd), buffer, DWORD(std::min<size_t>(size, 16384)),
                 &count, nullptr)) {
    errno = GetLastError() == ERROR_NO_DATA ? EPIPE : EIO;
    return -1;
  }
  if (!count && size) {
    errno = EAGAIN;
    return -1;
  }
  return count;
}
void Close(int fd) { _close(fd); }
int Duplicate(int fd) { return _dup(fd); }
int Redirect(int from, int to) { return _dup2(from, to); }
int OpenLog(const std::string &path) {
  return _wopen(Wide(path).c_str(),
                _O_CREAT | _O_WRONLY | _O_APPEND | _O_BINARY,
                _S_IREAD | _S_IWRITE);
}
bool WaitReadable(int fd, int timeout_ms) {
  auto until =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  do {
    if (Ready(fd))
      return true;
    if (interrupted)
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (std::chrono::steady_clock::now() < until);
  return false;
}
void WaitIo(int input_fd, int, int timeout_ms) {
  WaitReadable(input_fd, timeout_ms);
}
TerminalSize GetTerminalSize(int) {
  TerminalSize size;
  HANDLE handle = console_output >= 0
                      ? Handle(console_output)
                      : CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (GetConsoleScreenBufferInfo(handle, &info)) {
    size.columns = info.srWindow.Right - info.srWindow.Left + 1;
    size.rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    // ConPTY's console window is only a message window, and its font API
    // describes the hidden console host, not the user's display terminal.
    // Only trust native font metrics for a visible classic console.
    HWND window = GetConsoleWindow();
    CONSOLE_FONT_INFOEX font{};
    font.cbSize = sizeof(font);
    if (window && IsWindowVisible(window) &&
        GetCurrentConsoleFontEx(handle, FALSE, &font) &&
        font.dwFontSize.X > 0 && font.dwFontSize.X <= 1024 &&
        font.dwFontSize.Y > 0 && font.dwFontSize.Y <= 1024) {
      size.pixel_width = size.columns * font.dwFontSize.X;
      size.pixel_height = size.rows * font.dwFontSize.Y;
    }
  }
  if (console_output < 0 && handle != INVALID_HANDLE_VALUE)
    CloseHandle(handle);
  return size;
}
struct NonBlocking::State {
  HANDLE handle;
  DWORD mode = 0;
  bool restore = false;
};
NonBlocking::NonBlocking(int fd, bool output)
    : state_(std::make_unique<State>()) {
  state_->handle = Handle(fd);
  // Reads use PeekNamedPipe; output pipes additionally require bounded writes.
  if (output && GetFileType(state_->handle) == FILE_TYPE_PIPE) {
    DWORD flags = 0;
    if (!GetNamedPipeHandleStateW(state_->handle, &state_->mode, nullptr,
                                  nullptr, nullptr, nullptr, 0))
      throw std::runtime_error("cannot read pipe mode");
    flags = state_->mode | PIPE_NOWAIT;
    if (!SetNamedPipeHandleState(state_->handle, &flags, nullptr, nullptr))
      throw std::runtime_error("cannot configure nonblocking pipe");
    state_->restore = true;
  }
}
NonBlocking::~NonBlocking() {
  if (state_->restore)
    SetNamedPipeHandleState(state_->handle, &state_->mode, nullptr, nullptr);
}
struct RawTerminal::State {
  HANDLE input, output;
  DWORD input_mode, output_mode;
  UINT input_cp, output_cp;
  State(HANDLE in, HANDLE out, DWORD in_mode, DWORD out_mode)
      : input(in), output(out), input_mode(in_mode), output_mode(out_mode),
        input_cp(GetConsoleCP()), output_cp(GetConsoleOutputCP()) {}
  State(const State &) = delete;
  State &operator=(const State &) = delete;
  ~State() {
    SetConsoleMode(input, input_mode);
    SetConsoleMode(output, output_mode);
    SetConsoleCP(input_cp);
    SetConsoleOutputCP(output_cp);
    console_output = -1;
    input_bytes.clear();
    high_surrogate = 0;
    mouse_buttons = 0;
  }
};
RawTerminal::RawTerminal(int input, int output) {
  DWORD in_mode = 0, out_mode = 0;
  if (!GetConsoleMode(Handle(input), &in_mode) ||
      !GetConsoleMode(Handle(output), &out_mode))
    throw std::runtime_error(
        "view requires a Windows console or ConPTY terminal");
  state_ = std::make_unique<State>(Handle(input), Handle(output), in_mode, out_mode);
  DWORD mode = (in_mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
                            ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE |
                            ENABLE_VIRTUAL_TERMINAL_INPUT)) |
               ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
  if (!SetConsoleMode(Handle(input), mode) ||
      !SetConsoleMode(Handle(output), out_mode | ENABLE_PROCESSED_OUTPUT |
                                          ENABLE_VIRTUAL_TERMINAL_PROCESSING) ||
      !SetConsoleCP(CP_UTF8) || !SetConsoleOutputCP(CP_UTF8))
    throw std::runtime_error(
        "terminal requires Windows 10 or newer with VT support");
  console_output = output;
}
RawTerminal::~RawTerminal() = default;
std::string Password() {
  HANDLE input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, 0, nullptr);
  HANDLE output =
      CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                  nullptr, OPEN_EXISTING, 0, nullptr);
  struct Restore {
    HANDLE input, output;
    DWORD mode = 0;
    bool active = false;
    ~Restore() {
      if (active) {
        SetConsoleMode(input, mode);
        DWORD count;
        WriteConsoleW(output, L"\r\n", 2, &count, nullptr);
      }
      if (input != INVALID_HANDLE_VALUE)
        CloseHandle(input);
      if (output != INVALID_HANDLE_VALUE)
        CloseHandle(output);
    }
  } restore{input, output};
  if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE ||
      !GetConsoleMode(input, &restore.mode))
    throw std::runtime_error("password requires an interactive console");
  if (!SetConsoleMode(
          input, (restore.mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT)) |
                     ENABLE_PROCESSED_INPUT))
    throw std::runtime_error("cannot disable password echo");
  restore.active = true;
  DWORD count;
  WriteConsoleW(output, L"Remote password: ", 17, &count, nullptr);
  std::wstring value;
  while (!interrupted) {
    if (WaitForSingleObject(input, 100) != WAIT_OBJECT_0)
      continue;
    INPUT_RECORD record{};
    if (!ReadConsoleInputW(input, &record, 1, &count))
      break;
    if (!count || record.EventType != KEY_EVENT ||
        !record.Event.KeyEvent.bKeyDown)
      continue;
    wchar_t c = record.Event.KeyEvent.uChar.UnicodeChar;
    if (c == L'\r') {
      auto result = Utf8(value);
      if (result.size() <= 256)
        return result;
      break;
    }
    if (c == L'\b') {
      if (!value.empty()) {
        wchar_t last = value.back();
        value.pop_back();
        if (last >= 0xdc00 && last <= 0xdfff && !value.empty())
          value.pop_back();
      }
    } else if (c >= 32) {
      value += c;
      if (value.size() > 256)
        break;
    }
  }
  throw std::runtime_error("password entry cancelled or too long");
}
std::string Logs() {
  wchar_t root[MAX_PATH + 1];
  DWORD size = GetTempPathW(MAX_PATH + 1, root);
  if (!size || size > MAX_PATH)
    throw std::runtime_error("cannot locate temporary directory");
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;OW)", SDDL_REVISION_1, &descriptor,
          nullptr))
    throw std::runtime_error("cannot secure log directory");
  struct Release {
    PSECURITY_DESCRIPTOR p;
    ~Release() { LocalFree(p); }
  } release{descriptor};
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
  std::random_device random;
  for (int attempt = 0; attempt < 10; ++attempt) {
    std::wstring path = std::wstring(root) + L"crossdesk-cli-" +
                        std::to_wstring(random()) + L"-" +
                        std::to_wstring(random());
    if (CreateDirectoryW(path.c_str(), &security))
      return Utf8(path);
    if (GetLastError() != ERROR_ALREADY_EXISTS)
      break;
  }
  throw std::runtime_error("cannot create private log directory");
}
} // namespace crossdesk::cli
#endif
