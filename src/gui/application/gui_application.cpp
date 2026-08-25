#include "application/gui_application.h"

#include <SDL3/SDL.h>
#include <slint.h>
#include <tinyfiledialogs.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "crossdesk_ui.h"
#include "fa_solid_900.h"
#include "localization.h"
#include "platform.h"
#include "platform/video_renderer.h"
#include "rendering/slint_video_presenter.h"
#if _WIN32
#include <windows.h>

#include "platform/tray/win_tray.h"
#elif defined(__APPLE__)
#include "platform/tray/mac_tray.h"
#include "platform/window_drag.h"
#elif defined(__linux__)
#include <GL/gl.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <unistd.h>

#include "platform/tray/linux_tray.h"
#endif
#include "rd_log.h"
#include "server_window_state.h"
#include "ui/ui_localization.h"
#include "version_checker.h"
#include "window_geometry.h"

namespace crossdesk {
namespace {

using namespace std::chrono_literals;

std::string CreatePasswordChangeRequestId(uint64_t sequence) {
  const auto timestamp = std::chrono::system_clock::now()
                             .time_since_epoch()
                             .count();
  std::random_device random;
  return std::to_string(timestamp) + "-" + std::to_string(random()) + "-" +
         std::to_string(sequence);
}

#if defined(__linux__) && !defined(__APPLE__)
bool HasNonEmptyEnvironmentVariable(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && value[0] != '\0';
}

class ScopedUnsetEnvironmentVariable {
 public:
  explicit ScopedUnsetEnvironmentVariable(const char* name) : name_(name) {
    if (const char* value = std::getenv(name_)) {
      original_value_ = value;
    }
    unsetenv(name_);
  }

  ~ScopedUnsetEnvironmentVariable() {
    if (original_value_) {
      setenv(name_, original_value_->c_str(), 1);
    } else {
      unsetenv(name_);
    }
  }

  ScopedUnsetEnvironmentVariable(const ScopedUnsetEnvironmentVariable&) =
      delete;
  ScopedUnsetEnvironmentVariable& operator=(
      const ScopedUnsetEnvironmentVariable&) = delete;

 private:
  const char* name_;
  std::optional<std::string> original_value_;
};

::Window X11GetActiveWindow(Display* display) {
  if (!display) {
    return 0;
  }

  const ::Window root = DefaultRootWindow(display);
  const Atom active_window_atom =
      XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
  if (active_window_atom != None) {
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long item_count = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(display, root, active_window_atom, 0, 1, False,
                           XA_WINDOW, &actual_type, &actual_format, &item_count,
                           &bytes_after, &data) == Success &&
        actual_type == XA_WINDOW && actual_format == 32 && item_count == 1 &&
        data) {
      const ::Window active_window = *reinterpret_cast<const ::Window*>(data);
      XFree(data);
      return active_window;
    }
    if (data) {
      XFree(data);
    }
  }

  ::Window focused_window = 0;
  int revert_to = RevertToNone;
  XGetInputFocus(display, &focused_window, &revert_to);
  return focused_window == None || focused_window == PointerRoot
             ? 0
             : focused_window;
}

std::optional<unsigned long> X11GetWindowProcessId(Display* display,
                                                   ::Window x11_window) {
  if (!display || !x11_window) {
    return std::nullopt;
  }

  const Atom process_id_atom = XInternAtom(display, "_NET_WM_PID", True);
  if (process_id_atom == None) {
    return std::nullopt;
  }

  Atom actual_type = None;
  int actual_format = 0;
  unsigned long item_count = 0;
  unsigned long bytes_after = 0;
  unsigned char* data = nullptr;
  const int status = XGetWindowProperty(
      display, x11_window, process_id_atom, 0, 1, False, XA_CARDINAL,
      &actual_type, &actual_format, &item_count, &bytes_after, &data);
  if (status != Success || actual_type != XA_CARDINAL || actual_format != 32 ||
      item_count != 1 || !data) {
    if (data) {
      XFree(data);
    }
    return std::nullopt;
  }

  const auto process_id = *reinterpret_cast<const unsigned long*>(data);
  XFree(data);
  return process_id;
}

bool X11WindowMatches(Display* display, ::Window x11_window,
                      const slint::Window& slint_window) {
  if (!display || !x11_window) {
    return false;
  }

  XWindowAttributes attributes;
  if (!XGetWindowAttributes(display, x11_window, &attributes)) {
    return false;
  }

  int root_x = 0;
  int root_y = 0;
  ::Window child = 0;
  if (!XTranslateCoordinates(display, x11_window, DefaultRootWindow(display), 0,
                             0, &root_x, &root_y, &child)) {
    return false;
  }

  const auto position = slint_window.position();
  const auto size = slint_window.size();
  constexpr int kGeometryTolerance = 4;
  return std::abs(root_x - position.x) <= kGeometryTolerance &&
         std::abs(root_y - position.y) <= kGeometryTolerance &&
         std::abs(attributes.width - static_cast<int>(size.width)) <=
             kGeometryTolerance &&
         std::abs(attributes.height - static_cast<int>(size.height)) <=
             kGeometryTolerance;
}
#endif

#if _WIN32
HICON LoadSlintTrayIcon() {
  HMODULE module = GetModuleHandleW(nullptr);
  HICON icon = reinterpret_cast<HICON>(
      LoadImageW(module, L"IDI_ICON1", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
  return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}
#endif

slint::SharedString UiText(std::string_view value) {
  return slint::SharedString(value);
}

void RegisterFontAwesome(slint::Window& window) {
  if (const auto error = window.window_handle().register_font_from_data(
          fa_solid_900_ttf, fa_solid_900_ttf_len)) {
    LOG_ERROR("Failed to register Font Awesome for Slint window: {}",
              error->data());
  }
}

std::string FormatPeerId(const char* id) {
  if (!id) {
    return {};
  }
  std::string compact(id);
  if (compact.size() != 9) {
    return compact;
  }
  compact.insert(3, " ");
  compact.insert(7, " ");
  return compact;
}

std::string FormatRemotePeerIdInput(std::string_view input) {
  std::string digits;
  digits.reserve(9);
  for (const unsigned char ch : input) {
    if (std::isdigit(ch) != 0 && digits.size() < 9) {
      digits.push_back(static_cast<char>(ch));
    }
  }
  if (digits.size() > 3) {
    digits.insert(3, " ");
  }
  if (digits.size() > 7) {
    digits.insert(7, " ");
  }
  return digits;
}

std::string CompactPeerId(std::string id) {
  id.erase(
      std::remove_if(id.begin(), id.end(),
                     [](unsigned char ch) { return std::isspace(ch) != 0; }),
      id.end());
  return id;
}

std::string Trim(std::string value) {
  const auto first =
      std::find_if_not(value.begin(), value.end(),
                       [](unsigned char ch) { return std::isspace(ch) != 0; });
  const auto last =
      std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
      }).base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

struct ParsedReleaseNoteBlock {
  slint::StyledText content;
  bool section_gap = false;
};

std::vector<ParsedReleaseNoteBlock> ParseReleaseNotesMarkdownForSlint(
    std::string_view markdown) {
  std::vector<ParsedReleaseNoteBlock> blocks;
  bool section_gap = false;

  size_t line_start = 0;
  while (line_start < markdown.size()) {
    const size_t newline = markdown.find('\n', line_start);
    const size_t line_end =
        newline == std::string_view::npos ? markdown.size() : newline;
    std::string_view line = markdown.substr(line_start, line_end - line_start);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }

    if (std::all_of(line.begin(), line.end(),
                    [](unsigned char ch) { return std::isspace(ch) != 0; })) {
      section_gap = !blocks.empty();
      if (newline == std::string_view::npos) {
        break;
      }
      line_start = newline + 1;
      continue;
    }

    size_t heading_end = 0;
    while (heading_end < line.size() && heading_end < 6 &&
           line[heading_end] == '#') {
      ++heading_end;
    }
    const bool is_heading =
        heading_end > 0 && heading_end < line.size() &&
        std::isspace(static_cast<unsigned char>(line[heading_end])) != 0;
    if (is_heading) {
      if (newline == std::string_view::npos) {
        break;
      }
      line_start = newline + 1;
      continue;
    }

    const std::string block_markdown(line);
    ParsedReleaseNoteBlock block;
    if (const auto parsed = slint::StyledText::from_markdown(block_markdown)) {
      block.content = *parsed;
    } else {
      LOG_WARN(
          "Failed to parse a release-note Markdown block; using plain text");
      block.content = slint::StyledText::from_plain_text(block_markdown);
    }
    block.section_gap = section_gap;
    blocks.push_back(std::move(block));
    section_gap = false;

    if (newline == std::string_view::npos) {
      break;
    }
    line_start = newline + 1;
  }
  return blocks;
}

std::string DecodeDroppedPath(std::string text) {
  const size_t line_end = text.find_first_of("\r\n");
  if (line_end != std::string::npos) {
    text.resize(line_end);
  }
  text = Trim(std::move(text));
  if (text.rfind("file://localhost/", 0) == 0) {
    text.erase(0, 16);
    text.insert(text.begin(), '/');
  } else if (text.rfind("file://", 0) == 0) {
    text.erase(0, 7);
  }

  std::string decoded;
  decoded.reserve(text.size());
  auto hex_value = [](char value) -> int {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2 < text.size()) {
      const int high = hex_value(text[i + 1]);
      const int low = hex_value(text[i + 2]);
      if (high >= 0 && low >= 0) {
        decoded.push_back(static_cast<char>((high << 4) | low));
        i += 2;
        continue;
      }
    }
    decoded.push_back(text[i]);
  }
#if defined(_WIN32)
  if (decoded.size() >= 3 && decoded[0] == '/' &&
      std::isalpha(static_cast<unsigned char>(decoded[1])) &&
      decoded[2] == ':') {
    decoded.erase(decoded.begin());
  }
#endif
  return decoded;
}

std::optional<int> ParsePort(const slint::SharedString& text) {
  const std::string value(text);
  int port = 0;
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), port);
  if (error != std::errc{} || end != value.data() + value.size() || port <= 0 ||
      port > 65535) {
    return std::nullopt;
  }
  return port;
}

std::string ConnectionStatusText(ConnectionStatus status, int language) {
  switch (status) {
    case ConnectionStatus::Connected:
      return localization::p2p_connected[language];
    case ConnectionStatus::Connecting:
      return localization::p2p_connecting[language];
    case ConnectionStatus::Gathering:
      return localization::p2p_gathering[language];
    case ConnectionStatus::Disconnected:
      return localization::p2p_disconnected[language];
    case ConnectionStatus::NoSuchTransmissionId:
      return localization::no_such_id[language];
    case ConnectionStatus::IncorrectPassword:
      return localization::reinput_password[language];
    case ConnectionStatus::Closed:
      return localization::p2p_closed[language];
    case ConnectionStatus::RemoteUnavailable:
      return localization::device_offline[language];
    case ConnectionStatus::Failed:
    default:
      return localization::p2p_failed[language];
  }
}

std::string FormatBitrate(uint32_t bits_per_second) {
  char text[48];
  if (bits_per_second < 1000) {
    std::snprintf(text, sizeof(text), "%u bps", bits_per_second);
  } else if (bits_per_second < 1000000) {
    std::snprintf(text, sizeof(text), "%u kbps", bits_per_second / 1000);
  } else {
    std::snprintf(text, sizeof(text), "%.1f mbps", bits_per_second / 1000000.0);
  }
  return text;
}

std::string FormatTransferRate(uint32_t bits_per_second) {
  return bits_per_second == 0 ? std::string{} : FormatBitrate(bits_per_second);
}

std::string FormatLossRate(float loss_rate) {
  if (loss_rate < 0.01f) {
    return "0%";
  }
  char text[24];
  std::snprintf(text, sizeof(text), "%.0f%%", loss_rate * 100.0f);
  return text;
}

std::string FormatFileSize(uint64_t bytes) {
  char text[48];
  if (bytes < 1024) {
    std::snprintf(text, sizeof(text), "%llu B",
                  static_cast<unsigned long long>(bytes));
  } else if (bytes < 1024 * 1024) {
    std::snprintf(text, sizeof(text), "%.2f KB", bytes / 1024.0);
  } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
    std::snprintf(text, sizeof(text), "%.2f MB", bytes / (1024.0 * 1024.0));
  } else {
    std::snprintf(text, sizeof(text), "%.2f GB",
                  bytes / (1024.0 * 1024.0 * 1024.0));
  }
  return text;
}

uint32_t DecodeFirstUtf8CodePoint(const std::string& text) {
  if (text.empty()) {
    return 0;
  }
  const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
  if (bytes[0] < 0x80) {
    return bytes[0];
  }
  if ((bytes[0] & 0xE0) == 0xC0 && text.size() >= 2) {
    return ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
  }
  if ((bytes[0] & 0xF0) == 0xE0 && text.size() >= 3) {
    return ((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) |
           (bytes[2] & 0x3F);
  }
  if ((bytes[0] & 0xF8) == 0xF0 && text.size() >= 4) {
    return ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3F) << 12) |
           ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
  }
  return 0;
}

int SlintKeyToWindowsVk(const std::string& text) {
  const uint32_t key = DecodeFirstUtf8CodePoint(text);
  if (key >= 'a' && key <= 'z') {
    return static_cast<int>(key - 'a' + 'A');
  }
  if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) {
    return static_cast<int>(key);
  }
  if (key >= 0xF704 && key <= 0xF71B) {
    return 0x70 + static_cast<int>(key - 0xF704);
  }

  switch (key) {
    case 0x08:
      return 0x08;
    case 0x09:
    case 0x19:
      return 0x09;
    case 0x0A:
      return 0x0D;
    case 0x1B:
      return 0x1B;
    case 0x7F:
      return 0x2E;
    case 0x10:
      return 0xA0;
    case 0x15:
      return 0xA1;
    case 0x11:
      return 0xA2;
    case 0x16:
      return 0xA3;
    case 0x12:
      return 0xA4;
    case 0x13:
      return 0xA5;
    case 0x17:
      return 0x5B;
    case 0x18:
      return 0x5C;
    case 0x14:
      return 0x14;
    case 0x20:
      return 0x20;
    case 0xF700:
      return 0x26;
    case 0xF701:
      return 0x28;
    case 0xF702:
      return 0x25;
    case 0xF703:
      return 0x27;
    case 0xF727:
      return 0x2D;
    case 0xF729:
      return 0x24;
    case 0xF72B:
      return 0x23;
    case 0xF72C:
      return 0x21;
    case 0xF72D:
      return 0x22;
    case 0xF72F:
      return 0x91;
    case 0xF730:
      return 0x13;
    case 0xF731:
      return 0x2C;
    case 0xF734:
      return 0xB2;
    case 0xF735:
      return 0x5D;
    case 0xF748:
      return 0xA6;
    case ';':
    case ':':
      return 0xBA;
    case '\'':
    case '"':
      return 0xDE;
    case '`':
    case '~':
      return 0xC0;
    case ',':
    case '<':
      return 0xBC;
    case '.':
    case '>':
      return 0xBE;
    case '/':
    case '?':
      return 0xBF;
    case '\\':
    case '|':
      return 0xDC;
    case '[':
    case '{':
      return 0xDB;
    case ']':
    case '}':
      return 0xDD;
    case '-':
    case '_':
      return 0xBD;
    case '=':
    case '+':
      return 0xBB;
    case '!':
      return '1';
    case '@':
      return '2';
    case '#':
      return '3';
    case '$':
      return '4';
    case '%':
      return '5';
    case '^':
      return '6';
    case '&':
      return '7';
    case '*':
      return '8';
    case '(':
      return '9';
    case ')':
      return '0';
    default:
      return -1;
  }
}

struct WindowDragState {
  bool active = false;
  bool use_global_pointer = false;
  float pointer_start_x = 0;
  float pointer_start_y = 0;
  float last_local_x = 0;
  float last_local_y = 0;
  float global_to_physical_scale = 1;
  slint::PhysicalPosition window_start;
  slint::PhysicalPosition last_target;
};

bool CanUseGlobalPointerPosition() {
  const char* driver = SDL_GetCurrentVideoDriver();
  if (driver == nullptr) {
    return false;
  }
#if defined(__linux__) && !defined(__APPLE__)
  // Wayland deliberately does not expose global pointer coordinates or allow
  // clients to position top-level windows.
  return std::strcmp(driver, "wayland") != 0;
#else
  return true;
#endif
}

float ServerWindowLogicalWidth(int language) {
  return language == 0 ? 250.0f : language == 1 ? 330.0f : 430.0f;
}

float ServerWindowLogicalHeight(bool collapsed) {
  return collapsed ? 30.0f : 150.0f;
}

constexpr float kStreamWindowLogicalWidth = 1280.0f;
constexpr float kStreamWindowLogicalHeight = 720.0f;
constexpr float kWaylandTitlebarLogicalHeight = 32.0f;

float StreamWindowLogicalHeight(bool custom_titlebar) {
  return kStreamWindowLogicalHeight +
         (custom_titlebar ? kWaylandTitlebarLogicalHeight : 0.0f);
}

SDL_DisplayID DisplayForSlintWindow(const slint::Window& window) {
  const auto position = window.position();
  const auto size = window.size();
#if defined(__APPLE__)
  const float scale = std::max(window.scale_factor(), 0.01f);
  const SDL_Point center{
      static_cast<int>(
          std::lround(position.x / scale + size.width / (2.0f * scale))),
      static_cast<int>(
          std::lround(position.y / scale + size.height / (2.0f * scale)))};
#else
  const SDL_Point center{
      position.x + static_cast<int>(size.width / 2),
      position.y + static_cast<int>(size.height / 2)};
#endif
  const SDL_DisplayID display = SDL_GetDisplayForPoint(&center);
  return display != 0 ? display : SDL_GetPrimaryDisplay();
}

#if !defined(__APPLE__)
window_geometry::PhysicalSize PhysicalWindowSize(const slint::Window& window,
                                                 float logical_width,
                                                 float logical_height) {
  const auto size = window.size();
  if (size.width > 0 && size.height > 0) {
    return {static_cast<int>(size.width), static_cast<int>(size.height)};
  }

  const float scale = std::max(window.scale_factor(), 0.01f);
  return {std::max(1, static_cast<int>(std::lround(logical_width * scale))),
          std::max(1, static_cast<int>(std::lround(logical_height * scale)))};
}

window_geometry::PhysicalRect PhysicalDisplayBounds(const SDL_Rect& bounds) {
  return {bounds.x, bounds.y, bounds.w, bounds.h};
}
#endif

bool PositionWindowAtCenter(slint::Window& window, const slint::Window& anchor,
                            float logical_width, float logical_height) {
  if (!CanUseGlobalPointerPosition()) {
    return false;
  }

  const SDL_DisplayID display = DisplayForSlintWindow(anchor);
  SDL_Rect usable_bounds{};
  if (display == 0 || !SDL_GetDisplayUsableBounds(display, &usable_bounds)) {
    LOG_WARN("Unable to obtain usable display bounds for stream window: {}",
             SDL_GetError());
    return false;
  }

#if defined(__APPLE__)
  const float target_x =
      static_cast<float>(usable_bounds.x) +
      (static_cast<float>(usable_bounds.w) - logical_width) / 2.0f;
  const float target_y =
      static_cast<float>(usable_bounds.y) +
      (static_cast<float>(usable_bounds.h) - logical_height) / 2.0f;
  window.set_position(
      slint::LogicalPosition(slint::Point<float>{target_x, target_y}));
#else
  const auto target = window_geometry::CenteredPosition(
      PhysicalDisplayBounds(usable_bounds),
      PhysicalWindowSize(window, logical_width, logical_height));
  window.set_position(slint::PhysicalPosition(
      slint::Point<int32_t>{target.x, target.y}));
#endif
  return true;
}

bool PositionWindowAtBottomRight(slint::Window& window, float logical_width,
                                 float logical_height) {
  if (!CanUseGlobalPointerPosition()) {
    return false;
  }

  const auto position = window.position();
  const auto size = window.size();
#if defined(__APPLE__)
  const float scale = std::max(window.scale_factor(), 0.01f);
  const SDL_Point center{
      static_cast<int>(
          std::lround(position.x / scale + size.width / (2.0f * scale))),
      static_cast<int>(
          std::lround(position.y / scale + size.height / (2.0f * scale)))};
#else
  const SDL_Point center{
      position.x + static_cast<int>(size.width / 2),
      position.y + static_cast<int>(size.height / 2)};
#endif
  SDL_DisplayID display = SDL_GetDisplayForPoint(&center);
  if (display == 0) {
    display = SDL_GetPrimaryDisplay();
  }

  SDL_Rect usable_bounds{};
  if (display == 0 || !SDL_GetDisplayUsableBounds(display, &usable_bounds)) {
    LOG_WARN("Unable to obtain usable display bounds for server window: {}",
             SDL_GetError());
    return false;
  }

#if defined(__APPLE__)
  const float target_x =
      static_cast<float>(usable_bounds.x) +
      std::max(0.0f, static_cast<float>(usable_bounds.w) - logical_width);
  const float target_y =
      static_cast<float>(usable_bounds.y) +
      std::max(0.0f, static_cast<float>(usable_bounds.h) - logical_height);
  window.set_position(
      slint::LogicalPosition(slint::Point<float>{target_x, target_y}));
#else
  const auto target = window_geometry::BottomRightPosition(
      PhysicalDisplayBounds(usable_bounds),
      PhysicalWindowSize(window, logical_width, logical_height));
  window.set_position(slint::PhysicalPosition(
      slint::Point<int32_t>{target.x, target.y}));
#endif
  return true;
}

#if _WIN32
bool ConfigureWindowsServerWindow(slint::Window& window) {
  const HWND hwnd = window.win32_hwnd();
  if (!hwnd) {
    return false;
  }

  LONG_PTR ex_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
  ex_style |= WS_EX_TOOLWINDOW;
  ex_style &= ~WS_EX_APPWINDOW;
  SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex_style);
  return SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED |
                          SWP_NOACTIVATE) != FALSE;
}
#endif

template <typename WindowHandle>
void DragWindow(WindowHandle& component, int phase, float mouse_x,
                float mouse_y, WindowDragState& state) {
  if (phase == 1) {
#if defined(__APPLE__)
    // Let AppKit run the native move loop. This preserves macOS window
    // snapping, Spaces, multi-display, and high-DPI behavior without manually
    // updating the window position for every pointer event.
    if (!StartNativeWindowDrag()) {
      LOG_WARN("Unable to start native macOS window drag");
    }
    state.active = false;
    return;
#endif
    auto& window = component->window();
    state.active = true;
    state.last_local_x = mouse_x;
    state.last_local_y = mouse_y;
    state.window_start = window.position();
    state.last_target = state.window_start;
    state.use_global_pointer = CanUseGlobalPointerPosition();
    if (state.use_global_pointer) {
      SDL_GetGlobalMouseState(&state.pointer_start_x, &state.pointer_start_y);
#if defined(__APPLE__)
      // Cocoa reports global pointer positions in logical screen points,
      // while Slint/winit accepts physical window positions.
      state.global_to_physical_scale = window.scale_factor();
#else
      state.global_to_physical_scale = 1.0f;
#endif
    }
    return;
  }
  if (phase == 0) {
    state.active = false;
    return;
  }
  if (!state.active || phase != 2) {
    return;
  }

  auto& window = component->window();
  if (state.use_global_pointer) {
    float pointer_x = 0;
    float pointer_y = 0;
    SDL_GetGlobalMouseState(&pointer_x, &pointer_y);
    const int delta_x = static_cast<int>(std::lround(
        (pointer_x - state.pointer_start_x) * state.global_to_physical_scale));
    const int delta_y = static_cast<int>(std::lround(
        (pointer_y - state.pointer_start_y) * state.global_to_physical_scale));
    const slint::PhysicalPosition target(slint::Point<int32_t>{
        state.window_start.x + delta_x, state.window_start.y + delta_y});
    if (target.x != state.last_target.x || target.y != state.last_target.y) {
      window.set_position(target);
      state.last_target = target;
    }
    return;
  }

  // Retain the previous behavior for backends without global pointer access.
  const float scale = window.scale_factor();
  const auto position = window.position();
  const int delta_x =
      static_cast<int>(std::lround((mouse_x - state.last_local_x) * scale));
  const int delta_y =
      static_cast<int>(std::lround((mouse_y - state.last_local_y) * scale));
  if (delta_x != 0 || delta_y != 0) {
    window.set_position(slint::PhysicalPosition(
        slint::Point<int32_t>{position.x + delta_x, position.y + delta_y}));
  }
  state.last_local_x = mouse_x;
  state.last_local_y = mouse_y;
}

}  // namespace

struct GuiApplication::SlintUi {
  slint::ComponentHandle<ui::MainWindow> main = ui::MainWindow::create();
  std::shared_ptr<slint::VectorModel<ui::ReleaseNoteBlock>>
      release_note_blocks_model =
          std::make_shared<slint::VectorModel<ui::ReleaseNoteBlock>>();
  std::optional<slint::ComponentHandle<ui::StreamWindow>> stream;
  std::optional<slint::ComponentHandle<ui::ServerWindow>> server;
  std::shared_ptr<slint::VectorModel<ui::RecentConnection>> recent_model =
      std::make_shared<slint::VectorModel<ui::RecentConnection>>();
  std::shared_ptr<slint::VectorModel<ui::StreamTab>> tab_model =
      std::make_shared<slint::VectorModel<ui::StreamTab>>();
  std::shared_ptr<slint::VectorModel<slint::SharedString>> display_model =
      std::make_shared<slint::VectorModel<slint::SharedString>>();
  std::shared_ptr<slint::VectorModel<ui::FileTransferEntry>> transfer_model =
      std::make_shared<slint::VectorModel<ui::FileTransferEntry>>();
  std::shared_ptr<slint::VectorModel<ui::NetworkStatsRow>> stats_model =
      std::make_shared<slint::VectorModel<ui::NetworkStatsRow>>();
  std::shared_ptr<slint::VectorModel<ui::ControllerEntry>> controller_model =
      std::make_shared<slint::VectorModel<ui::ControllerEntry>>();
  std::shared_ptr<slint::VectorModel<slint::SharedString>>
      controller_name_model =
          std::make_shared<slint::VectorModel<slint::SharedString>>();
  std::unique_ptr<SlintVideoPresenter> video_presenter;
  std::vector<std::string> tab_order;
  std::vector<std::string> tab_ids;
  std::string tab_model_signature;
  std::vector<std::string> controller_ids;
  std::string connection_dialog_remote_id;
  bool connection_password_initialized = false;
  bool portable_service_dialog_initialized = false;
  std::string recent_model_signature;
  slint::Timer timer;
  WindowDragState main_drag;
  WindowDragState stream_drag;
  WindowDragState server_drag;
  int main_native_titlebar_attempts = 30;
  int stream_live_resize_configuration_attempts = 0;
  int stream_initial_position_attempts = 0;
  int server_initial_position_attempts = 0;
#if _WIN32
  int server_native_window_attempts = 0;
#endif
  int localized_language = -1;
  std::chrono::steady_clock::time_point last_clipboard_poll{};
  slint::Timer video_timer;
#if _WIN32
  std::unique_ptr<WinTray> tray;
#elif defined(__APPLE__)
  std::unique_ptr<MacTray> tray;
#elif defined(__linux__)
  std::unique_ptr<LinuxTray> tray;
  Display* x11_focus_display = nullptr;
  std::chrono::steady_clock::time_point last_x11_activation_poll{};
#endif

  ~SlintUi() {
#if defined(__linux__) && !defined(__APPLE__)
    if (x11_focus_display) {
      XCloseDisplay(x11_focus_display);
    }
#endif
  }
};

GuiApplication::GuiApplication() = default;
GuiApplication::~GuiApplication() = default;

int GuiApplication::Run() {
  path_manager_ = std::make_unique<PathManager>("CrossDesk");
  if (!path_manager_) {
    return -1;
  }
  exec_log_path_ = path_manager_->GetLogPath().string();
  dll_log_path_ = exec_log_path_;
  cache_path_ = path_manager_->GetCachePath().string();
  config_center_ = std::make_unique<ConfigCenter>(cache_path_ + "/config.ini");

  InitializeLogger();
  LOG_INFO("CrossDesk version: {} (Slint UI)", CROSSDESK_VERSION);

  strncpy(signal_server_ip_self_, config_center_->GetSignalServerHost().c_str(),
          sizeof(signal_server_ip_self_) - 1);
  const int signal_port = config_center_->GetSignalServerPort();
  if (signal_port > 0) {
    std::snprintf(signal_server_port_self_, sizeof(signal_server_port_self_),
                  "%d", signal_port);
  }

  latest_version_info_ = CheckUpdate();
  if (!latest_version_info_.empty()) {
    std::string version;
    if (latest_version_info_.contains("latest_version") &&
        latest_version_info_["latest_version"].is_string()) {
      version = latest_version_info_["latest_version"].get<std::string>();
    } else if (latest_version_info_.contains("version") &&
               latest_version_info_["version"].is_string()) {
      version = latest_version_info_["version"].get<std::string>();
    }
    latest_version_ = version.empty() ? std::string{} : "v" + version;
    if (latest_version_info_.contains("releaseNotes") &&
        latest_version_info_["releaseNotes"].is_string()) {
      release_notes_ = latest_version_info_["releaseNotes"].get<std::string>();
    }
    if (latest_version_info_.contains("releaseName") &&
        latest_version_info_["releaseName"].is_string()) {
      release_name_ = latest_version_info_["releaseName"].get<std::string>();
    }
    if (latest_version_info_.contains("releaseDate") &&
        latest_version_info_["releaseDate"].is_string()) {
      release_date_ = latest_version_info_["releaseDate"].get<std::string>();
    }
    update_available_ =
        !version.empty() && IsNewerVersion(CROSSDESK_VERSION, latest_version_);
  }

  InitializeSettings();
  if (!InitializeSDL()) {
    return -1;
  }
  InitializeModules();
  InitializeUi();

  ui_->timer.start(slint::TimerMode::Repeated, 16ms, [this] { Tick(); });
  ScheduleNextVideoFrame();
  // The native tray icon is managed outside Slint, so Slint cannot count it
  // when deciding whether the last hidden window should stop the event loop.
  // Keep the loop alive until the tray's explicit Exit action requests quit.
  ui_->main->show();
  slint::run_event_loop(slint::EventLoopMode::RunUntilQuit);
  Cleanup();
  return 0;
}

void GuiApplication::InitializeLogger() { InitLogger(exec_log_path_); }

void GuiApplication::InitializeSettings() {
  settings_.Load();
  settings_.LoadRecentConnectionAliases();
  localization_language_index_ =
      localization::detail::ClampLanguageIndex(language_button_value_);
  language_button_value_ = localization_language_index_;
  localization_language_ =
      static_cast<ConfigCenter::LANGUAGE>(localization_language_index_);
}

bool GuiApplication::InitializeSDL() {
#if defined(__linux__) && !defined(__APPLE__)
  // Standard Wayland top-level windows cannot choose an absolute screen
  // position. Use the session's XWayland compatibility server for the GUI so
  // auxiliary windows such as the controlled-side status window can be
  // placed deterministically. Capture and input still detect the surrounding
  // Wayland session through XDG_SESSION_TYPE and continue to use the portal.
  const char* requested_video_driver = std::getenv("SDL_VIDEODRIVER");
  const bool video_driver_allows_x11 =
      requested_video_driver == nullptr || requested_video_driver[0] == '\0' ||
      std::strcmp(requested_video_driver, "x11") == 0;
  use_xwayland_gui_ = IsWaylandSession() &&
                      HasNonEmptyEnvironmentVariable("DISPLAY") &&
                      video_driver_allows_x11;
  if (use_xwayland_gui_) {
    setenv("SDL_VIDEODRIVER", "x11", 1);
    LOG_INFO(
        "Wayland session detected; using XWayland for positioned GUI "
        "windows while retaining Wayland portal capture and input");
  }
#endif
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    LOG_ERROR("SDL initialization failed: {}", SDL_GetError());
    return false;
  }
#if defined(__linux__) && !defined(__APPLE__)
  const char* active_video_driver = SDL_GetCurrentVideoDriver();
  // X11 permits the manual move/position operations used by the frameless
  // main window. Keep native Wayland decorations as a compatibility fallback.
  use_x11_custom_titlebar_ =
      active_video_driver != nullptr &&
      std::strcmp(active_video_driver, "x11") == 0;
#endif

  if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(0)) {
    screen_width_ = mode->w;
    screen_height_ = mode->h;
  }
  clipboard_.Initialize();
  return true;
}

void GuiApplication::InitializeModules() {
  if (modules_inited_) {
    return;
  }
  // Renderer construction can log device, shader, or pipeline failures.
  // Initialize it after Run() has selected the application's log directory.
  video_renderer_ = CreateVideoRenderer();
  devices_.Initialize();
  CreateConnectionPeer();
  modules_inited_ = true;
}

void GuiApplication::InitializeUi() {
#if defined(__APPLE__)
  // Slint's renderer must be selected before the first component creates the
  // process-wide backend. The macOS package contains Skia (Metal) and the
  // software fallback, but no OpenGL window renderer.
  const char* requested_slint_backend = std::getenv("SLINT_BACKEND");
  if (requested_slint_backend == nullptr ||
      requested_slint_backend[0] == '\0') {
    if (setenv("SLINT_BACKEND", "winit-skia", 1) == 0) {
      LOG_INFO("Using Slint winit-skia renderer with the macOS Metal backend");
    } else {
      LOG_WARN("Unable to select the Slint Metal backend through SLINT_BACKEND");
    }
  } else {
    LOG_INFO("Using requested Slint backend: {}", requested_slint_backend);
  }
#endif
#if defined(__linux__) && !defined(__APPLE__)
  // Winit prefers Wayland whenever WAYLAND_DISPLAY/WAYLAND_SOCKET is present.
  // Hide them only while Slint selects its process-wide window backend, then
  // restore the environment before the rest of the application starts.
  std::optional<ScopedUnsetEnvironmentVariable> wayland_display_guard;
  std::optional<ScopedUnsetEnvironmentVariable> wayland_socket_guard;
  if (use_xwayland_gui_) {
    wayland_display_guard.emplace("WAYLAND_DISPLAY");
    wayland_socket_guard.emplace("WAYLAND_SOCKET");
  }
#endif
  ui_ = std::make_unique<SlintUi>();
  ui_->video_presenter =
      std::make_unique<SlintVideoPresenter>(*video_renderer_);
#if _WIN32
  ui_->main->set_custom_titlebar(true);
#elif defined(__linux__)
  ui_->main->set_custom_titlebar(use_x11_custom_titlebar_);
  ui_->main->set_wayland_titlebar(use_xwayland_gui_);
#endif
  std::vector<ui::ReleaseNoteBlock> release_note_blocks;
  for (const auto& parsed : ParseReleaseNotesMarkdownForSlint(release_notes_)) {
    ui::ReleaseNoteBlock block;
    block.content = parsed.content;
    block.section_gap = parsed.section_gap;
    release_note_blocks.push_back(std::move(block));
  }
  ui_->release_note_blocks_model->set_vector(std::move(release_note_blocks));
  ui_->main->set_release_note_blocks(ui_->release_note_blocks_model);
  RegisterFontAwesome(ui_->main->window());
  ui_->main->set_recent_connections(ui_->recent_model);
  ResetSettingsUi();
  BindMainCallbacks();
  InitializeSystemTray();
  UpdateLocalization();
  SyncMainWindow();
}

void GuiApplication::InitializeSystemTray() {
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
  if (!ui_) {
    return;
  }
  auto show_window = [this] {
    if (!ui_) {
      return;
    }
    ui_->main->show();
    ui_->main->window().set_minimized(false);
#if defined(__APPLE__)
    // A window restored from the tray may only obtain its NSWindow after the
    // callback returns, so let the regular UI tick configure it.
    ui_->main_native_titlebar_attempts = 30;
#endif
  };
  auto hide_window = [this] {
    if (ui_) {
      ui_->main->hide();
    }
  };
  auto open_settings = [this] {
    if (!ui_) {
      return;
    }
    ui_->main->show();
    ui_->main->window().set_minimized(false);
    ui_->main->set_settings_open(true);
#if defined(__APPLE__)
    ui_->main_native_titlebar_attempts = 30;
#endif
  };
  auto exit_app = [this] {
    exit_ = true;
    slint::quit_event_loop();
  };
#if _WIN32
  ui_->tray = std::make_unique<WinTray>(
      std::move(show_window), std::move(hide_window),
      std::move(open_settings), std::move(exit_app), LoadSlintTrayIcon(),
      L"CrossDesk", localization_language_index_);
#elif defined(__APPLE__)
  ui_->tray = std::make_unique<MacTray>(
      std::move(show_window), std::move(hide_window), std::move(open_settings),
      std::move(exit_app), "CrossDesk", localization_language_index_);
#elif defined(__linux__)
  ui_->tray = std::make_unique<LinuxTray>(
      std::move(show_window), std::move(hide_window),
      std::move(open_settings), std::move(exit_app), "CrossDesk",
      localization_language_index_);
#endif
#endif
}

bool GuiApplication::MinimizeMainWindowToTray() {
  if (!ui_) {
    return false;
  }
#if _WIN32 || defined(__APPLE__)
  if (!ui_->tray) {
    return false;
  }
  ui_->tray->MinimizeToTray();
  return true;
#elif defined(__linux__)
  return ui_->tray && ui_->tray->MinimizeToTray();
#else
  return false;
#endif
}

void GuiApplication::ResetSettingsUi() {
  if (!ui_) {
    return;
  }
  ui_->main->set_language_index(language_button_value_);
  ui_->main->set_video_quality_index(video_quality_button_value_);
  ui_->main->set_frame_rate_index(video_frame_rate_button_value_);
  ui_->main->set_codec_index(video_encode_format_button_value_);
  ui_->main->set_hardware_codec_enabled(enable_hardware_video_codec_);
  ui_->main->set_turn_enabled(enable_turn_);
  ui_->main->set_srtp_enabled(enable_srtp_);
  ui_->main->set_self_hosted_enabled(enable_self_hosted_);
  ui_->main->set_autostart_enabled(enable_autostart_);
  ui_->main->set_daemon_enabled(enable_daemon_);
  ui_->main->set_file_save_path(file_transfer_save_path_buf_);
  ui_->main->set_server_host(signal_server_ip_self_);
  ui_->main->set_server_port(signal_server_port_self_);
  ui_->main->set_coturn_port(coturn_server_port_self_);
}

void GuiApplication::BindMainCallbacks() {
  auto& main = ui_->main;
  main->window().on_close_requested([this] {
    // Keep the application and all active sessions alive. The tray helper
    // ensures that supported platforms still provide a way to restore the
    // main window; Slint hides it regardless of tray availability.
    MinimizeMainWindowToTray();
    return slint::CloseRequestResponse::HideWindow;
  });
  main->on_main_title_drag([this](int phase, float x, float y) {
    if (ui_) {
      DragWindow(ui_->main, phase, x, y, ui_->main_drag);
    }
  });
  main->on_minimize_main_window([this] {
    if (ui_) {
      ui_->main->window().set_minimized(true);
    }
  });
  main->on_close_main_window([this] {
    if (!ui_) {
      return;
    }
    MinimizeMainWindowToTray();
    ui_->main->hide();
  });
  main->on_copy_local_id([this] {
    if (!SDL_SetClipboardText(client_id_)) {
      LOG_WARN("Copy local id failed: {}", SDL_GetError());
    }
  });
  main->on_toggle_password_visibility([this] {
    show_password_ = !show_password_;
    ui_->main->set_password_visible(show_password_);
  });
  main->on_reset_password([this](slint::SharedString value) {
    const std::string password(value);
    if (password.size() != 6) {
      return false;
    }

    if (!peer_ || !signal_connected_) {
      offline_warning_text_ =
          localization::signal_disconnected[localization_language_index_];
      show_offline_warning_window_ = true;
      return true;
    }

    std::string request_id;
    {
      std::lock_guard<std::mutex> lock(password_change_mutex_);
      if (password_change_pending_) {
        return true;
      }
      request_id =
          CreatePasswordChangeRequestId(++next_password_change_request_id_);
      password_change_pending_ = true;
      password_change_result_ready_ = false;
      password_change_succeeded_ = false;
      password_change_result_uncertain_ = false;
      password_change_requested_at_ = std::chrono::steady_clock::now();
      pending_password_change_request_id_ = request_id;
      pending_local_password_ = password;
      password_change_error_.clear();
    }

    const bool self_hosted = config_center_->IsSelfHosted();
    const std::string server_host =
        self_hosted ? config_center_->GetSignalServerHost()
                    : config_center_->GetDefaultServerHost();
    const int server_port =
        self_hosted ? config_center_->GetSignalServerPort()
                    : config_center_->GetDefaultSignalServerPort();
    const std::string pending_identity =
        std::string(client_id_) + "@" + password;
    if (!settings_.StagePendingPasswordChange(
            pending_identity, self_hosted, server_host, server_port,
            request_id)) {
      std::lock_guard<std::mutex> lock(password_change_mutex_);
      password_change_pending_ = false;
      pending_password_change_request_id_.clear();
      pending_local_password_.clear();
      password_change_error_.clear();
      offline_warning_text_ = localization::failed[localization_language_index_];
      show_offline_warning_window_ = true;
      LOG_ERROR("Could not durably stage password change");
      return true;
    }

    const nlohmann::json request = {{"type", "change_password"},
                                    {"request_id", request_id},
                                    {"new_password", password}};
    const std::string message = request.dump();
    if (SendSignalMessage(peer_, message.data(), message.size()) != 0) {
      {
        std::lock_guard<std::mutex> lock(password_change_mutex_);
        password_change_pending_ = false;
        pending_password_change_request_id_.clear();
        pending_local_password_.clear();
        offline_warning_text_ =
            localization::signal_disconnected[localization_language_index_];
        show_offline_warning_window_ = true;
      }
      if (!settings_.ClearPendingPasswordChange()) {
        LOG_WARN("Could not clear unsent pending password change");
      }
    }
    return true;
  });
  main->on_connect_requested(
      [this](slint::SharedString id) { ConnectFromUi(std::string(id)); });
  main->on_format_remote_id([](slint::SharedString value) {
    return UiText(FormatRemotePeerIdInput(std::string_view(value)));
  });
  main->on_recent_connect(
      [this](slint::SharedString id) { ConnectFromUi(std::string(id)); });
  main->on_recent_edit_alias(
      [this](slint::SharedString id, slint::SharedString alias) {
        const std::string remote_id(id);
        const std::string trimmed = Trim(std::string(alias));
        if (trimmed.empty()) {
          settings_.EraseRecentConnectionAlias(remote_id);
        } else {
          settings_.SetRecentConnectionAlias(remote_id, trimmed);
        }
        settings_.SaveRecentConnectionAliases();
        reload_recent_connections_ = true;
      });
  main->on_recent_delete([this](slint::SharedString id) {
    const std::string remote_id(id);
    const auto connection = std::find_if(
        recent_connections_.begin(), recent_connections_.end(),
        [&](const auto& entry) { return entry.second.remote_id == remote_id; });
    if (connection != recent_connections_.end()) {
      thumbnail_->DeleteThumbnail(connection->first);
      settings_.EraseRecentConnectionAlias(remote_id);
      settings_.SaveRecentConnectionAliases();
      reload_recent_connections_ = true;
    }
  });
  main->on_browse_save_path([this] {
    const char* folder = tinyfd_selectFolderDialog(
        localization::file_transfer_save_path[localization_language_index_]
            .c_str(),
        file_transfer_save_path_buf_[0] ? file_transfer_save_path_buf_
                                        : nullptr);
    if (folder) {
      ui_->main->set_file_save_path(folder);
    }
  });
  main->on_save_settings([this] { SaveSettingsFromUi(); });
  main->on_cancel_settings([this] { ResetSettingsUi(); });
  main->on_save_self_hosted_settings([this] {
    const std::string host(ui_->main->get_server_host());
    const auto signal_port = ParsePort(ui_->main->get_server_port());
    const auto coturn_port = ParsePort(ui_->main->get_coturn_port());
    if (!host.empty()) {
      config_center_->SetServerHost(host);
      std::memset(signal_server_ip_self_, 0, sizeof(signal_server_ip_self_));
      std::strncpy(signal_server_ip_self_, host.c_str(),
                   sizeof(signal_server_ip_self_) - 1);
      std::memset(signal_server_ip_, 0, sizeof(signal_server_ip_));
      std::strncpy(signal_server_ip_, host.c_str(),
                   sizeof(signal_server_ip_) - 1);
    }
    if (signal_port) {
      config_center_->SetServerPort(*signal_port);
      std::snprintf(signal_server_port_self_, sizeof(signal_server_port_self_),
                    "%d", *signal_port);
      std::snprintf(signal_server_port_, sizeof(signal_server_port_), "%d",
                    *signal_port);
    }
    if (coturn_port) {
      config_center_->SetCoturnServerPort(*coturn_port);
      std::snprintf(coturn_server_port_self_, sizeof(coturn_server_port_self_),
                    "%d", *coturn_port);
      std::snprintf(coturn_server_port_, sizeof(coturn_server_port_), "%d",
                    *coturn_port);
    }
  });
  main->on_cancel_self_hosted_settings([this] {
    const std::string host = config_center_->GetSignalServerHost();
    ui_->main->set_server_host(UiText(host));
    const int signal_port = config_center_->GetSignalServerPort();
    const int coturn_port = config_center_->GetCoturnServerPort();
    ui_->main->set_server_port(signal_port > 0
                                   ? UiText(std::to_string(signal_port))
                                   : slint::SharedString{});
    ui_->main->set_coturn_port(coturn_port > 0
                                   ? UiText(std::to_string(coturn_port))
                                   : slint::SharedString{});
  });
  main->on_open_download([this] { OpenUrl("https://crossdesk.cn"); });
  main->on_connection_cancel([this] {
    const auto props = FindRemoteSession(ui_->connection_dialog_remote_id);
    if (!props) {
      show_connection_status_window_ = false;
      return;
    }
    const ConnectionStatus status = props->connection_status_.load();
    if (status == ConnectionStatus::IncorrectPassword) {
      std::memset(props->remote_password_, 0, sizeof(props->remote_password_));
      password_validating_ = false;
    } else {
      CloseStreamTab(props->remote_id_);
      re_enter_remote_id_ = true;
    }
    show_connection_status_window_ = false;
  });
  main->on_connection_acknowledge([this] {
    const auto props = FindRemoteSession(ui_->connection_dialog_remote_id);
    if (props) {
      const ConnectionStatus status = props->connection_status_.load();
      if (status == ConnectionStatus::NoSuchTransmissionId ||
          status == ConnectionStatus::RemoteUnavailable) {
        CloseStreamTab(props->remote_id_);
        re_enter_remote_id_ = true;
      }
    }
    show_connection_status_window_ = false;
  });
  main->on_connection_submit_password([this](slint::SharedString value,
                                             bool remember) {
    const auto props = FindRemoteSession(ui_->connection_dialog_remote_id);
    const std::string password(value);
    if (!props || password.empty() || password.size() > 6) {
      return;
    }
    std::memset(props->remote_password_, 0, sizeof(props->remote_password_));
    std::memcpy(props->remote_password_, password.data(), password.size());
    props->remember_password_ = remember;
    props->rejoin_ = true;
    password_validating_ = true;
    need_to_rejoin_ = true;
  });
  main->on_request_screen_recording_permission([this] {
#ifdef __APPLE__
    OpenScreenRecordingPreferences();
    mac_screen_recording_permission_requested_ = true;
    RefreshMacPermissionStatus(true);
#endif
  });
  main->on_request_accessibility_permission([this] {
#ifdef __APPLE__
    OpenAccessibilityPreferences();
    mac_accessibility_permission_requested_ = true;
    RefreshMacPermissionStatus(true);
#endif
  });
  main->on_install_portable_service([this] {
#if _WIN32 && CROSSDESK_PORTABLE
    portable_service_do_not_remind_ = false;
    ui_->main->set_portable_service_do_not_remind(false);
    StartPortableWindowsServiceInstall();
#endif
  });
  main->on_cancel_portable_service([this] {
#if _WIN32 && CROSSDESK_PORTABLE
    if (portable_service_install_state_.load(std::memory_order_acquire) ==
        PortableServiceInstallState::installing) {
      return;
    }
    portable_service_do_not_remind_ =
        ui_->main->get_portable_service_do_not_remind();
    if (portable_service_do_not_remind_) {
      portable_service_prompt_suppressed_ = true;
      config_center_->SetPortableServicePromptSuppressed(true);
      show_portable_service_prompt_suppressed_window_ = true;
    }
    show_portable_service_install_window_ = false;
#endif
  });
  main->on_acknowledge_portable_service([this] {
#if _WIN32 && CROSSDESK_PORTABLE
    show_portable_service_install_window_ = false;
    portable_service_installed_ =
        portable_service_install_state_.load(std::memory_order_acquire) ==
        PortableServiceInstallState::succeeded;
    JoinPortableWindowsServiceInstallThread();
#endif
  });
  main->on_acknowledge_portable_service_suppressed([this] {
#if _WIN32 && CROSSDESK_PORTABLE
    show_portable_service_prompt_suppressed_window_ = false;
#endif
  });
}

void GuiApplication::BindStreamCallbacks() {
  if (!ui_->stream) {
    return;
  }
  auto& stream = *ui_->stream;
  auto close_stream_sessions = [this] {
    std::vector<std::string> ids;
    {
      std::shared_lock lock(remote_sessions_mutex_);
      for (const auto& [id, _] : remote_sessions_) {
        ids.push_back(id);
      }
    }
    for (const auto& id : ids) {
      CloseStreamTab(id);
    }
    if (ui_ && ui_->stream) {
      (*ui_->stream)->hide();
    }
  };
  stream->window().on_close_requested([close_stream_sessions] {
    close_stream_sessions();
    return slint::CloseRequestResponse::HideWindow;
  });
  stream->on_stream_title_drag([this](int phase, float x, float y) {
    if (ui_ && ui_->stream) {
      DragWindow(*ui_->stream, phase, x, y, ui_->stream_drag);
    }
  });
  stream->on_minimize_stream_window([this] {
    if (ui_ && ui_->stream) {
      (*ui_->stream)->window().set_minimized(true);
    }
  });
  stream->on_toggle_maximize_stream_window([this] {
    if (!ui_ || !ui_->stream) {
      return;
    }
    auto& stream = *ui_->stream;
    auto& window = stream->window();
    const bool maximize = !window.is_maximized();
    stream->set_window_maximized(maximize);
    window.set_maximized(maximize);
  });
  stream->on_close_stream_window(close_stream_sessions);
  stream->on_select_tab([this](int index) { SelectStreamTab(index); });
  stream->on_reorder_tab([this](int from, float drop_x, float tab_width) {
    ReorderStreamTab(from, drop_x, tab_width);
  });
  stream->on_close_tab(
      [this](slint::SharedString id) { CloseStreamTab(std::string(id)); });
  stream->on_switch_display([this](int index) {
    auto props = SelectedSession();
    if (!props || index < 0 ||
        index >= static_cast<int>(props->display_info_list_.size())) {
      return;
    }
    props->selected_display_ = index;
    RemoteAction action{};
    action.type = ControlType::display_id;
    action.d = index;
    const std::string message = action.to_json();
    SendReliableDataFrame(props->peer_, message.c_str(), message.size(),
                          props->control_data_label_.c_str());
  });
  stream->on_send_shortcut([this](slint::SharedString shortcut) {
    auto props = SelectedSession();
    if (!props || !props->peer_) {
      return;
    }
    RemoteAction action{};
    action.type = ControlType::service_command;
    action.c.flag = std::string(shortcut) == "Ctrl+Alt+Del"
                        ? ServiceCommandFlag::send_sas
                        : ServiceCommandFlag::lock_workstation;
    const std::string message = action.to_json();
    SendReliableDataFrame(props->peer_, message.c_str(), message.size(),
                          props->control_data_label_.c_str());
  });
  stream->on_toggle_mouse_control([this] {
    auto props = SelectedSession();
    if (!props || !props->connection_established_) {
      return;
    }
    props->control_mouse_ = !props->control_mouse_;
    props->enable_mouse_control_ = props->control_mouse_;
    start_keyboard_capturer_ = props->control_mouse_;
    if (!props->control_mouse_) {
      keyboard_.ForceReleasePressedKeys();
    }
  });
  stream->on_toggle_audio([this] {
    auto props = SelectedSession();
    if (!props || !props->connection_established_) {
      return;
    }
    props->audio_capture_button_pressed_ =
        !props->audio_capture_button_pressed_;
    RemoteAction action{};
    action.type = ControlType::audio_capture;
    action.a = props->audio_capture_button_pressed_;
    const std::string message = action.to_json();
    SendReliableDataFrame(props->peer_, message.c_str(), message.size(),
                          props->control_data_label_.c_str());
  });
  stream->on_select_file([this] {
    auto props = SelectedSession();
    if (!props) {
      return;
    }
    transfers_.ProcessSelectedFile(
        OpenFileDialog(localization::select_file[localization_language_index_]),
        props, props->file_label_);
  });
  stream->on_close_file_transfer([this] {
    if (auto props = SelectedSession()) {
      props->file_transfer_.file_transfer_window_visible_ = false;
    }
  });
  stream->on_can_drop_file(
      [](const slint::DataTransfer& data) { return data.has_plain_text(); });
  stream->on_file_dropped([this](const slint::DataTransfer& data) {
    const auto text = data.plain_text();
    auto props = SelectedSession();
    if (!text || !props) {
      return;
    }
    const std::string path = DecodeDroppedPath(std::string(*text));
    if (!path.empty()) {
      transfers_.ProcessSelectedFile(path, props, props->file_label_,
                                     props->remote_id_);
    }
  });
  stream->on_toggle_stats([this] {
    if (auto props = SelectedSession()) {
      props->net_traffic_stats_button_pressed_ =
          !props->net_traffic_stats_button_pressed_;
    }
  });
  stream->on_toggle_fullscreen([this] {
    auto& stream = *ui_->stream;
#if defined(__APPLE__)
    const bool enter_fullscreen = !IsStreamWindowFullscreen();
    // Update Slint's fullscreen layout before AppKit applies the new frame so
    // the first fullscreen draw cannot briefly contain the windowed tab strip.
    stream->set_fullscreen_enabled(enter_fullscreen);
    if (!SetStreamWindowFullscreen(enter_fullscreen)) {
      stream->set_fullscreen_enabled(!enter_fullscreen);
      return;
    }
#else
    auto& window = stream->window();
    const bool enter_fullscreen = !window.is_fullscreen();
#endif
    fullscreen_button_pressed_ = enter_fullscreen;

#if defined(__APPLE__)
    video_frame_dirty_.store(true, std::memory_order_release);
#else
    stream->set_fullscreen_enabled(enter_fullscreen);
    window.request_redraw();
    window.set_fullscreen(enter_fullscreen);
#endif
  });
  stream->on_disconnect([this] {
    if (auto props = SelectedSession()) {
      CloseStreamTab(props->remote_id_);
    }
  });
  stream->on_pointer_input([this](slint::PointerEventButton button,
                                  slint::PointerEventKind kind, float x,
                                  float y) {
    SendPointerInput(static_cast<int>(button), static_cast<int>(kind), x, y);
  });
  stream->on_scroll_input([this](float dx, float dy, float x, float y) {
    SendScrollInput(dx, dy, x, y);
  });
  stream->on_key_input([this](slint::SharedString text, bool pressed,
                              bool control, bool alt, bool shift, bool meta) {
    SendKeyInput(std::string(text), pressed, control, alt, shift, meta);
  });
  stream->on_keyboard_focus_changed(
      [this](bool focused) { SetStreamKeyboardFocus(focused); });
}

void GuiApplication::BindServerCallbacks() {
  if (!ui_->server) {
    return;
  }
  auto& server = *ui_->server;
  server->on_title_drag([this](int phase, float x, float y) {
    auto& drag = ui_->server_drag;
    DragWindow(*ui_->server, phase, x, y, drag);
  });
  server->on_toggle_collapsed([this](bool collapsed) {
    server_window_collapsed_ = collapsed;
    const int language = (*ui_->server)->get_language_index();
    const float width = ServerWindowLogicalWidth(language);
    (*ui_->server)
        ->window()
        .set_size(slint::LogicalSize(
            slint::Size<float>{width, ServerWindowLogicalHeight(collapsed)}));
  });
  server->on_controller_selected([this](int index) {
    if (index < 0 || index >= static_cast<int>(ui_->controller_ids.size())) {
      return;
    }
    selected_server_remote_id_ = ui_->controller_ids[index];
  });
  server->on_select_file([this] {
    if (selected_server_remote_id_.empty()) {
      return;
    }
    transfers_.ProcessSelectedFile(
        OpenFileDialog(localization::select_file[localization_language_index_]),
        nullptr, file_label_, selected_server_remote_id_);
  });
  server->on_disconnect_controller([this] {
    if (peer_ && !selected_server_remote_id_.empty()) {
      LeaveConnection(peer_, selected_server_remote_id_.c_str());
    }
  });
}

void GuiApplication::Tick() {
  if (exit_) {
    slint::quit_event_loop();
    return;
  }
  HandlePasswordChangeResult();
  HandleCredentialRecovery();
  if (!peer_) {
    CreateConnectionPeer();
  }

  SDL_Event event;
#if defined(__APPLE__)
  // Slint's winit backend owns the macOS AppKit event loop. SDL_PollEvent()
  // implicitly pumps Cocoa events, so calling it from this Slint timer would
  // re-enter winit's event handler and abort the process. Only drain events
  // that are already in SDL's queue; clipboard changes are polled below and
  // tray/window actions use direct Slint callbacks on macOS.
  while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                        SDL_EVENT_LAST) > 0) {
#else
  while (SDL_PollEvent(&event)) {
#endif
    if (event.type == SDL_EVENT_QUIT) {
      exit_ = true;
    } else if (event.type == SDL_EVENT_CLIPBOARD_UPDATE) {
      clipboard_.HandleLocalUpdate();
    }
  }
  clipboard_.ApplyPendingRemoteText();
#if defined(__linux__) && !defined(__APPLE__)
  if (ui_->tray) {
    ui_->tray->ProcessEvents();
  }
#endif
  const auto now = std::chrono::steady_clock::now();
  if (now - ui_->last_clipboard_poll >= 250ms) {
    clipboard_.HandleLocalUpdate();
    ui_->last_clipboard_poll = now;
  }

  if (need_to_rejoin_ && now - last_rejoin_check_time_ >= 1s) {
    last_rejoin_check_time_ = now;
    need_to_rejoin_ = false;
    for (const auto& [_, props] : remote_sessions_) {
      if (props && props->rejoin_) {
        ConnectTo(props->remote_id_, props->remote_password_,
                  props->remember_password_);
      }
    }
  }

  UpdateLabels();
  const bool was_reloading_recent_connections = reload_recent_connections_;
  HandleRecentConnections();
  if (was_reloading_recent_connections && !reload_recent_connections_) {
    ui_->recent_model_signature.clear();
  }
  HandleConnectionStatusChange();
  HandlePendingPresenceProbe();
  HandlePresenceProbeTimeout();
  HandleWindowsServiceIntegration();
#if defined(__linux__) && !defined(__APPLE__)
  SyncXWaylandWindowActivation();
#else
  SyncStreamKeyboardFocus();
#endif
  devices_.UpdateInteractions();
  ShareLocalCursorState();

  UpdateLocalization();
  SyncMainWindow();
  SyncConnectionDialog();
  SyncPlatformDialogs();
  SyncStreamWindow();
  SyncServerWindow();
}

void GuiApplication::ShareLocalCursorState() {
  constexpr auto kCursorStateHeartbeatInterval = 500ms;

  bool has_connected_controller = false;
  {
    std::shared_lock lock(connection_status_mutex_);
    has_connected_controller =
        std::any_of(connection_status_.begin(), connection_status_.end(),
                    [](const auto& entry) {
                      return entry.second == ConnectionStatus::Connected;
                    });
  }
  if (!is_server_mode_ || !peer_ || !has_connected_controller) {
    has_shared_cursor_state_ = false;
    last_cursor_state_share_time_ = {};
    return;
  }

  CursorState sampled{};
  if (!cursor_state_provider_.Sample(&sampled)) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const bool changed =
      !has_shared_cursor_state_ ||
      sampled.visible != last_shared_cursor_state_.visible ||
      sampled.shape != last_shared_cursor_state_.shape;
  const bool heartbeat_due =
      last_cursor_state_share_time_.time_since_epoch().count() == 0 ||
      now - last_cursor_state_share_time_ >= kCursorStateHeartbeatInterval;
  if (!changed && !heartbeat_due) {
    return;
  }

  sampled.seq = ++cursor_state_sequence_;
  RemoteAction action{};
  action.type = ControlType::cursor_state;
  action.cs = sampled;
  const std::string message = action.to_json();
  const int result = SendDataFrame(peer_, message.c_str(), message.size(),
                                   mouse_label_.c_str());
  if (result != 0) {
    LOG_WARN("Send cursor state failed, ret={}", result);
    return;
  }

  last_shared_cursor_state_ = sampled;
  has_shared_cursor_state_ = true;
  last_cursor_state_share_time_ = now;
}

void GuiApplication::HandlePasswordChangeResult() {
  bool succeeded = false;
  bool uncertain = false;
  std::string error;

  {
    std::lock_guard<std::mutex> lock(password_change_mutex_);
    if (password_change_pending_ && !password_change_result_ready_ &&
        std::chrono::steady_clock::now() - password_change_requested_at_ >=
            std::chrono::seconds(10)) {
      password_change_result_ready_ = true;
      password_change_succeeded_ = false;
      password_change_result_uncertain_ = true;
      password_change_error_ = "Server did not respond";
    }

    if (!password_change_result_ready_) {
      return;
    }

    succeeded = password_change_succeeded_;
    uncertain = password_change_result_uncertain_;
    error = password_change_error_;
    password_change_pending_ = false;
    password_change_result_ready_ = false;
    password_change_succeeded_ = false;
    password_change_result_uncertain_ = false;
    pending_password_change_request_id_.clear();
    pending_local_password_.clear();
    password_change_error_.clear();
  }

  if (!succeeded) {
    if (uncertain) {
      LOG_WARN("Password change outcome is unknown: {}; re-authenticating",
               error);
    } else {
      LOG_WARN("Password change failed: {}", error);
      if (!settings_.ClearPendingPasswordChange()) {
        LOG_WARN("Could not clear rejected pending password change");
      }
    }
    offline_warning_text_ = localization::failed[localization_language_index_];
    if (!error.empty()) {
      offline_warning_text_ += ": " + error;
    }
    show_offline_warning_window_ = true;
    if (uncertain && peer_) {
      LeaveConnection(peer_, client_id_);
      DestroyPeer(&peer_);
    }
    return;
  }

  if (!settings_.PromotePendingPasswordChange()) {
    // The pending credential was persisted before the request was sent, so it
    // remains sufficient for recovery even when promotion cannot be written.
    LOG_WARN("Password changed on server; pending recovery record retained");
  }

  LOG_INFO("Password changed successfully for [{}]", client_id_);
  if (peer_) {
    LeaveConnection(peer_, client_id_);
    DestroyPeer(&peer_);
  }
}

void GuiApplication::HandleCredentialRecovery() {
  bool retry_active = false;
  bool promote_pending = false;
  bool clear_pending = false;
  {
    std::lock_guard<std::mutex> lock(password_change_mutex_);
    retry_active = credential_recovery_retry_active_;
    promote_pending = credential_recovery_promote_pending_;
    clear_pending = credential_recovery_clear_pending_;
    credential_recovery_retry_active_ = false;
    credential_recovery_promote_pending_ = false;
    credential_recovery_clear_pending_ = false;
    if (retry_active) {
      credential_recovery_attempt_pending_ = false;
    }
  }

  if (retry_active) {
    LOG_INFO("Pending credential was not accepted; retrying active credential");
    if (peer_) {
      DestroyPeer(&peer_);
    }
    return;
  }

  if (promote_pending) {
    if (!settings_.PromotePendingPasswordChange()) {
      LOG_WARN("Recovered credential is active but promotion remains pending");
    } else {
      LOG_INFO("Recovered password change with pending credential");
    }
  } else if (clear_pending) {
    if (!settings_.ClearPendingPasswordChange()) {
      LOG_WARN("Active credential recovered but pending record could not be "
               "cleared");
    } else {
      LOG_INFO("Password change was not committed; retained active credential");
    }
  } else {
    return;
  }

  std::lock_guard<std::mutex> lock(password_change_mutex_);
  credential_recovery_in_progress_ = false;
  credential_recovery_attempt_pending_ = false;
}

void GuiApplication::UpdateLocalization() {
  const int language =
      localization::detail::ClampLanguageIndex(localization_language_index_);
  if (!ui_ || ui_->localized_language == language) {
    return;
  }
  ui_localization::ApplyMainWindowStrings(ui_->main, language);
  ui_localization::ApplyStreamWindowStrings(ui_->main, language);
  if (ui_->stream) {
    ui_localization::ApplyStreamWindowStrings(*ui_->stream, language);
  }
  ui_->localized_language = language;
}

void GuiApplication::SyncMainWindow() {
  if (!ui_) {
    return;
  }
#if defined(__APPLE__)
  if (ui_->main_native_titlebar_attempts > 0) {
    if (HideDisabledMainWindowZoomButton()) {
      ui_->main_native_titlebar_attempts = 0;
    } else {
      --ui_->main_native_titlebar_attempts;
    }
  }
#endif
  if (re_enter_remote_id_) {
    ui_->main->invoke_reset_remote_id();
    re_enter_remote_id_ = false;
  }
  ui_->main->set_local_id(UiText(FormatPeerId(client_id_)));
  ui_->main->set_local_password(password_saved_);
  ui_->main->set_password_visible(show_password_);
  ui_->main->set_signal_connected(signal_connected_);
  ui_->main->set_signal_tls_error(signal_status_ ==
                                  SignalStatus::SignalTlsCertError);
  ui_->main->set_update_available(update_available_);
  ui_->main->set_current_version(CROSSDESK_VERSION);
  ui_->main->set_latest_version(UiText(latest_version_));
  ui_->main->set_release_name(UiText(release_name_));
  ui_->main->set_release_date(UiText(release_date_));
  ui_->main->set_settings_session_active(stream_window_inited_);
#if (((defined(_WIN32) || defined(__linux__)) && !defined(__aarch64__) && \
      !defined(__arm__) && USE_CUDA) ||                                   \
     defined(__APPLE__))
  ui_->main->set_hardware_codec_available(true);
#else
  ui_->main->set_hardware_codec_available(false);
#endif

  if (show_offline_warning_window_) {
    ui_->main->set_offline_warning(UiText(offline_warning_text_));
    show_offline_warning_window_ = false;
  }

  std::ostringstream signature_builder;
  for (const auto& [key, connection] : recent_connections_) {
    signature_builder << key << '\0' << connection.remote_id << '\0'
                      << connection.remote_host_name << '\0'
                      << settings_.RecentConnectionDisplayName(connection)
                      << '\0'
                      << device_presence_cache_.IsOnline(connection.remote_id)
                      << '\n';
  }
  const std::string signature = signature_builder.str();
  if (ui_->recent_model_signature == signature) {
    return;
  }
  ui_->recent_model_signature = signature;

  std::vector<ui::RecentConnection> model;
  model.reserve(recent_connections_.size());
  for (const auto& [_, connection] : recent_connections_) {
    ui::RecentConnection item;
    item.remote_id = UiText(connection.remote_id);
    item.host_name = UiText(connection.remote_host_name);
    item.display_name =
        UiText(settings_.RecentConnectionDisplayName(connection));
    item.online = device_presence_cache_.IsOnline(connection.remote_id);
    std::vector<unsigned char> rgba;
    int image_width = 0;
    int image_height = 0;
    if (!connection.image_path.empty() &&
        thumbnail_->DecodeImage(connection.image_path, &rgba, &image_width,
                                &image_height)) {
      slint::SharedPixelBuffer<slint::Rgba8Pixel> pixels(image_width,
                                                         image_height);
      std::memcpy(pixels.begin(), rgba.data(), rgba.size());
      item.thumbnail = slint::Image(std::move(pixels));
    }
    model.push_back(std::move(item));
  }
  // LoadThumbnail already orders connections by last update time. Keep that
  // order within each presence group while placing online devices first.
  std::stable_partition(
      model.begin(), model.end(),
      [](const ui::RecentConnection& connection) { return connection.online; });
  ui_->recent_model->set_vector(std::move(model));
}

void GuiApplication::SyncConnectionDialog() {
  if (!ui_ || !show_connection_status_window_) {
    if (ui_) {
      ui_->main->set_connection_dialog_open(false);
      ui_->connection_password_initialized = false;
    }
    return;
  }

  auto props = FindRemoteSession(focused_remote_id_);
  if (!props) {
    ui_->main->set_connection_dialog_open(false);
    return;
  }

  if (ui_->connection_dialog_remote_id != props->remote_id_) {
    ui_->connection_password_initialized = false;
  }
  ui_->connection_dialog_remote_id = props->remote_id_;
  const ConnectionStatus status = props->connection_status_.load();
  const bool password_required = status == ConnectionStatus::IncorrectPassword;
  const bool pending = status == ConnectionStatus::Connecting ||
                       status == ConnectionStatus::Gathering;
  std::string text;
  if (password_required) {
    text = password_validating_
               ? localization::validate_password[localization_language_index_]
           : password_validating_time_ <= 1
               ? localization::input_password[localization_language_index_]
               : localization::reinput_password[localization_language_index_];
  } else {
    text = ConnectionStatusText(status, localization_language_index_);
  }

  ui_->main->set_connection_dialog_open(true);
  ui_->main->set_connection_status_text(UiText(text));
  ui_->main->set_connection_pending(pending);
  ui_->main->set_connection_password_required(password_required);
  ui_->main->set_connection_validating(password_validating_);
  if (password_required && !password_validating_ &&
      !ui_->connection_password_initialized) {
    ui_->main->set_connection_password(props->remote_password_);
    ui_->main->set_connection_remember_password(props->remember_password_);
    ui_->connection_password_initialized = true;
  } else if (!password_required) {
    ui_->connection_password_initialized = false;
  }
}

void GuiApplication::SyncPlatformDialogs() {
#if _WIN32 && CROSSDESK_PORTABLE
  CheckPortableWindowsService();
  const PortableServiceInstallState state =
      portable_service_install_state_.load(std::memory_order_acquire);
  if (state == PortableServiceInstallState::succeeded) {
    portable_service_installed_ = true;
  }
  ui_->main->set_portable_service_settings_visible(true);
  ui_->main->set_portable_service_dialog_open(
      show_portable_service_install_window_);
  ui_->main->set_portable_service_suppressed_notice_open(
      show_portable_service_prompt_suppressed_window_);
  ui_->main->set_portable_service_installed(portable_service_installed_);
  ui_->main->set_portable_service_installing(
      state == PortableServiceInstallState::installing);
  ui_->main->set_portable_service_succeeded(
      state == PortableServiceInstallState::succeeded);
  const std::string* status = nullptr;
  if (state == PortableServiceInstallState::installing) {
    status =
        &localization::installing_windows_service[localization_language_index_];
  } else if (state == PortableServiceInstallState::succeeded) {
    status = &localization::windows_service_install_success
                 [localization_language_index_];
  } else if (state == PortableServiceInstallState::failed) {
    status = &localization::windows_service_install_failed
                 [localization_language_index_];
  }
  ui_->main->set_portable_service_status(status ? UiText(*status)
                                                : slint::SharedString{});
  if (show_portable_service_install_window_) {
    if (!ui_->portable_service_dialog_initialized) {
      ui_->main->set_portable_service_do_not_remind(
          portable_service_do_not_remind_);
      ui_->portable_service_dialog_initialized = true;
    }
  } else {
    ui_->portable_service_dialog_initialized = false;
  }
#else
  ui_->main->set_portable_service_settings_visible(false);
  ui_->main->set_portable_service_dialog_open(false);
  ui_->main->set_portable_service_suppressed_notice_open(false);
#endif
#ifdef __APPLE__
  RefreshMacPermissionStatus(false);
  show_request_permission_window_ = !mac_screen_recording_permission_granted_ ||
                                    !mac_accessibility_permission_granted_;
  ui_->main->set_permission_dialog_open(show_request_permission_window_);
  ui_->main->set_screen_recording_granted(
      mac_screen_recording_permission_granted_);
  ui_->main->set_accessibility_granted(mac_accessibility_permission_granted_);
#else
  ui_->main->set_permission_dialog_open(false);
#endif
}

#if defined(__linux__) && !defined(__APPLE__)
void GuiApplication::SyncXWaylandWindowActivation() {
  if (!ui_ || !use_xwayland_gui_) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now - ui_->last_x11_activation_poll < 100ms) {
    return;
  }
  ui_->last_x11_activation_poll = now;

  if (!ui_->x11_focus_display) {
    ui_->x11_focus_display = XOpenDisplay(nullptr);
  }
  Display* display = ui_->x11_focus_display;
  const ::Window active_window = X11GetActiveWindow(display);
  if (!active_window) {
    SetStreamKeyboardFocus(false);
    return;
  }

  if (const auto process_id = X11GetWindowProcessId(display, active_window);
      process_id && *process_id != static_cast<unsigned long>(getpid())) {
    ui_->main->set_window_active(false);
    if (ui_->stream) {
      (*ui_->stream)->set_window_active(false);
    }
    SetStreamKeyboardFocus(false);
    return;
  }

  ui_->main->set_window_active(
      X11WindowMatches(display, active_window, ui_->main->window()));
  if (ui_->stream) {
    const bool stream_active =
        X11WindowMatches(display, active_window, (*ui_->stream)->window());
    (*ui_->stream)->set_window_active(stream_active);
    SetStreamKeyboardFocus(stream_active);
  } else {
    SetStreamKeyboardFocus(false);
  }
}
#endif

void GuiApplication::SetStreamKeyboardFocus(bool focused) {
  if (focus_on_stream_window_ == focused) {
    return;
  }

  focus_on_stream_window_ = focused;
  if (!focused) {
    keyboard_.ForceReleasePressedKeys();
  }
}

void GuiApplication::SyncStreamKeyboardFocus() {
  if (!ui_ || !ui_->stream || !(*ui_->stream)->window().is_visible()) {
    SetStreamKeyboardFocus(false);
    return;
  }

#if _WIN32
  const HWND stream_hwnd = (*ui_->stream)->window().win32_hwnd();
  SetStreamKeyboardFocus(stream_hwnd != nullptr &&
                         GetForegroundWindow() == stream_hwnd);
#elif defined(__APPLE__)
  SetStreamKeyboardFocus(IsStreamWindowActive());
#endif
}

void GuiApplication::SyncStreamWindow() {
  bool has_sessions = false;
  {
    std::shared_lock lock(remote_sessions_mutex_);
    has_sessions = !remote_sessions_.empty();
  }
  if (need_to_create_stream_window_ && !ui_->stream) {
    ui_->stream.emplace(ui::StreamWindow::create());
#if defined(__linux__)
    (*ui_->stream)->set_custom_titlebar(use_xwayland_gui_);
#endif
    RegisterFontAwesome((*ui_->stream)->window());
    ConfigureStreamVideoRenderer();
    // Slint globals belong to a component tree. Force the next localization
    // pass to initialize the newly-created, independent stream window tree.
    ui_->localized_language = -1;
    (*ui_->stream)->set_tabs(ui_->tab_model);
    (*ui_->stream)->set_displays(ui_->display_model);
    (*ui_->stream)->set_file_transfers(ui_->transfer_model);
    (*ui_->stream)->set_stats_rows(ui_->stats_model);
    BindStreamCallbacks();
    (*ui_->stream)->show();
    // Repeat after the next layout pass because the backend resolves the
    // initial DPI and preferred size asynchronously on some platforms.
    ui_->stream_initial_position_attempts = 2;
#if defined(__APPLE__)
    ui_->stream_live_resize_configuration_attempts = 30;
#endif
    stream_window_created_ = true;
    stream_window_inited_ = true;
    need_to_create_stream_window_ = false;
  }
  if (!ui_->stream) {
    return;
  }
  (*ui_->stream)->set_window_maximized((*ui_->stream)->window().is_maximized());
  (*ui_->stream)->set_srtp_enabled(enable_srtp_);
#if defined(__APPLE__)
  if (ui_->stream_live_resize_configuration_attempts > 0) {
    if (ConfigureStreamWindowLiveResize(
            (*ui_->stream)->window().appkit_view())) {
      ui_->stream_live_resize_configuration_attempts = 0;
    } else {
      --ui_->stream_live_resize_configuration_attempts;
    }
  }
#endif
  if (ui_->video_presenter && ui_->video_presenter->EnsureAttached()) {
    video_frame_dirty_.store(true, std::memory_order_release);
  }
  if (ui_->stream_initial_position_attempts > 0) {
    if (PositionWindowAtCenter(
            (*ui_->stream)->window(), ui_->main->window(),
            kStreamWindowLogicalWidth,
            StreamWindowLogicalHeight((*ui_->stream)->get_custom_titlebar()))) {
      --ui_->stream_initial_position_attempts;
    } else {
      ui_->stream_initial_position_attempts = 0;
    }
  }
  if (!has_sessions) {
    SetStreamKeyboardFocus(false);
#if defined(__APPLE__)
    void* stream_view = (*ui_->stream)->window().appkit_view();
    SetStreamWindowFullscreen(false);
    UnregisterStreamWindow(stream_view);
#endif
    if (ui_->video_presenter) {
      ui_->video_presenter->Detach();
    }
    (*ui_->stream)->hide();
    ui_->stream.reset();
    stream_window_created_ = false;
    stream_window_inited_ = false;
    ui_->stream_initial_position_attempts = 0;
    ui_->stream_live_resize_configuration_attempts = 0;
    focused_remote_id_.clear();
    controlled_remote_id_.clear();
    ui_->tab_order.clear();
    ui_->tab_ids.clear();
    ui_->tab_model_signature.clear();
    return;
  }

  std::vector<ui::StreamTab> tabs;
  std::unordered_map<std::string, ui::StreamTab> tabs_by_id;
  {
    std::shared_lock lock(remote_sessions_mutex_);
    tabs_by_id.reserve(remote_sessions_.size());
    for (const auto& [id, props] : remote_sessions_) {
      if (!props || !props->tab_opened_) {
        continue;
      }
      ui::StreamTab tab;
      tab.remote_id = UiText(id);
      tab.title = UiText(
          props->remote_host_name_.empty() ? id : props->remote_host_name_);
      tab.connected =
          props->connection_status_.load() == ConnectionStatus::Connected;
      tabs_by_id.emplace(id, std::move(tab));
    }
  }
  std::erase_if(ui_->tab_order, [&tabs_by_id](const std::string& id) {
    return !tabs_by_id.contains(id);
  });
  for (const auto& [id, _] : tabs_by_id) {
    if (std::find(ui_->tab_order.begin(), ui_->tab_order.end(), id) ==
        ui_->tab_order.end()) {
      ui_->tab_order.push_back(id);
    }
  }
  ui_->tab_ids = ui_->tab_order;
  tabs.reserve(ui_->tab_ids.size());
  std::ostringstream tab_signature_builder;
  for (const auto& id : ui_->tab_ids) {
    if (auto found = tabs_by_id.find(id); found != tabs_by_id.end()) {
      tab_signature_builder << id << '\0' << std::string(found->second.title)
                            << '\0' << found->second.connected << '\n';
      tabs.push_back(std::move(found->second));
    }
  }
  const std::string tab_signature = tab_signature_builder.str();
  if (ui_->tab_model_signature != tab_signature) {
    ui_->tab_model_signature = tab_signature;
    ui_->tab_model->set_vector(std::move(tabs));
#if defined(__APPLE__)
    // Adding or removing a tab changes the native video's top inset.
    video_frame_dirty_.store(true, std::memory_order_release);
#endif
  }

  int selected_index = 0;
  if (focused_remote_id_.empty() && !ui_->tab_ids.empty()) {
    focused_remote_id_ = ui_->tab_ids.front();
  }
  for (int i = 0; i < static_cast<int>(ui_->tab_ids.size()); ++i) {
    if (ui_->tab_ids[i] == focused_remote_id_) {
      selected_index = i;
      break;
    }
  }
  (*ui_->stream)->set_selected_tab(selected_index);
  SelectStreamTab(selected_index);

  auto props = SelectedSession();
  if (!props) {
    return;
  }
  const ConnectionStatus status = props->connection_status_.load();
  const bool waiting_for_frame =
      status == ConnectionStatus::Connected &&
      !(*ui_->stream)->get_has_frame();
  // Once the peer is connected, the frame-waiting hint replaces the
  // connection status. Keeping these states mutually exclusive also avoids a
  // transient overlap when both properties are synchronized in one tick.
  (*ui_->stream)
      ->set_status_text(UiText(
          status == ConnectionStatus::Connected
              ? std::string{}
              : ConnectionStatusText(status, localization_language_index_)));
  (*ui_->stream)
      ->set_receiving_text(UiText(
          waiting_for_frame
              ? localization::receiving_screen[localization_language_index_]
              : std::string{}));
  (*ui_->stream)->set_mouse_control_enabled(props->control_mouse_);
  int remote_cursor_shape =
      static_cast<int>(CursorShape::default_cursor);
  bool remote_cursor_active = false;
  {
    std::lock_guard lock(props->remote_cursor_state_mutex_);
    remote_cursor_active =
        status == ConnectionStatus::Connected && props->control_mouse_ &&
        props->remote_cursor_state_received_;
    if (remote_cursor_active) {
      remote_cursor_shape = static_cast<int>(
          props->remote_cursor_state_.visible
              ? props->remote_cursor_state_.shape
              : CursorShape::none);
    }
  }
  (*ui_->stream)->set_remote_cursor_active(remote_cursor_active);
  (*ui_->stream)->set_remote_cursor_shape(remote_cursor_shape);
  (*ui_->stream)->set_audio_enabled(props->audio_capture_button_pressed_);
#if defined(__APPLE__)
  fullscreen_button_pressed_ = IsStreamWindowFullscreen();
#else
  fullscreen_button_pressed_ = (*ui_->stream)->window().is_fullscreen();
#endif
  (*ui_->stream)->set_fullscreen_enabled(fullscreen_button_pressed_);
  (*ui_->stream)->set_stats_visible(props->net_traffic_stats_button_pressed_);

  std::vector<slint::SharedString> displays;
  displays.reserve(props->display_info_list_.size());
  for (size_t index = 0; index < props->display_info_list_.size(); ++index) {
    displays.emplace_back(UiText(localization::FormatDisplayLabel(
        index, props->display_info_list_[index].name,
        localization_language_index_)));
  }
  ui_->display_model->set_vector(std::move(displays));
  (*ui_->stream)->set_selected_display(props->selected_display_);

  const auto& net = props->net_traffic_stats_;
  std::vector<ui::NetworkStatsRow> stats_rows;
  stats_rows.reserve(4);
  const auto append_stats_row = [&](const std::string& label,
                                    const auto& inbound, const auto& outbound) {
    ui::NetworkStatsRow row;
    row.label = UiText(label);
    row.inbound = UiText(FormatBitrate(inbound.bitrate));
    row.outbound = UiText(FormatBitrate(outbound.bitrate));
    row.loss_rate = UiText(FormatLossRate(inbound.loss_rate));
    stats_rows.push_back(std::move(row));
  };
  append_stats_row(localization::video[localization_language_index_],
                   net.video_inbound_stats, net.video_outbound_stats);
  append_stats_row(localization::audio[localization_language_index_],
                   net.audio_inbound_stats, net.audio_outbound_stats);
  append_stats_row(localization::data[localization_language_index_],
                   net.data_inbound_stats, net.data_outbound_stats);
  append_stats_row(localization::total[localization_language_index_],
                   net.total_inbound_stats, net.total_outbound_stats);
  ui_->stats_model->set_vector(std::move(stats_rows));
  (*ui_->stream)
      ->set_stats_resolution(UiText(std::to_string(props->video_width_) + "x" +
                                    std::to_string(props->video_height_)));
  (*ui_->stream)
      ->set_stats_connection_mode(
          UiText(props->traversal_mode_ == TraversalMode::P2P
                     ? localization::connection_mode_direct
                           [localization_language_index_]
                     : localization::connection_mode_relay
                           [localization_language_index_]));

  std::vector<RemoteSession::FileTransferInfo> file_list;
  {
    std::lock_guard lock(props->file_transfer_.file_transfer_list_mutex_);
    file_list = props->file_transfer_.file_transfer_list_;
  }
  const auto transfer_priority = [](RemoteSession::FileTransferStatus status) {
    switch (status) {
      case RemoteSession::FileTransferStatus::Sending:
        return 0;
      case RemoteSession::FileTransferStatus::Completed:
        return 1;
      case RemoteSession::FileTransferStatus::Queued:
        return 2;
      case RemoteSession::FileTransferStatus::Failed:
        return 3;
    }
    return 3;
  };
  std::stable_sort(file_list.begin(), file_list.end(),
                   [&](const auto& left, const auto& right) {
                     return transfer_priority(left.status) <
                            transfer_priority(right.status);
                   });

  std::vector<ui::FileTransferEntry> transfers;
  transfers.reserve(file_list.size());
  for (const auto& info : file_list) {
    ui::FileTransferEntry item;
    item.name = UiText(info.file_name);
    const auto status_index = static_cast<int>(info.status);
    if (status_index == 0)
      item.status = UiText(localization::queued[localization_language_index_]);
    if (status_index == 1)
      item.status = UiText(localization::sending[localization_language_index_]);
    if (status_index == 2)
      item.status =
          UiText(localization::completed[localization_language_index_]);
    if (status_index == 3)
      item.status = UiText(localization::failed[localization_language_index_]);
    item.progress = status_index == 2 ? 1.0f
                    : info.file_size == 0
                        ? 0.0f
                        : std::clamp(static_cast<float>(info.sent_bytes) /
                                         static_cast<float>(info.file_size),
                                     0.0f, 1.0f);
    item.speed = UiText(FormatTransferRate(info.rate_bps));
    item.size = UiText(FormatFileSize(info.file_size));
    transfers.push_back(std::move(item));
  }
  ui_->transfer_model->set_vector(std::move(transfers));
  (*ui_->stream)
      ->set_file_transfer_visible(
          props->file_transfer_.file_transfer_window_visible_);

  const auto fps_now = std::chrono::steady_clock::now();
  if (!props->net_traffic_stats_button_pressed_) {
    props->fps_ = 0;
    props->frame_count_ = 0;
    props->last_time_ = {};
  } else if (props->last_time_.time_since_epoch().count() == 0) {
    props->last_time_ = fps_now;
  } else {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             fps_now - props->last_time_)
                             .count();
    if (elapsed >= 1000) {
      props->fps_ = static_cast<int>(props->frame_count_ * 1000 / elapsed);
      props->frame_count_ = 0;
      props->last_time_ = fps_now;
    }
  }
  (*ui_->stream)->set_stats_fps(UiText(std::to_string(props->fps_)));
}

void GuiApplication::SyncStreamVideoFrame() {
  if (!ui_ || !ui_->stream || !ui_->video_presenter) {
    return;
  }
  auto props = SelectedSession();
  if (!props) {
    return;
  }

  SlintVideoPresenter::Frame frame;
  frame.remote_id = props->remote_id_;
  {
    std::lock_guard lock(props->video_frame_mutex_);
    frame.nv12 = props->front_frame_;
    frame.width = props->video_width_;
    frame.height = props->video_height_;
    frame.sequence = props->video_frame_sequence_;
  }

  const auto result = ui_->video_presenter->Present(frame);
  if (result.width > 0 && result.height > 0) {
    (*ui_->stream)
        ->set_stats_resolution(UiText(std::to_string(result.width) + "x" +
                                      std::to_string(result.height)));
  }
  if (result.new_frame) {
    ++props->frame_count_;
  }
}

void GuiApplication::ConfigureStreamVideoRenderer() {
  if (!ui_ || !ui_->stream || !ui_->video_presenter) {
    return;
  }

  ui_->video_presenter->PrepareWindow(
      *ui_->stream,
      [this] {
        SlintVideoPresenter::SurfaceState state;
        state.selected_stream = focused_remote_id_;
        state.fullscreen = fullscreen_button_pressed_;
        if (ui_) {
          state.tab_count = ui_->tab_ids.size();
        }
        return state;
      },
      [this] { video_frame_dirty_.store(true, std::memory_order_release); });
}

void GuiApplication::ScheduleNextVideoFrame() {
  if (!ui_) {
    return;
  }

  constexpr auto frame_interval = std::chrono::nanoseconds(1'000'000'000 / 60);
  const auto now = std::chrono::steady_clock::now();
  if (next_video_frame_time_ == std::chrono::steady_clock::time_point{} ||
      now - next_video_frame_time_ > 250ms) {
    next_video_frame_time_ = now;
  }

  if (now >= next_video_frame_time_) {
    const bool frame_dirty =
        video_frame_dirty_.exchange(false, std::memory_order_acq_rel);
    const bool native_render_needed = ui_->stream && ui_->video_presenter &&
                                      ui_->video_presenter->NeedsRedraw();
    if (frame_dirty || native_render_needed) {
      SyncStreamVideoFrame();
    }
    do {
      next_video_frame_time_ += frame_interval;
    } while (next_video_frame_time_ <= now);
  }

  const auto after_render = std::chrono::steady_clock::now();
  const auto remaining = std::max(next_video_frame_time_ - after_render,
                                  std::chrono::steady_clock::duration::zero());
  const auto delay_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
  const auto delay_ms = std::max<int64_t>(1, (delay_ns + 999'999) / 1'000'000);
  ui_->video_timer.start(slint::TimerMode::SingleShot,
                         std::chrono::milliseconds(delay_ms),
                         [this] { ScheduleNextVideoFrame(); });
}

void GuiApplication::SyncServerWindow() {
  // The connection map is authoritative. Lifecycle flags record callback
  // intent, but callbacks for different controllers can cross each other, so
  // the final decision must reflect the current connected-controller set.
  need_to_create_server_window_.exchange(false, std::memory_order_acq_rel);
  need_to_destroy_server_window_.exchange(false, std::memory_order_acq_rel);
  bool has_connected_controller = false;
  {
    std::shared_lock lock(connection_status_mutex_);
    has_connected_controller =
        std::any_of(connection_status_.begin(), connection_status_.end(),
                    [](const auto& entry) {
                      return entry.second == ConnectionStatus::Connected;
                    });
  }

  if (has_connected_controller && !ui_->server) {
    ui_->server.emplace(ui::ServerWindow::create());
    RegisterFontAwesome((*ui_->server)->window());
    (*ui_->server)->set_controllers(ui_->controller_model);
    (*ui_->server)->set_controller_names(ui_->controller_name_model);
    (*ui_->server)->set_language_index(localization_language_index_);
    server_window_collapsed_ = false;
    (*ui_->server)
        ->window()
        .set_size(slint::LogicalSize(slint::Size<float>{
            ServerWindowLogicalWidth(localization_language_index_),
            ServerWindowLogicalHeight(false)}));
    BindServerCallbacks();
    (*ui_->server)->show();
    // Apply once immediately and once after the next layout pass. This keeps
    // the right and bottom edges correct when the backend resolves DPI and
    // language-dependent preferred width asynchronously on first show.
    ui_->server_initial_position_attempts = 2;
#if _WIN32
    ui_->server_native_window_attempts = 30;
#endif
    server_window_created_ = true;
    server_window_inited_ = true;
  }
  if (!has_connected_controller && ui_->server) {
    (*ui_->server)->hide();
    ui_->server.reset();
    server_window_created_ = false;
    server_window_inited_ = false;
    ui_->server_initial_position_attempts = 0;
#if _WIN32
    ui_->server_native_window_attempts = 0;
#endif
  }
  if (!ui_->server) {
    return;
  }

  std::vector<ui::ControllerEntry> controllers;
  std::vector<slint::SharedString> names;
  ui_->controller_ids.clear();
  {
    std::shared_lock lock(connection_status_mutex_);
    controllers.reserve(connection_status_.size());
    for (const auto& [id, _] : connection_status_) {
      const auto host = connection_host_names_.find(id);
      const std::string name =
          host != connection_host_names_.end() && !host->second.empty()
              ? host->second
              : id;
      ui::ControllerEntry entry;
      entry.remote_id = UiText(id);
      entry.display_name = UiText(name);
      controllers.push_back(entry);
      names.emplace_back(UiText(name));
      ui_->controller_ids.push_back(id);
    }
  }
  ui_->controller_model->set_vector(std::move(controllers));
  ui_->controller_name_model->set_vector(std::move(names));
  const int selected = server_window_state::ReconcileSelectedController(
      ui_->controller_ids, &selected_server_remote_id_);
  (*ui_->server)->set_selected_controller(selected);
  (*ui_->server)->set_language_index(localization_language_index_);
#if _WIN32
  if (ui_->server_native_window_attempts > 0) {
    if (ConfigureWindowsServerWindow((*ui_->server)->window())) {
      ui_->server_native_window_attempts = 0;
    } else {
      --ui_->server_native_window_attempts;
    }
  }
#endif
  if (ui_->server_initial_position_attempts > 0) {
    const float server_width =
        ServerWindowLogicalWidth(localization_language_index_);
    const float server_height =
        ServerWindowLogicalHeight(server_window_collapsed_);
    (*ui_->server)
        ->window()
        .set_size(slint::LogicalSize(
            slint::Size<float>{server_width, server_height}));
    if (PositionWindowAtBottomRight((*ui_->server)->window(), server_width,
                                    server_height)) {
      --ui_->server_initial_position_attempts;
    } else {
      ui_->server_initial_position_attempts = 0;
    }
  }
  (*ui_->server)
      ->set_controller_label(
          UiText(localization::controller[localization_language_index_]));
  (*ui_->server)
      ->set_connection_label(UiText(
          localization::connection_status[localization_language_index_]));
  (*ui_->server)
      ->set_file_transfer_label(
          UiText(localization::file_transfer[localization_language_index_]));
  (*ui_->server)
      ->set_select_file_label(
          UiText(localization::select_file[localization_language_index_]));

  ConnectionStatus status = ConnectionStatus::Closed;
  {
    std::shared_lock lock(connection_status_mutex_);
    if (const auto found = connection_status_.find(selected_server_remote_id_);
        found != connection_status_.end()) {
      status = found->second;
    }
  }
  (*ui_->server)
      ->set_connection_status(
          UiText(ConnectionStatusText(status, localization_language_index_)));

  auto& transfer = transfers_.global_state();
  const auto sent = transfer.file_sent_bytes_.load();
  const auto total = transfer.file_total_bytes_.load();
  (*ui_->server)
      ->set_file_transfer_visible(transfer.file_transfer_window_visible_);
  (*ui_->server)->set_sending_file(transfer.file_sending_.load());
  (*ui_->server)
      ->set_file_progress(total == 0 ? 0.0f
                                     : std::clamp(static_cast<float>(sent) /
                                                      static_cast<float>(total),
                                                  0.0f, 1.0f));
  (*ui_->server)
      ->set_file_progress_text(
          UiText(FormatTransferRate(transfer.file_send_rate_bps_.load())));
  std::string current_file_name;
  const uint32_t current_file_id = transfer.current_file_id_.load();
  if (current_file_id != 0) {
    std::lock_guard lock(transfer.file_transfer_list_mutex_);
    for (const auto& info : transfer.file_transfer_list_) {
      if (info.file_id == current_file_id) {
        current_file_name = info.file_name;
        break;
      }
    }
  }
  (*ui_->server)->set_current_file_name(UiText(current_file_name));
  (*ui_->server)
      ->set_file_size_text(total == 0 ? slint::SharedString{}
                                      : UiText(FormatFileSize(sent) + " / " +
                                               FormatFileSize(total)));
}

void GuiApplication::SaveSettingsFromUi() {
  auto& main = ui_->main;
  language_button_value_ =
      localization::detail::ClampLanguageIndex(main->get_language_index());
  video_quality_button_value_ =
      std::clamp(main->get_video_quality_index(), 0, 2);
  video_frame_rate_button_value_ =
      std::clamp(main->get_frame_rate_index(), 0, 1);
  video_encode_format_button_value_ = std::clamp(main->get_codec_index(), 0, 1);
  enable_hardware_video_codec_ = main->get_hardware_codec_enabled();
  enable_turn_ = main->get_turn_enabled();
  enable_srtp_ = main->get_srtp_enabled();
  enable_self_hosted_ = main->get_self_hosted_enabled();
  enable_autostart_ = main->get_autostart_enabled();
  enable_daemon_ = main->get_daemon_enabled();

  localization_language_ =
      static_cast<ConfigCenter::LANGUAGE>(language_button_value_);
  localization_language_index_ = language_button_value_;
  config_center_->SetLanguage(localization_language_);
  config_center_->SetVideoQuality(
      static_cast<ConfigCenter::VIDEO_QUALITY>(video_quality_button_value_));
  config_center_->SetVideoFrameRate(static_cast<ConfigCenter::VIDEO_FRAME_RATE>(
      video_frame_rate_button_value_));
  config_center_->SetVideoEncodeFormat(
      static_cast<ConfigCenter::VIDEO_ENCODE_FORMAT>(
          video_encode_format_button_value_));
  config_center_->SetHardwareVideoCodec(enable_hardware_video_codec_);
  config_center_->SetTurn(enable_turn_);
  config_center_->SetSrtp(enable_srtp_);
  config_center_->SetSelfHosted(enable_self_hosted_);
  config_center_->SetAutostart(enable_autostart_);
  config_center_->SetDaemon(enable_daemon_);

  const std::string path(main->get_file_save_path());
  std::memset(file_transfer_save_path_buf_, 0,
              sizeof(file_transfer_save_path_buf_));
  std::strncpy(file_transfer_save_path_buf_, path.c_str(),
               sizeof(file_transfer_save_path_buf_) - 1);
  config_center_->SetFileTransferSavePath(path);

  const std::string host(main->get_server_host());
  if (!host.empty()) {
    config_center_->SetServerHost(host);
  }
  if (const auto port = ParsePort(main->get_server_port())) {
    config_center_->SetServerPort(*port);
  }
  if (const auto port = ParsePort(main->get_coturn_port())) {
    config_center_->SetCoturnServerPort(*port);
  }

  language_button_value_last_ = language_button_value_;
  video_quality_button_value_last_ = video_quality_button_value_;
  video_frame_rate_button_value_last_ = video_frame_rate_button_value_;
  video_encode_format_button_value_last_ = video_encode_format_button_value_;
  enable_hardware_video_codec_last_ = enable_hardware_video_codec_;
  enable_turn_last_ = enable_turn_;
  enable_srtp_last_ = enable_srtp_;
  enable_self_hosted_last_ = enable_self_hosted_;
  enable_autostart_last_ = enable_autostart_;
  enable_daemon_last_ = enable_daemon_;
  file_transfer_save_path_last_ = path;
  ui_->localized_language = -1;

  if (!stream_window_inited_) {
    CloseAllRemoteSessions();
    CreateConnectionPeer();
  }
}

void GuiApplication::ConnectFromUi(const std::string& input) {
  const std::string remote_id = CompactPeerId(input);
  if (remote_id.empty()) {
    return;
  }
  if (auto existing = FindRemoteSession(remote_id);
      existing && existing->connection_established_) {
    focused_remote_id_ = remote_id;
    need_to_create_stream_window_ = true;
    return;
  }

  for (const auto& [_, connection] : recent_connections_) {
    if (connection.remote_id == remote_id) {
      ConnectTo(remote_id, connection.password.c_str(),
                connection.remember_password);
      return;
    }
  }
  ConnectTo(remote_id, "", false);
}

void GuiApplication::SelectStreamTab(int index) {
  if (!ui_ || index < 0 || index >= static_cast<int>(ui_->tab_ids.size())) {
    return;
  }
  const std::string selected_remote_id = ui_->tab_ids[index];
  const bool selection_changed = focused_remote_id_ != selected_remote_id;
  bool renderer_selection_changed = false;
  if (!controlled_remote_id_.empty() &&
      controlled_remote_id_ != selected_remote_id) {
    keyboard_.ForceReleasePressedKeys();
  }
  focused_remote_id_ = selected_remote_id;
  controlled_remote_id_ = selected_remote_id;
  if (ui_->video_presenter) {
    renderer_selection_changed =
        ui_->video_presenter->SelectStream(selected_remote_id);
  }
  if (renderer_selection_changed) {
    video_frame_dirty_.store(true, std::memory_order_release);
  }
  if (selection_changed && ui_->stream) {
    (*ui_->stream)->set_has_frame(false);
  }
  std::shared_ptr<RemoteSession> selected_session;
  {
    std::shared_lock lock(remote_sessions_mutex_);
    for (auto& [id, props] : remote_sessions_) {
      if (props) {
        props->tab_selected_ = id == focused_remote_id_;
      }
    }
    const auto selected = remote_sessions_.find(selected_remote_id);
    if (selected != remote_sessions_.end()) {
      selected_session = selected->second;
    }
    start_keyboard_capturer_ = selected_session &&
                               selected_session->control_mouse_ &&
                               selected_session->connection_status_.load() ==
                                   ConnectionStatus::Connected;
  }
}

void GuiApplication::ReorderStreamTab(int from, float drop_x, float tab_width) {
  if (!ui_ || from < 0 || from >= static_cast<int>(ui_->tab_order.size()) ||
      tab_width <= 0.0f) {
    return;
  }
  const float slot_width = tab_width + 1.0f;
  const int target =
      std::clamp(static_cast<int>(std::floor(drop_x / slot_width)), 0,
                 static_cast<int>(ui_->tab_order.size()) - 1);
  if (target == from) {
    return;
  }
  std::string moved = std::move(ui_->tab_order[from]);
  ui_->tab_order.erase(ui_->tab_order.begin() + from);
  ui_->tab_order.insert(ui_->tab_order.begin() + target, std::move(moved));
  ui_->tab_ids = ui_->tab_order;
  SelectStreamTab(target);
}

void GuiApplication::CloseStreamTab(const std::string& remote_id) {
  std::shared_ptr<RemoteSession> props;
  {
    std::shared_lock lock(remote_sessions_mutex_);
    if (const auto found = remote_sessions_.find(remote_id);
        found != remote_sessions_.end()) {
      props = found->second;
    }
  }
  if (!props) {
    return;
  }
  keyboard_.ReleaseRemotePressedKeys(remote_id, "stream_tab_closed");
  CloseRemoteSession(props);
  WaitForThumbnailSaveTasks();
  reload_recent_connections_ = true;
  recent_connection_image_save_time_ = 0;
  ResetRemoteSessionResources(props);
  {
    std::unique_lock lock(remote_sessions_mutex_);
    remote_sessions_.erase(remote_id);
  }
  if (ui_->video_presenter) {
    ui_->video_presenter->ForgetStream(remote_id);
  }
  if (focused_remote_id_ == remote_id) {
    focused_remote_id_.clear();
    controlled_remote_id_.clear();
    if (ui_->video_presenter && ui_->video_presenter->SelectStream({})) {
      video_frame_dirty_.store(true, std::memory_order_release);
    }
  }
}

std::shared_ptr<GuiApplication::RemoteSession>
GuiApplication::SelectedSession() {
  std::shared_lock lock(remote_sessions_mutex_);
  auto found = remote_sessions_.find(focused_remote_id_);
  if (found != remote_sessions_.end()) {
    return found->second;
  }
  for (const auto& [_, props] : remote_sessions_) {
    if (props && props->tab_selected_) {
      return props;
    }
  }
  return nullptr;
}

void GuiApplication::SendPointerInput(int button, int kind, float x, float y) {
  auto props = SelectedSession();
  if (!props || !props->peer_ || !props->control_mouse_ || !ui_->stream) {
    return;
  }

  const auto size = (*ui_->stream)->window().size();
  const float scale = (*ui_->stream)->window().scale_factor();
  const float available_width = size.width / scale;
  const float titlebar_height =
      !fullscreen_button_pressed_ && (*ui_->stream)->get_custom_titlebar()
          ? kWaylandTitlebarLogicalHeight
          : 0.0f;
  const float available_height =
      size.height / scale - titlebar_height -
      (fullscreen_button_pressed_ ? 0.0f
                                  : (ui_->tab_ids.size() > 1 ? 30.0f : 0.0f));
  float render_width = available_width;
  float render_height = available_height;
  float offset_x = 0;
  float offset_y = 0;
  if (props->video_width_ > 0 && props->video_height_ > 0 &&
      available_width > 0 && available_height > 0) {
    const float video_aspect =
        static_cast<float>(props->video_width_) / props->video_height_;
    const float area_aspect = available_width / available_height;
    if (video_aspect > area_aspect) {
      render_height = available_width / video_aspect;
      offset_y = (available_height - render_height) * 0.5f;
    } else {
      render_width = available_height * video_aspect;
      offset_x = (available_width - render_width) * 0.5f;
    }
  }
  if (x < offset_x || x > offset_x + render_width || y < offset_y ||
      y > offset_y + render_height) {
    return;
  }

  RemoteAction action{};
  action.type = ControlType::mouse;
  action.m.x = std::clamp((x - offset_x) / render_width, 0.0f, 1.0f);
  action.m.y = std::clamp((y - offset_y) / render_height, 0.0f, 1.0f);
  // Slint enum order: Cancel=0, Down=1, Up=2, Move=3 and
  // Other=0, Left=1, Right=2, Middle=3.
  if (kind == 3) {
    action.m.flag = MouseFlag::move;
  } else if (kind == 1) {
    if (button == 1)
      action.m.flag = MouseFlag::left_down;
    else if (button == 2)
      action.m.flag = MouseFlag::right_down;
    else if (button == 3)
      action.m.flag = MouseFlag::middle_down;
    else
      return;
  } else if (kind == 2) {
    if (button == 1)
      action.m.flag = MouseFlag::left_up;
    else if (button == 2)
      action.m.flag = MouseFlag::right_up;
    else if (button == 3)
      action.m.flag = MouseFlag::middle_up;
    else
      return;
  } else {
    return;
  }

  controlled_remote_id_ = props->remote_id_;
  const std::string message = action.to_json();
  SendDataFrame(props->peer_, message.c_str(), message.size(),
                props->mouse_label_.c_str());
}

void GuiApplication::SendScrollInput(float delta_x, float delta_y, float x,
                                     float y) {
  auto props = SelectedSession();
  if (!props || !props->peer_ || !props->control_mouse_ || !ui_->stream) {
    return;
  }
  const auto size = (*ui_->stream)->window().size();
  const float scale = (*ui_->stream)->window().scale_factor();
  const float width = size.width / scale;
  const float titlebar_height =
      !fullscreen_button_pressed_ && (*ui_->stream)->get_custom_titlebar()
          ? kWaylandTitlebarLogicalHeight
          : 0.0f;
  const float height =
      size.height / scale - titlebar_height -
      (fullscreen_button_pressed_ ? 0.0f
                                  : (ui_->tab_ids.size() > 1 ? 30.0f : 0.0f));
  if (width <= 0 || height <= 0) {
    return;
  }
  RemoteAction action{};
  action.type = ControlType::mouse;
  action.m.x = std::clamp(x / width, 0.0f, 1.0f);
  action.m.y = std::clamp(y / height, 0.0f, 1.0f);
  if (std::abs(delta_y) >= std::abs(delta_x)) {
    action.m.flag = MouseFlag::wheel_vertical;
    action.m.s = delta_y > 0 ? 1 : delta_y < 0 ? -1 : 0;
  } else {
    action.m.flag = MouseFlag::wheel_horizontal;
    action.m.s = delta_x > 0 ? 1 : delta_x < 0 ? -1 : 0;
  }
  if (action.m.s == 0) {
    return;
  }
  controlled_remote_id_ = props->remote_id_;
  const std::string message = action.to_json();
  SendDataFrame(props->peer_, message.c_str(), message.size(),
                props->mouse_label_.c_str());
}

void GuiApplication::SendKeyInput(const std::string& text, bool pressed,
                                  bool control, bool alt, bool shift,
                                  bool meta) {
  (void)control;
  (void)alt;
  (void)shift;
  (void)meta;
  auto props = SelectedSession();
  if (!props || !props->peer_ || !props->control_mouse_ ||
      props->connection_status_.load() != ConnectionStatus::Connected) {
    return;
  }

  controlled_remote_id_ = props->remote_id_;
  focused_remote_id_ = props->remote_id_;

  // Native capture forwards the event independently. Slint keyboard events
  // are used only by platforms whose native capture backend is unavailable.
  if (keyboard_capturer_is_started_ && !keyboard_capturer_uses_window_events_) {
    return;
  }

  const int virtual_key = SlintKeyToWindowsVk(text);
  if (virtual_key >= 0) {
    keyboard_.SendKeyCommand(virtual_key, pressed);
  }
}

std::string GuiApplication::OpenFileDialog(const std::string& title) {
#if defined(__APPLE__)
  return OpenNativeFileDialog(title);
#else
  const char* path =
      tinyfd_openFileDialog(title.c_str(), "", 0, nullptr, nullptr, 0);
  return path ? path : "";
#endif
}

bool GuiApplication::OpenUrl(const std::string& url) {
  return SDL_OpenURL(url.c_str());
}

void GuiApplication::Cleanup() {
  if (!ui_) {
    SDL_Quit();
    return;
  }
  ui_->timer.stop();
  ui_->video_timer.stop();
#if _WIN32 && CROSSDESK_PORTABLE
  JoinPortableWindowsServiceInstallThread();
#endif
  clipboard_.Shutdown();
  keyboard_.ForceReleasePressedKeys();
  devices_.DestroyDevices();
  devices_.DestroyFactories();
  CloseAllRemoteSessions();
  WaitForThumbnailSaveTasks();
  devices_.DestroyAudioOutput();
  if (ui_->stream) {
#if defined(__APPLE__)
    void* stream_view = (*ui_->stream)->window().appkit_view();
    SetStreamWindowFullscreen(false);
    UnregisterStreamWindow(stream_view);
#endif
    if (ui_->video_presenter) {
      ui_->video_presenter->Detach();
    }
    (*ui_->stream)->hide();
    ui_->stream.reset();
  }
  if (ui_->server) {
    (*ui_->server)->hide();
  }
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
  if (ui_->tray) {
    ui_->tray->RemoveTrayIcon();
    ui_->tray.reset();
  }
#endif
  ui_.reset();
  SDL_Quit();
}

}  // namespace crossdesk
