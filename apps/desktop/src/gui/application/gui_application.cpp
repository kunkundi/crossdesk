#include "application/gui_application.h"

#include <SDL3/SDL.h>
#include <openssl/rand.h>
#include <remote_action.h>
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
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "crossdesk_ui.h"
#include "fa_solid_900.h"
#include "features/about/about_catalog.h"
#include "features/feedback/diagnostic_export.h"
#include "features/file_transfer/transfer_notification_layout.h"
#include "features/file_transfer/transfer_notification_state.h"
#include "features/file_transfer/file_browser_window.h"
#include "features/gallery/gallery_fullscreen_transition.h"
#include "features/gallery/gallery_window_state.h"
#include "features/gallery/gallery_workspace.h"
#include "features/gallery/preview_bandwidth.h"
#include "features/gallery/preview_resolution.h"
#include "features/input/pointer_motion.h"
#include "features/input/stream_keyboard_focus.h"
#include "filesystem_utf8.h"
#include "localization.h"
#include "platform.h"
#ifndef __OHOS__
#include "platform/instance_activation.h"
#endif
#include "platform/video_renderer.h"
#include "rendering/slint_video_presenter.h"
#include "rendering/video_geometry.h"
#include "rendering/video_frame_diagnostics.h"
#include "runtime/privacy_control_state.h"
#if _WIN32
#include <windows.h>

#include "platform/windows/gui/slint_backend.h"
#include "platform/windows/gui/session_end_observer.h"
#include "platform/windows/gui/tray/win_tray.h"
#include "platform/windows/gui/window_icons.h"
#include "platform/windows/gui/window_taskbar.h"
#include "platform/windows/service/autostart_control.h"
#include "platform/windows/service/interactive_desktop.h"
#include "platform/windows/service/unattended_config.h"
#elif defined(__APPLE__)
#include "platform/macos/gui/tray/mac_tray.h"
#include "platform/macos/gui/video_frame_scheduler.h"
#include "platform/window_drag.h"
#elif defined(__OHOS__)
#include "platform/harmony/harmony_autostart.h"
#include "platform/harmony/gui/tray/harmony_tray.h"
#include "platform/harmony/gui/window_state.h"
#include "tools/file_transfer.h"
#elif (defined(__linux__) && !defined(__OHOS__))
#include <GL/gl.h>
#include <X11/Xatom.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/Xlib.h>
#include <unistd.h>

#include "platform/linux/gui/cuda_graphics.h"
#include "platform/linux/gui/tray/linux_tray.h"
#include "platform/linux/gui/wayland_activation.h"
#include "platform/linux/headless/headless_console.h"
// Xlib defines Status as a macro, which collides with UpdateChecker::Status.
#undef Status
#endif
#include "rd_log.h"
#include "server_window_state.h"
#include "signal_server_settings.h"
#include <minirtc.h>
#include "ui/announcement_text.h"
#include "ui/model_update.h"
#include "ui/release_notes_text.h"
#include "ui/ui_localization.h"
#include "update_checker.h"
#if CROSSDESK_HAS_UPDATER
#include "updater/update_controller.h"
#endif
#include "window_geometry.h"
#include "window_presentation.h"

namespace crossdesk {
namespace {

using namespace std::chrono_literals;

bool SupportsControlHandCursor([[maybe_unused]] const slint::Window& window) {
#if defined(_WIN32)
  return LoadCursorW(nullptr, IDC_HAND) != nullptr;
#elif defined(__linux__) && !defined(__OHOS__)
  // Match winit's pointer aliases, without using any move/grab fallback.
  constexpr const char* names[] = {"pointer", "hand2", "hand1", "hand",
                                  "pointing_hand"};
  if (window.wayland_surface()) {
    const char* theme = std::getenv("XCURSOR_THEME");
    for (const char* name : names) {
      if (auto* images = XcursorLibraryLoadImages(name, theme, 24)) {
        const bool available = images->nimage > 0;
        XcursorImagesDestroy(images);
        if (available) return true;
      }
    }
    return false;
  }
  std::unique_ptr<Display, decltype(&XCloseDisplay)> display{
      XOpenDisplay(nullptr), XCloseDisplay};
  if (!display) return false;
  for (const char* name : names) {
    const Cursor cursor = XcursorLibraryLoadCursor(display.get(), name);
    if (cursor != None) {
      XFreeCursor(display.get(), cursor);
      return true;
    }
  }
  return false;
#else
  // AppKit supplies pointingHandCursor; other adapters resolve pointer.
  return true;
#endif
}

std::optional<std::string> GenerateRandomPassword(std::string_view current) {
  constexpr std::string_view alphabet =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  constexpr auto unbiased_limit = 256 - 256 % alphabet.size();
  std::string password;
  do {
    password.clear();
    while (password.size() < 6) {
      unsigned char value;
      if (RAND_bytes(&value, sizeof(value)) != 1) {
        return std::nullopt;
      }
      // Reject the incomplete bucket so every character is equally likely.
      if (value < unbiased_limit) {
        password.push_back(alphabet[value % alphabet.size()]);
      }
    }
  } while (password == current ||
           password.find_first_not_of("0123456789") == std::string::npos);
  return password;
}

std::string CreatePasswordChangeRequestId(uint64_t sequence) {
  const auto timestamp = std::chrono::system_clock::now()
                             .time_since_epoch()
                             .count();
  std::random_device random;
  return std::to_string(timestamp) + "-" + std::to_string(random()) + "-" +
         std::to_string(sequence);
}

#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
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

std::string X11WindowTitle(Display* display, ::Window window) {
  if (!display || !window) return {};
  unsigned char* name = nullptr;
  unsigned long length = 0, remaining = 0;
  Atom type = None;
  int format = 0;
  XGetWindowProperty(display, window, XInternAtom(display, "_NET_WM_NAME", False),
                     0, 1024, False, XInternAtom(display, "UTF8_STRING", False),
                     &type, &format, &length, &remaining, &name);
  const std::string title = name && format == 8
      ? std::string(reinterpret_cast<char*>(name), length) : std::string{};
  if (name) XFree(name);
  return title;
}

bool X11ActivateWindow(Display* display, std::string_view title) {
  if (!display) return false;
  Atom type = None;
  int format = 0;
  unsigned long count = 0, remaining = 0;
  unsigned char* data = nullptr;
  const auto root = DefaultRootWindow(display);
  if (XGetWindowProperty(display, root, XInternAtom(display, "_NET_CLIENT_LIST", False),
                         0, 4096, False, XA_WINDOW, &type, &format, &count,
                         &remaining, &data) != Success || !data) {
    if (data) XFree(data);
    return false;
  }
  bool activated = false;
  if (type == XA_WINDOW && format == 32) {
    const auto* windows = reinterpret_cast<const ::Window*>(data);
    for (unsigned long i = 0; i < count; ++i) {
      if (X11GetWindowProcessId(display, windows[i]) !=
          static_cast<unsigned long>(getpid())) continue;
      if (X11WindowTitle(display, windows[i]) != title) continue;
      XEvent event{};
      event.xclient.type = ClientMessage;
      event.xclient.window = windows[i];
      event.xclient.message_type = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
      event.xclient.format = 32;
      event.xclient.data.l[0] = 1;
      event.xclient.data.l[1] = CurrentTime;
      activated = XSendEvent(display, root, False,
                             SubstructureRedirectMask | SubstructureNotifyMask,
                             &event) != 0;
      XFlush(display);
      break;
    }
  }
  XFree(data);
  return activated;
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

slint::SharedString TrustFingerprint(std::string fingerprint) {
  for (size_t i = 8; i < fingerprint.size(); i += 9) fingerprint.insert(i, " ");
  return UiText(fingerprint);
}

ui::TrustedDeviceEntry TrustRow(const TrustedDevices::Device& device,
                               const std::string& remote = {}) {
  auto id = device.device_id;
  if (id.rfind("C-", 0) == 0) id.erase(0, 2);
  return {UiText(remote), UiText(device.public_key),
      UiText(device.name.empty() ? id : device.name), UiText(id), {}};
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
  std::string digits = gallery::Workspace::NormalizeId(std::string(input));
  digits.erase(std::remove_if(digits.begin(), digits.end(),
                              [](unsigned char c) {
                                return c < '0' || c > '9';
                              }),
               digits.end());
  if (digits.size() > 9) {
    digits.resize(9);
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
  return gallery::Workspace::NormalizeId(id);
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

std::string ConnectionStatusText(MiniRtcConnectionStatus status, int language) {
  switch (status) {
    case MiniRtcConnectionStatus::MiniRtcConnectionConnected:
      return localization::p2p_connected[language];
    case MiniRtcConnectionStatus::MiniRtcConnectionConnecting:
      return localization::p2p_connecting[language];
    case MiniRtcConnectionStatus::MiniRtcConnectionGathering:
      return localization::p2p_gathering[language];
    case MiniRtcConnectionStatus::MiniRtcConnectionDisconnected:
      return localization::p2p_disconnected[language];
    case MiniRtcConnectionStatus::MiniRtcConnectionNoSuchTransmissionId:
      return localization::no_such_id[language];
    case MiniRtcConnectionStatus::MiniRtcConnectionIncorrectPassword:
      return localization::reinput_password[language];
    case MiniRtcConnectionStatus::MiniRtcConnectionClosed:
      return localization::p2p_closed[language];
    case MiniRtcConnectionStatus::MiniRtcConnectionRemoteUnavailable:
      return localization::device_offline[language];
    case MiniRtcConnectionStatus::MiniRtcConnectionFailed:
    default:
      return localization::p2p_failed[language];
  }
}

std::string FormatResolution(int width, int height) {
  return std::to_string(width) + " × " + std::to_string(height);
}

std::string FormatBitrate(uint32_t bits_per_second) {
  char text[48];
  if (bits_per_second < 1000) {
    std::snprintf(text, sizeof(text), "%u bps", bits_per_second);
  } else if (bits_per_second < 1000000) {
    std::snprintf(text, sizeof(text), "%u kbps", bits_per_second / 1000);
  } else {
    std::snprintf(text, sizeof(text), "%.1f Mbps", bits_per_second / 1000000.0);
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
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
  std::unique_ptr<Display, decltype(&XCloseDisplay)> x11_display{nullptr,
                                                              XCloseDisplay};
  slint::Timer pointer_timer;
#endif

  bool ReadGlobalPointer(float* x, float* y) {
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver && std::strcmp(driver, "x11") == 0) {
      // Slint can dispatch motion before SDL pumps its XInput2 events, and
      // pointer warps do not emit raw motion at all. Query X11 directly so
      // dragging never depends on SDL's cached global pointer position.
      if (!x11_display) {
        x11_display.reset(XOpenDisplay(nullptr));
      }
      if (!x11_display) {
        return false;
      }
      ::Window root, child;
      int root_x, root_y, local_x, local_y;
      unsigned int buttons;
      if (!XQueryPointer(x11_display.get(), DefaultRootWindow(x11_display.get()),
                         &root, &child, &root_x, &root_y, &local_x, &local_y,
                         &buttons)) {
        return false;
      }
      *x = static_cast<float>(root_x);
      *y = static_cast<float>(root_y);
      return true;
    }
#endif
    SDL_GetGlobalMouseState(x, y);
    return true;
  }
};

bool CanUseGlobalPointerPosition() {
  const char* driver = SDL_GetCurrentVideoDriver();
  if (driver == nullptr) {
    return false;
  }
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
  // Wayland deliberately does not expose global pointer coordinates or allow
  // clients to position top-level windows.
  return std::strcmp(driver, "wayland") != 0;
#else
  return true;
#endif
}

// Place the native cursor in the stream's client area so Slint hit-tests its
// ordinary controls at the same position as the scaled video cursor.
bool PlaceStreamControlPointer(const slint::Window& window, float x, float y) {
#if defined(_WIN32)
  const HWND hwnd = window.win32_hwnd();
  if (!hwnd || GetForegroundWindow() != hwnd) return false;
  RECT client{};
  POINT current{};
  if (!GetClientRect(hwnd, &client) || IsRectEmpty(&client) || !GetCursorPos(&current) ||
      GetAncestor(WindowFromPoint(current), GA_ROOT) != hwnd ||
      !ScreenToClient(hwnd, &current) || !PtInRect(&client, current)) return false;
  const float scale = window.scale_factor();
  POINT target{std::clamp(std::lround(x * scale), client.left, client.right - 1),
               std::clamp(std::lround(y * scale), client.top, client.bottom - 1)};
  return ClientToScreen(hwnd, &target) && GetAncestor(WindowFromPoint(target), GA_ROOT) == hwnd &&
         SetCursorPos(target.x, target.y) && GetCursorPos(&current) &&
         current.x == target.x && current.y == target.y;
#elif defined(__APPLE__)
  const float scale = window.scale_factor();
  return PlaceMacStreamControlPointer(window.appkit_view(), x * scale, y * scale);
#elif defined(__linux__) && !defined(__OHOS__)
  if (window.wayland_surface()) return false;
  std::unique_ptr<Display, decltype(&XCloseDisplay)> display(XOpenDisplay(nullptr), XCloseDisplay);
  if (!display) return false;
  const auto active = X11GetActiveWindow(display.get());
  if (!X11WindowMatches(display.get(), active, window) ||
      X11GetWindowProcessId(display.get(), active) != static_cast<unsigned long>(getpid()))
    return false;
  const auto size = window.size();
  if (!size.width || !size.height) return false;
  const float scale = window.scale_factor();
  const int px = std::clamp<long>(std::lround(x * scale), 0, size.width - 1);
  const int py = std::clamp<long>(std::lround(y * scale), 0, size.height - 1);
  // Specifying the source window also limits the warp to its visible area.
  XWarpPointer(display.get(), active, active, 0, 0, 0, 0, px, py);
  ::Window root, child;
  int root_x, root_y, client_x, client_y;
  unsigned int buttons;
  return XQueryPointer(display.get(), active, &root, &child, &root_x, &root_y,
                       &client_x, &client_y, &buttons) && client_x == px && client_y == py;
#else
  return false;
#endif
}

constexpr int kWaylandTitlebarLogicalHeight = 32;

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
      std::max(0.0f, (usable_bounds.w - logical_width) / 2.0f);
  const float target_y =
      static_cast<float>(usable_bounds.y) +
      std::max(0.0f, (usable_bounds.h - logical_height) / 2.0f);
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
    state.use_global_pointer =
        CanUseGlobalPointerPosition() &&
        state.ReadGlobalPointer(&state.pointer_start_x, &state.pointer_start_y);
    if (state.use_global_pointer) {
#if defined(__APPLE__)
      // Cocoa reports global pointer positions in logical screen points,
      // while Slint/winit accepts physical window positions.
      state.global_to_physical_scale = window.scale_factor();
#else
      state.global_to_physical_scale = 1.0f;
#endif
    }
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
    if (state.use_global_pointer && state.x11_display) {
      // Moving the window can leave consecutive pointer events at identical
      // local coordinates, so Slint need not emit another TouchArea::moved.
      // Follow the global pointer throughout the drag, including those moves.
      state.pointer_timer.start(
          slint::TimerMode::Repeated, std::chrono::milliseconds(16),
          [weak = slint::ComponentWeakHandle(component), &state] {
            if (auto window = weak.lock()) {
              DragWindow(*window, 2, 0, 0, state);
            } else {
              state.active = false;
              state.pointer_timer.stop();
            }
          });
    }
#endif
    return;
  }
  if (phase == 0) {
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
    state.pointer_timer.stop();
    if (state.use_global_pointer && state.x11_display) {
      DragWindow(component, 2, mouse_x, mouse_y, state);
    }
#endif
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
    if (!state.ReadGlobalPointer(&pointer_x, &pointer_y)) {
      return;
    }
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

struct GuiApplication::StreamView {
  explicit StreamView(std::shared_ptr<RemoteSession> props)
      : session(std::move(props)), renderer(CreateVideoRenderer()),
        presenter(renderer, session->remote_id_) {}

  std::shared_ptr<RemoteSession> session;
  std::shared_ptr<VideoRenderer> renderer;
  SlintVideoPresenter presenter;
  std::shared_ptr<slint::VectorModel<slint::SharedString>> display_model =
      std::make_shared<slint::VectorModel<slint::SharedString>>();
  std::shared_ptr<slint::VectorModel<slint::SharedString>> resolution_model =
      std::make_shared<slint::VectorModel<slint::SharedString>>();
  DisplayResolutionInfo resolution_choices;
  std::shared_ptr<slint::VectorModel<ui::NetworkStatsRow>> stats_model =
      std::make_shared<slint::VectorModel<ui::NetworkStatsRow>>();
  WindowDragState drag;
  std::string endpoint;
  unsigned endpoint_window_number = 1;
  ConfigCenter::StreamWindowSize normal_size{1280, 720};
  int position_attempts = 2;
#if defined(__APPLE__)
  int chrome_attempts = 30;
#endif
  bool was_visible = false;
  slint::ComponentHandle<ui::StreamWindow> window = ui::StreamWindow::create();

  bool Visible() const {
    return window->window().is_visible() && !window->window().is_minimized();
  }

  bool Fullscreen() const {
#if defined(__APPLE__)
    return IsStreamWindowFullscreen(window->window().appkit_view());
#else
    return window->window().is_fullscreen();
#endif
  }

  void ObserveWindowState() {
    const auto& native = window->window();
    const bool fullscreen = Fullscreen();
    const bool maximized = native.is_maximized();
    window->set_fullscreen_enabled(fullscreen);
    window->set_window_maximized(maximized);
    if (position_attempts > 0 || !Visible() || fullscreen || maximized) return;
    const auto size = native.size();
    const float scale = native.scale_factor();
    if (!(scale > 0)) return;
    // Save logical client dimensions so moving between displays with different
    // backing scales does not change the apparent size on the next connection.
    const int width = std::lround(size.width / scale);
    const int height = std::lround(size.height / scale);
    if (width >= 640 && height >= 360) normal_size = {width, height};
  }

  void SaveSize(ConfigCenter& config) {
    if (position_attempts > 0) return;  // The window closed before its first show.
    ObserveWindowState();
    if (config.SetStreamWindowSize(session->device_preference_key_, normal_size) != 0)
      LOG_WARN("Could not save control window size");
  }

  ~StreamView() {
    session->SetVideoRenderer(nullptr);
    presenter.Detach();
    window->hide();
  }
};

struct GuiApplication::SlintUi {
#if _WIN32
  SessionEndObserver session_end;
#endif
  gallery::Workspace gallery;
  std::filesystem::path gallery_path;
  std::vector<std::string> gallery_ids;
  std::set<std::string> gallery_hidden_active_ids;
  struct GroupEditor {
    std::string id;
    std::shared_ptr<slint::VectorModel<ui::DeviceGroupOption>> options =
        std::make_shared<slint::VectorModel<ui::DeviceGroupOption>>();
  };
  GroupEditor group_editors[2];
  // Snapshot while the import picker is open; never includes credentials.
  std::shared_ptr<slint::VectorModel<ui::GalleryRecentDevice>> gallery_recent_model =
      std::make_shared<slint::VectorModel<ui::GalleryRecentDevice>>();
  std::string gallery_focus, gallery_auth_id, gallery_requested_auth,
      gallery_control_id;
  std::chrono::steady_clock::time_point gallery_synced{};
  std::chrono::steady_clock::time_point gallery_network_synced{};
  std::shared_ptr<slint::VectorModel<ui::GalleryNetworkDevice>> gallery_network_model =
      std::make_shared<slint::VectorModel<ui::GalleryNetworkDevice>>();
  gallery::WindowState connection_window, gallery_window;
  gallery::FullscreenTransition gallery_transition;
  int restore_ticks[2] = {};
  float loaded_window_scale[2] = {};
  bool gallery_shown = false;
  std::optional<slint::PhysicalSize> observed_gallery_size;
  struct Preview {
    std::weak_ptr<RemoteSession> session;
    uint64_t sequence = 0;
    slint::Image image;
  };
  std::unordered_map<std::string, Preview> gallery_previews;
  struct PendingPreview {
    std::shared_ptr<RemoteSession> props;
    PreviewSubscription demand;
    bool needs_sync = false;
  };
  std::vector<PendingPreview> gallery_pending_previews;
  std::vector<gallery::PreviewBandwidthDemand> gallery_bandwidth_demands;
  gallery::PreviewBandwidthAllocator gallery_bandwidth_allocator;
  struct BatchOperation {
    std::string id, action, file;
    // Only explicit add-device requests carry credentials; never persisted.
    std::optional<std::string> password;
    bool remember_password = false;
    enum class State { Queued, Running, AwaitingPassword, Finished };
    State state = State::Queued;
    uint32_t file_id = 0;
    std::weak_ptr<RemoteSession> session;
    std::chrono::steady_clock::time_point started{};
  };
  // Transient concurrency control only; completed operations are discarded.
  std::vector<BatchOperation> batch_operations;
  std::string saved_window_signature;
  std::chrono::steady_clock::time_point window_saved{};
  std::shared_ptr<slint::VectorModel<ui::GalleryTile>> gallery_model =
      std::make_shared<slint::VectorModel<ui::GalleryTile>>();
  std::shared_ptr<slint::VectorModel<ui::GalleryActionDevice>> gallery_selected_model =
      std::make_shared<slint::VectorModel<ui::GalleryActionDevice>>();
  float pointer_x = 0, pointer_y = 0;
  std::set<int> pressed_mouse_buttons;
  PointerMotion pointer_motion;
  uint64_t announcement_version = UINT64_MAX;
  std::string announcement_selected_id, announcement_selected_revision;
  // Share the window shell and dialogs, but keep each component's UI state and
  // native window independent. Only the wall instance ever uses gallery mode.
  slint::ComponentHandle<ui::MainWindow> main = ui::MainWindow::create();
  slint::ComponentHandle<ui::MainWindow> wall = ui::MainWindow::create();
  auto& Window(bool gallery) { return gallery ? wall : main; }
  std::shared_ptr<slint::VectorModel<ui::ReleaseNoteBlock>>
      release_note_blocks_model =
          std::make_shared<slint::VectorModel<ui::ReleaseNoteBlock>>();
  std::map<std::string, StreamView> streams;
  bool releasing_input = false;
  bool gallery_previous_mouse_control = false;
  std::shared_ptr<slint::VectorModel<ui::RecentConnection>> recent_model =
      std::make_shared<slint::VectorModel<ui::RecentConnection>>();
  struct TransferNotification {
    std::optional<slint::ComponentHandle<ui::FileTransferNotification>> window;
    RemoteSession::FileTransferInfo info;
    transfer_notification::Presentation presentation;
  };
  std::map<uint32_t, TransferNotification> transfer_notifications;
  std::optional<slint::ComponentHandle<ui::ClipboardFileNotification>> clipboard_notification;
  uint64_t clipboard_notification_id = 0;
  SDL_DisplayID clipboard_notification_display = 0;
  std::map<std::string, std::unique_ptr<FileBrowserWindow>> file_browsers;
  SDL_DisplayID transfer_display = 0;
  std::shared_ptr<slint::VectorModel<ui::ControllerEntry>> controller_model =
      std::make_shared<slint::VectorModel<ui::ControllerEntry>>();
  std::vector<std::string> controller_ids;
  std::string trusted_device_signature;
  std::shared_ptr<slint::VectorModel<ui::TrustedDeviceEntry>> trusted_device_model =
      std::make_shared<slint::VectorModel<ui::TrustedDeviceEntry>>();
  std::shared_ptr<slint::VectorModel<ui::TrustedDeviceEntry>> trust_candidate_model =
      std::make_shared<slint::VectorModel<ui::TrustedDeviceEntry>>();
  std::string connection_dialog_remote_id;
  bool connection_password_initialized = false;
  bool connection_dialog_gallery = false;
  bool portable_service_dialog_initialized = false;
  std::string recent_model_signature;
  gallery::PreviewResolutionDebouncer recent_preview_resolution;
  gallery::PreviewResolution recent_preview_size;
  std::unordered_map<std::string, slint::Image> recent_preview_images;
  std::optional<std::string> recent_group_filter;
  std::vector<std::string> recent_filter_groups;
  slint::Timer timer;
  std::shared_ptr<void> file_dialog_lifetime = std::make_shared<int>(0);
  bool file_dialog_open = false;
  std::future<feedback::ExportResult> diagnostic_export;
  std::string diagnostic_saved_path;
  WindowDragState main_drag, gallery_drag;
  int main_native_titlebar_attempts = 30;
  int gallery_native_titlebar_attempts = 0;
  int localized_language = -1;
  std::chrono::steady_clock::time_point last_clipboard_poll{};
  slint::Timer video_timer;
#if defined(__APPLE__)
  MacVideoFrameScheduler video_scheduler;
#endif
#ifndef __OHOS__
  std::unique_ptr<platform::InstanceActivationReceiver> activation_receiver;
#endif
#if _WIN32
  std::unique_ptr<WinTray> tray;
#elif defined(__APPLE__)
  std::unique_ptr<MacTray> tray;
#elif defined(__OHOS__)
  std::unique_ptr<HarmonyTray> tray;
#elif (defined(__linux__) && !defined(__OHOS__))
  std::unique_ptr<LinuxTray> tray;
  Display* x11_focus_display = nullptr;
  int main_activation_attempts = 0;
  std::string main_activation_token;
  std::chrono::steady_clock::time_point last_x11_activation_poll{};
#endif

  ~SlintUi() {
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
    if (x11_focus_display) {
      XCloseDisplay(x11_focus_display);
    }
#endif
  }
};

GuiApplication::GuiApplication() = default;
GuiApplication::~GuiApplication() = default;

int GuiApplication::Run(bool login_launch) {
#ifndef __OHOS__
  // Publish the GUI endpoint before configuration/media initialization, so a
  // second launch can queue activation even while this window is starting.
  auto activation_receiver =
      std::make_unique<platform::InstanceActivationReceiver>();
  std::string activation_error;
  activation_receiver->Start(activation_error);
#endif
  path_manager_ = std::make_unique<PathManager>("CrossDesk");
  if (!path_manager_) {
    return -1;
  }
  exec_log_path_ = path_manager_->GetLogPath().string();
  dll_log_path_ = exec_log_path_;
  InitializeLogger();
#ifndef __OHOS__
  if (!activation_error.empty()) LOG_WARN("{}", activation_error);
#endif
  cache_path_ = path_manager_->GetCachePath().string();
  announcements_.SetStorageDirectory(path_manager_->GetCachePath() /
                                     "announcement_reads");
  config_center_ = std::make_unique<ConfigCenter>(cache_path_ + "/config.ini");
#ifdef _WIN32
  if (SupportsServiceAutostart() && SharedHostIdentity::Configured()) {
    // User-session features attach after unlock; the persistent host keeps
    // its transport, capture and input across this transition.
    if (IsUnattendedEnabled() && !IsCurrentSessionUserDesktopActive()) {
      LOG_INFO("Waiting for the unlocked user desktop before attaching GUI features");
      do {
        Sleep(250);
      } while (IsUnattendedEnabled() && !IsCurrentSessionUserDesktopActive());
    }
    if (!PrepareSharedGuiIdentity(true)) return -1;
  }
#endif
  InitializeTrustedDevices(path_manager_->GetConfigPath() / "trusted-devices");
#ifdef __APPLE__
  StartMacPermissionMigration(false);
#endif

  LOG_INFO("CrossDesk version: {} (Slint UI)", CROSSDESK_VERSION);

  strncpy(signal_server_ip_self_, config_center_->GetSignalServerHost().c_str(),
          sizeof(signal_server_ip_self_) - 1);
  const int signal_port = config_center_->GetSignalServerPort();
  if (signal_port > 0 && signal_port <= 65535) {
    std::snprintf(signal_server_port_self_, sizeof(signal_server_port_self_),
                  "%d", signal_port);
  }

#if defined(__linux__) && !defined(__OHOS__)
  try {
    if (!InitializeSettings()) return -1;
  } catch (const std::system_error&) {
    // Credential I/O errors are logged by SettingsManager. Exit cleanly rather
    // than initializing a new identity or crashing with an uncaught exception.
    return -1;
  }
#else
  if (!InitializeSettings()) return -1;
#endif
#ifdef _WIN32
  // Older credential caches and trust stores are available after settings initialization.
  MigrateServiceAutostart(true);
  // Upgrade an already-enabled standalone host before the GUI opens its peer.
  // Until an identity is attached the GUI can still run on its own identity,
  // e.g. before its first connection has obtained a device ID.
  if (SupportsServiceAutostart() && IsUnattendedEnabled() && !shared_identity_) {
    if (!PrepareSharedGuiIdentity(false))
      LOG_WARN("Could not upgrade the unattended host; boot startup stays off");
    else if (config_center_->SetAutostart(true) != 0)
      return -1;  // Attached to a host that could not be configured.
  }
#endif
#if defined(__linux__) && !defined(__OHOS__)
  HeadlessConsole::Instance().BindSettings(
      config_center_.get(), [this] {
        std::lock_guard<std::mutex> lock(password_change_mutex_);
        return HasActiveSession() || password_change_pending_ || credential_recovery_in_progress_;
      }, [this](SettingEffect effect) {
        ApplySettingsFromConfig(effect == SettingEffect::reconnect);
      });
#endif
#ifdef __APPLE__
  if (login_launch) SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
#endif
  if (!InitializeSDL()) {
    return -1;
  }
#if _WIN32
  const auto backend = ConfigureWindowsSlintBackend();
  if (!backend.success) {
    LOG_ERROR("{}", backend.diagnostic);
    SDL_Quit();
    return -1;
  }
  LOG_INFO("Slint backend: {}; {}", backend.backend, backend.diagnostic);
#endif
  InitializeModules();
  update_checker_ = std::make_unique<UpdateChecker>(CROSSDESK_VERSION);
#if CROSSDESK_HAS_UPDATER
  updater_ = std::make_unique<updater::Controller>(path_manager_->GetCachePath());
#endif
  InitializeUi();
  InitializeLan();
#ifndef __OHOS__
  ui_->activation_receiver = std::move(activation_receiver);
#endif
  // Configure the initial geometry before the native window becomes visible.
  // Subsequent ticks only acknowledge that placement and its resolved DPI.
  RestoreWindowGeometry(true);

  ui_->timer.start(slint::TimerMode::Repeated, 16ms, [this] { Tick(); });
  ScheduleNextVideoFrame();
  // The native tray icon is managed outside Slint, so Slint cannot count it
  // when deciding whether the last hidden window should stop the event loop.
  // Keep the loop alive until the tray's explicit Exit action requests quit.
  if (!login_launch || !MinimizeMainWindowToTray()) {
    ui_->main->show();
#if _WIN32
    ConfigureWindowsWindowIcons(ui_->main->window().win32_hwnd());
#elif defined(__APPLE__)
    ConfigureMainWindowChrome(ui_->main->window().appkit_view());
    PrepareNativeFileDialog();
#endif
  }
  slint::run_event_loop(slint::EventLoopMode::RunUntilQuit);
  Cleanup();
  return 0;
}

void GuiApplication::InitializeLogger() { InitLogger(exec_log_path_); }

bool GuiApplication::InitializeSettings() {
  settings_.Load();
  if (!settings_.UpgradeHistory()) return false;
  settings_.SelectHistoryServer();
  settings_.ApplyPreviewPreference();
  ReloadRecentConnections();
  if (!remote_resolutions_.Load(
          std::filesystem::path(cache_path_) / "remote_display_resolutions.json"))
    LOG_WARN("Could not load remembered remote display resolutions");
  localization_language_index_ =
      localization::detail::ClampLanguageIndex(language_button_value_);
  localization_language_ =
      static_cast<ConfigCenter::LANGUAGE>(language_button_value_);
  SyncPrivacyScreenSettings();
  return true;
}

bool GuiApplication::InitializeSDL() {
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
  ConfigureLinuxCudaGraphics();
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
  // SDL disables the screensaver by default. A background remote desktop app
  // should leave automatic display sleep under the user's system settings.
#ifndef __OHOS__
  SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
#endif
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    LOG_ERROR("SDL initialization failed: {}", SDL_GetError());
    return false;
  }
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
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
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
  // Detect the desktop session before temporarily hiding Wayland variables
  // for the XWayland GUI backend below.
  const bool is_wayland_session = IsWaylandSession();
  // Winit prefers Wayland whenever WAYLAND_DISPLAY/WAYLAND_SOCKET is present.
  // Hide them only while Slint selects its process-wide window backend, then
  // restore the environment before the rest of the application starts.
  std::optional<ScopedUnsetEnvironmentVariable> wayland_display_guard;
  std::optional<ScopedUnsetEnvironmentVariable> wayland_socket_guard;
  if (use_xwayland_gui_) {
    wayland_display_guard.emplace("WAYLAND_DISPLAY");
    wayland_socket_guard.emplace("WAYLAND_SOCKET");
  }
  // Match crossdesk.desktop even when the Debian launcher runs crossdesk-bin.
  // Set this before creating any windows so X11 and Wayland share the same id.
  slint::set_xdg_app_id("crossdesk");
#endif
  ui_ = std::make_unique<SlintUi>();
  for (const auto& window : {ui_->main, ui_->wall})
    window->set_ip_direct_enabled(config_center_->IsIpDirectEnabled());
  ui_->main->set_lan_mode(config_center_->GetConnectionMode() ==
                          ConfigCenter::CONNECTION_MODE::DIRECT_IP);
#if _WIN32
  ui_->session_end.Start();
#endif
  const auto last_presented = config_center_->GetLastPresentedRelease();
  ui_->main->set_release_welcome_pending(
      last_presented.empty() || IsNewerVersion(last_presented, CROSSDESK_VERSION));
#if defined(__APPLE__)
  ui_->video_scheduler.Start([this](double queued_ms) {
    for (auto& [_, view] : ui_->streams)
      if (view.Visible() && view.session->frame_dirty_)
        view.session->video_latency_->RecordFrameWake(queued_ms);
    PresentPendingVideoFrame();
  });
  std::atomic_store_explicit(&video_frame_wakeup_,
      std::make_shared<const std::function<void()>>(
          ui_->video_scheduler.Notifier()), std::memory_order_release);
#elif !defined(__OHOS__)
  // Windows/Linux have no display-link equivalent, so a decoded frame used to
  // wait for the next fallback timer tick (up to a full 60 Hz interval) before
  // it was submitted. Wake the event loop directly and keep the timer only as
  // a safety net.
  std::atomic_store_explicit(&video_frame_wakeup_,
      std::make_shared<const std::function<void()>>(
          [this] { RequestVideoPresent(); }), std::memory_order_release);
#endif
#if _WIN32
  ui_->main->set_custom_titlebar(true);
#elif (defined(__linux__) && !defined(__OHOS__))
  ui_->main->set_custom_titlebar(use_x11_custom_titlebar_);
  ui_->main->set_wayland_titlebar(use_xwayland_gui_);
  // Set this only at startup so dismissing the notice lasts until exit.
  ui_->main->set_wayland_notice_open(is_wayland_session);
#endif
  ui_->main->set_release_note_blocks(ui_->release_note_blocks_model);
  RegisterFontAwesome(ui_->main->window());
  ui_->main->set_recent_connections(ui_->recent_model);
  LoadHistoryWorkspace();
  ui_->wall->set_gallery_tiles(ui_->gallery_model);
  ui_->wall->set_gallery_selected_devices(ui_->gallery_selected_model);
  ui_->wall->set_gallery_network_devices(ui_->gallery_network_model);
  ui_->wall->set_gallery_mode(true);
  ui_->wall->set_custom_titlebar(ui_->main->get_custom_titlebar());
  ui_->wall->set_wayland_titlebar(ui_->main->get_wayland_titlebar());
  ui_->wall->set_release_note_blocks(ui_->release_note_blocks_model);
  ui_->wall->set_update_open(false);
  RegisterFontAwesome(ui_->wall->window());
  // The first DeskHub window is 960 x 720 logical pixels including its titlebar.
  // Custom titlebars are part of the Slint client; AppKit's titlebar is not.
  auto default_gallery_size =
      slint::LogicalSize(slint::Size<float>{960.0f, 720.0f});
#if defined(__APPLE__)
  default_gallery_size.height -= MainWindowInitialPositionYOffset();
#endif
  ui_->gallery_window.wall_size = default_gallery_size;
  try {
    std::ifstream file(std::filesystem::path(cache_path_) / "deskhub.json.window");
    if (file) {
      const auto saved = nlohmann::json::parse(file);
      const float scale = std::max(ui_->main->window().scale_factor(), 1.0f);
      ui_->loaded_window_scale[0] = ui_->loaded_window_scale[1] = scale;
      const int width = std::clamp(
          saved.value("width", int(default_gallery_size.width)), 640, 7680);
      const int height = std::clamp(
          saved.value("height", int(default_gallery_size.height)), 486, 4320);
      ui_->gallery_window.wall_size =
          slint::LogicalSize(slint::Size<float>{float(width), float(height)});
      ui_->gallery_window.wall_fullscreen = saved.value("fullscreen", false);
      ui_->gallery_window.wall_maximized = saved.value("maximized", false);
#if defined(__APPLE__)
      // Older state used Slint's cached zoom flag, which could misclassify the
      // fixed-size connection window as maximized. Keep its geometry/fullscreen
      // state, but only trust the maximized flag written by the native check.
      if (saved.value("version", 1) < 2)
        ui_->gallery_window.wall_maximized = false;
#endif
      if (saved.contains("wall_x"))
        ui_->gallery_window.wall_position =
            slint::PhysicalPosition(slint::Point<int32_t>{
                int(saved.at("wall_x").get<float>() * scale),
                int(saved.at("wall_y").get<float>() * scale)});
      if (saved.contains("connection_x"))
        ui_->connection_window.connection_position =
            slint::PhysicalPosition(slint::Point<int32_t>{
                int(saved.at("connection_x").get<float>() * scale),
                int(saved.at("connection_y").get<float>() * scale)});
      const float tile_width = saved.value("tile_width", 216.0f);
      if (std::isfinite(tile_width))
        // The rendered grid constrains the restored width to available space.
        ui_->wall->set_gallery_tile_width(std::max(tile_width, 184.0f));
      const float focus_rail_height = saved.value("focus_rail_height", 70.875f);
      if (std::isfinite(focus_rail_height))
        ui_->wall->set_gallery_focus_rail_height(std::max(focus_rail_height, 54.0f));
      ui_->connection_window.BeginRestore();
    }
  } catch (...) {
    LOG_WARN("Could not restore DeskHub window state");
  }
  ResetSettingsUi();
  ResetSettingsUi(true);
  ui_->main->set_controllers(ui_->controller_model);
  ui_->main->on_controller_selected([this](int index) {
    if (index >= 0 && index < static_cast<int>(ui_->controller_ids.size()))
      selected_server_remote_id_ = ui_->controller_ids[index];
  });
  ui_->main->on_send_controller_file([this](slint::SharedString id) {
    OpenFileManager(std::string(id));
  });
  ui_->main->on_disconnect_controller([this](slint::SharedString id) {
    CloseServerController(std::string(id));
  });
  ui_->main->on_disconnect_all_controllers([this] {
    const auto ids = ui_->controller_ids;
    for (const auto& id : ids) CloseServerController(id);
  });
  BindMainCallbacks();
  BindMainCallbacks(true);
  InitializeSystemTray();
  UpdateLocalization();
  SyncMainWindow();
}

void GuiApplication::ShowMainWindow(const std::string& activation_token) {
#if !defined(__linux__) || defined(__OHOS__)
  (void)activation_token;
#endif
  if (!ui_) {
    return;
  }
  ui_->main->show();
  ui_->main->window().set_minimized(false);
#if _WIN32
  const HWND window = ui_->main->window().win32_hwnd();
  ConfigureWindowsWindowIcons(window);
  ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(window);
#endif
#if defined(__APPLE__)
  ConfigureMainWindowChrome(ui_->main->window().appkit_view());
  MacActivateWindow(ui_->main->window().appkit_view());
  // A window restored from the tray may only obtain its NSWindow after the
  // callback returns, so let the regular UI tick configure it.
  ui_->main_native_titlebar_attempts = 30;
#elif defined(__linux__) && !defined(__OHOS__)
  // Mapping/unminimizing through Slint is asynchronous. Retry native lookup
  // after the window manager has updated its client list and geometry.
  ui_->main_activation_attempts = 30;
  ui_->main_activation_token = activation_token;
#endif
}

void GuiApplication::InitializeSystemTray() {
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
  if (!ui_) {
    return;
  }
  auto show_window = [this] { ShowMainWindow(); };
  auto hide_window = [this] {
    if (ui_) {
      ui_->main->hide();
    }
  };
  auto open_settings = [this, show_window] {
    if (!ui_) {
      return;
    }
    show_window();
    ui_->main->set_settings_open(true);
  };
  auto exit_app = [this] {
    exit_ = true;
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
#elif defined(__OHOS__)
  ui_->tray = std::make_unique<HarmonyTray>(
      std::move(show_window), std::move(hide_window), std::move(open_settings),
      std::move(exit_app), "CrossDesk", localization_language_index_);
#elif (defined(__linux__) && !defined(__OHOS__))
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
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
  return ui_->tray && ui_->tray->MinimizeToTray();
#else
  return false;
#endif
}

void GuiApplication::ResetSettingsUi(bool gallery_window) {
  if (!ui_) {
    return;
  }
  auto& main = ui_->Window(gallery_window);
  main->set_language_index(language_button_value_);
  main->set_codec_index(video_encode_format_button_value_);
  main->set_ip_direct_enabled_draft(config_center_->IsIpDirectEnabled());
  main->set_settings_save_error("");
  try {
    bool listen_all = true;
    if (lan_) {
      listen_all = lan_->identity().listen_all();
    } else {
      const auto directory = path_manager_->GetConfigPath() / "lan";
      if (std::filesystem::exists(directory / "identity.json")) {
        lan::Identity identity(directory);
        identity.Load();
        listen_all = identity.listen_all();
      }
    }
    main->set_lan_listen_all(listen_all);
    main->set_lan_listen_all_draft(listen_all);
  } catch (const std::exception& error) {
    main->set_settings_save_error(
        main->global<ui::LanStrings>().invoke_error_message(UiText(error.what())));
  }
  main->set_force_relay_enabled(config_center_->IsForceRelay());
  main->set_self_hosted_enabled(enable_self_hosted_);
#ifdef _WIN32
  // The machine setting can also be changed by the CLI or another admin.
  enable_autostart_ = config_center_->IsEnableAutostart();
#endif
  main->set_autostart_enabled(enable_autostart_);
#if defined(__OHOS__)
  main->set_autostart_system_managed(true);
#endif
  main->set_daemon_enabled(enable_daemon_);
  main->set_save_remote_previews(config_center_->IsSaveRemotePreviews());
  main->set_desktop_realtime_rate_control(config_center_->IsDesktopRealtimeRateControlEnabled());
  main->set_pause_video_when_minimized(config_center_->IsPauseVideoWhenMinimized());
  main->set_privacy_on_connect_enabled(
      config_center_->IsEnablePrivacyScreen());
  main->set_privacy_local_unlock_allowed(config_center_->IsPrivacyLocalUnlockAllowed());
#if defined(_WIN32) || defined(__APPLE__)
  main->set_privacy_black_screen_available(true);
  main->set_privacy_black_screen(config_center_->IsPrivacyBlackScreen());
#endif
  main->set_lock_on_disconnect_enabled(config_center_->IsLockOnDisconnect());
  main->set_trusted_devices_only(config_center_->IsTrustedDevicesOnly());
#if defined(_WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(__OHOS__))
  main->set_privacy_setting_available(true);
  main->set_lock_on_disconnect_available(true);
#else
  main->set_privacy_setting_available(false);
  main->set_lock_on_disconnect_available(false);
#endif
#if defined(__OHOS__)
  // The sandbox default is not the system desktop. Show the actual receive
  // location until a public directory has been selected and authorized.
  main->set_file_save_path(UiText(file_transfer_save_path_buf_[0]
      ? std::string(file_transfer_save_path_buf_)
      : FileReceiver().OutputDir().string()));
#else
  main->set_file_save_path(file_transfer_save_path_buf_);
#endif
  main->set_server_host(signal_server_ip_self_);
  main->set_server_port(signal_server_port_self_);
  main->set_self_host_settings_error("");
  main->set_gallery_resolution_index(ui_->gallery.stream_settings.resolution);
  main->set_gallery_frame_rate_index(ui_->gallery.stream_settings.FrameRateIndex());
  main->set_gallery_resolution_draft(ui_->gallery.stream_settings.resolution);
  main->set_gallery_frame_rate_draft(ui_->gallery.stream_settings.FrameRateIndex());
  main->set_gallery_bandwidth_index(ui_->gallery.stream_settings.BandwidthIndex());
  main->set_gallery_bandwidth_draft(ui_->gallery.stream_settings.BandwidthIndex());
  main->set_gallery_settings_error("");
}

void GuiApplication::BindMainCallbacks(bool gallery_window) {
  auto* main = ui_->Window(gallery_window).operator->();
  main->on_gallery_pointer([this](slint::PointerEventButton button,
                                 slint::PointerEventKind kind, float x,
                                 float y) {
    if (!ui_->gallery_control_id.empty() && !ui_->gallery_transition.Active())
      SendPointerInput(static_cast<int>(button), static_cast<int>(kind), x, y);
  });
  main->on_gallery_scroll([this](float dx, float dy, float x, float y) {
    if (!ui_->gallery_control_id.empty() && !ui_->gallery_transition.Active())
      SendScrollInput(dx, dy, x, y);
  });
  main->on_gallery_key([this](slint::SharedString text, bool pressed,
                             bool ctrl, bool alt, bool shift, bool meta) {
    if (!ui_->gallery_control_id.empty() && !ui_->gallery_transition.Active())
      SendKeyInput(std::string(text), pressed, ctrl, alt, shift, meta);
  });
  main->on_open_gallery([this] { ShowGalleryWindow(); });
  main->on_connection_mode_changed([this, main](bool lan_mode) {
    const auto mode = lan_mode ? ConfigCenter::CONNECTION_MODE::DIRECT_IP
                               : ConfigCenter::CONNECTION_MODE::DEVICE_ID;
    if (config_center_->SetConnectionMode(mode) != 0) {
      LOG_WARN("Could not save connection mode preference");
      main->set_offline_warning(UiText(
          localization::Literal("无法保存连接模式，下次启动可能不会恢复当前模式。", localization_language_index_)));
    }
  });
  main->on_toggle_gallery_fullscreen(
      [this] { ToggleGalleryFullscreen(); });
  main->on_gallery_action(
      [this, main](slint::SharedString action, slint::SharedString value) {
        GalleryAction(std::string(action), std::string(value),
                      main->get_gallery_mode());
      });
  main->on_gallery_swap([this, main](slint::SharedString source,
                                     slint::SharedString target) {
    if (!main->get_gallery_mode()) return true;
    auto updated = ui_->gallery;
    if (!updated.SwapPositions(std::string(source), std::string(target),
                               ui_->gallery_ids))
      return true;
    if (!updated.Save(ui_->gallery_path)) return false;
    ui_->gallery = std::move(updated);
    ui_->gallery_synced = {};
    SyncGallery();
    return true;
  });
  main->on_gallery_add_device(
      [this](slint::SharedString id, slint::SharedString password,
             slint::SharedString group, bool remember) {
        return AddGalleryDevice(std::string(id), std::string(password),
                                std::string(group), remember);
      });
  main->on_gallery_import_recent([this](slint::SharedString group) {
    return ImportGalleryRecent(std::string(group));
  });
  main->on_device_group_action(
      [this, main](slint::SharedString action, slint::SharedString value) {
        DeviceGroupAction(std::string(action), std::string(value),
                          main->get_gallery_mode());
      });
  main->on_device_group_save([this, main](slint::SharedString group) {
    return SaveDeviceGroups(std::string(group), main->get_gallery_mode());
  });
  main->on_recent_group_selected([this](int index) {
    if (index == 1) ui_->recent_group_filter = "";
    else if (index >= 2 && index - 2 < int(ui_->recent_filter_groups.size()))
      ui_->recent_group_filter = ui_->recent_filter_groups[index - 2];
    else ui_->recent_group_filter.reset();
    SyncMainWindow();
  });
  main->on_delete_group([this](slint::SharedString name) {
    return DeleteGroup(std::string(name));
  });
  main->on_delete_announcement([this](slint::SharedString id,
                                          slint::SharedString revision) {
    const auto parse = [](const slint::SharedString& value)
        -> std::optional<int64_t> {
      const std::string_view text(value.data());
      int64_t number = 0;
      const auto result =
          std::from_chars(text.data(), text.data() + text.size(), number);
      if (result.ec != std::errc{} ||
          result.ptr != text.data() + text.size() || number <= 0)
        return std::nullopt;
      return number;
    };
    const auto announcement_id = parse(id);
    const auto announcement_revision = parse(revision);
    if (!announcement_id || !announcement_revision) return;
    announcements_.Dismiss(*announcement_id, *announcement_revision);
    SyncAnnouncements();
  });
  main->on_refresh_announcements([this] { announcements_.Refresh(); });
  main->on_read_all_announcements([this] {
    announcements_.ReadAll();
    SyncAnnouncements();
  });
  main->on_open_announcement_link([this](slint::SharedString link) {
    const std::string url(link);
    if (announcement_text::IsWebUrl(url)) OpenUrl(url);
  });
  main->on_load_more_announcements([this] {
    announcements_.LoadMore();
    SyncAnnouncements();
  });
  main->on_open_announcement([this](slint::SharedString id,
                                         slint::SharedString revision) {
    const auto snapshot = announcements_.Get();
    for (const auto& item : snapshot.items) {
      if (std::to_string(item["id"].get<int64_t>()) != id.data() ||
          std::to_string(item["revision"].get<int64_t>()) != revision.data())
        continue;
      ui_->announcement_selected_id = id.data();
      ui_->announcement_selected_revision = revision.data();
      ui_->announcement_version = UINT64_MAX;
      announcements_.Read(item["id"], item["revision"]);
      SyncAnnouncements();
      break;
    }
  });
  main->window().on_close_requested([this, main] {
    // Closing DeskHub disconnects its remote sessions. The connection window
    // still minimizes to the tray without terminating the application.
    if (main->get_gallery_mode()) CloseGalleryWindow();
    else MinimizeMainWindowToTray();
    return slint::CloseRequestResponse::HideWindow;
  });
  main->on_main_title_drag([this, main](int phase, float x, float y) {
    if (ui_) {
      DragWindow(ui_->Window(main->get_gallery_mode()), phase, x, y,
                 main->get_gallery_mode() ? ui_->gallery_drag : ui_->main_drag);
    }
  });
  main->on_minimize_main_window([this, main] {
    if (ui_) {
      main->window().set_minimized(true);
    }
  });
  main->on_close_main_window([this, main] {
    if (!ui_) {
      return;
    }
    if (main->get_gallery_mode()) CloseGalleryWindow();
    else MinimizeMainWindowToTray();
    main->hide();
  });
  main->on_copy_local_id([this] {
    if (!SDL_SetClipboardText(client_id_)) {
      LOG_WARN("Copy local id failed: {}", SDL_GetError());
      return false;
    }
    return true;
  });
  main->on_signal_reconnect_requested([this] {
    LOG_INFO("Signal reconnect requested from the status bar");
    if (ForceSignalReconnect() != 0) {
      LOG_WARN("Signal reconnect could not be started");
    }
  });
  main->on_toggle_password_visibility([this, main] {
    show_password_ = !show_password_;
    main->set_password_visible(show_password_);
  });
  main->on_reset_password([this, main](slint::SharedString value) {
    const std::string password(value);
    if (!main->get_lan_mode()) return RequestPasswordChange(password);
    if (!lan::ValidPassword(password)) return false;
    if (!LanAvailable()) return true;  // The initialization error is shown on the main view.
    try {
      lan_error_.clear();
      lan_->SetPassword(password);
      main->set_lan_password_pending(lan_->password_pending());
    } catch (const std::exception& error) {
      lan_error_ = error.what();
    }
    return true;
  });
  main->on_refresh_password([this] {
    const auto password = GenerateRandomPassword(password_saved_);
    if (!password) {
      LOG_ERROR("Could not generate a random password");
      offline_warning_text_ = localization::failed[localization_language_index_];
      show_offline_warning_window_ = true;
      return;
    }
    RequestPasswordChange(*password);
  });
  main->on_lan_select_address([this, main](slint::SharedString address) {
    if (!LanAvailable()) {
      main->set_lan_endpoint_error(UiText(lan_error_.empty() ? "Busy" : lan_error_));
      return false;
    }
    try {
      lan_->identity().SelectAddress(std::string(address));
      const auto port =
          UiText(std::to_string(lan_->PortFor(std::string(address))));
      main->set_lan_listener_ip(address);
      main->set_lan_listener_ipv6(std::string(address).find(':') !=
                                  std::string::npos);
      main->set_lan_listener_port(port);
      main->set_lan_local_port(port);
      main->set_lan_port_dirty(false);
      main->set_lan_endpoint_error("");
      return true;
    } catch (const std::exception& error) {
      main->set_lan_endpoint_error(UiText(error.what()));
      return false;
    }
  });
  main->on_lan_configure([this, main](slint::SharedString address,
                                      slint::SharedString port) {
    if (!LanAvailable()) {
      main->set_lan_endpoint_error(UiText(lan_error_.empty() ? "Busy" : lan_error_));
      return false;
    }
    try {
      const auto port_number = lan::ParsePort(std::string(port));
      lan_->Configure(std::string(address), port_number);
      const auto normalized_port = UiText(std::to_string(port_number));
      main->set_lan_listener_ip(address);
      main->set_lan_listener_ipv6(std::string(address).find(':') !=
                                  std::string::npos);
      main->set_lan_listener_port(normalized_port);
      main->set_lan_local_port(normalized_port);
      main->set_lan_port_dirty(false);
      lan_error_.clear();
      main->set_lan_endpoint_error("");
      return true;
    } catch (const std::exception& error) {
      main->set_lan_endpoint_error(UiText(error.what()));
      return false;
    }
  });
  main->on_lan_reset_password([this] {
    if (LanAvailable()) {
      lan_error_.clear();
      lan_->ResetPassword();
    }
  });
  main->on_lan_copy_text([this](slint::SharedString value) {
    if (!SDL_SetClipboardText(std::string(value).c_str())) {
      lan_error_ = "Cannot copy text";
      return false;
    }
    return true;
  });
  main->on_lan_prepare_connect([this, main](slint::SharedString endpoint) {
    try {
      const auto [address, port] = lan::ParseEndpoint(Trim(std::string(endpoint)), 52888);
      if (!LanAvailable()) return UiText(lan_error_.empty() ? "Busy" : lan_error_);
      main->set_lan_remote_address(UiText(address));
      main->set_lan_remote_port(UiText(std::to_string(port)));
      main->set_lan_retry_after(lan_->RetryAfter(address, port));
      // History is newest first. Match both address and port, and only use
      // the latest matching record when an endpoint has been reused.
      for (const auto& [_, recent] : recent_connections_) {
        if (recent.lan.address != address ||
            recent.lan.port != port)
          continue;
        if (recent.remember_password && lan::ValidPassword(recent.password)) {
          main->set_lan_access_password(UiText(recent.password));
          main->set_lan_remember_password(true);
        }
        break;
      }
      lan_error_.clear();
      main->set_lan_message("");
      return slint::SharedString{};
    } catch (const std::exception& error) {
      return UiText(error.what());
    }
  });
  main->on_lan_cancel_connect([this, main] {
    if (lan_ && lan_connecting_) lan_->Close(lan_connecting_);
    lan_connecting_.reset();
    main->set_lan_connecting(false);
    lan_error_.clear();
    main->set_lan_message("");
  });
  main->on_lan_connect([this, main](slint::SharedString address,
                              slint::SharedString port,
                              slint::SharedString password, bool remember) {
    try {
      ConnectLan(std::string(address), lan::ParsePort(std::string(port)),
                 std::string(password), remember);
    } catch (const std::exception& error) {
      lan_error_ = error.what();
    }
    main->set_lan_connecting(lan_connecting_ != nullptr);
    main->set_lan_message(UiText(lan_error_));
  });
  main->on_connect_requested(
      [this](slint::SharedString id) { ConnectFromUi(std::string(id)); });
  main->on_format_remote_id([](slint::SharedString value) {
    return UiText(FormatRemotePeerIdInput(std::string_view(value)));
  });
  main->on_recent_connect(
      [this](slint::SharedString id) { ConnectFromUi(std::string(id)); });
  main->on_recent_disconnect([this](slint::SharedString id) {
    const std::string remote_id(id);
    if (auto props = FindRemoteSession(remote_id);
        props && props->connection_status_ == MiniRtcConnectionStatus::MiniRtcConnectionConnected &&
        !props->closing_) {
      DisconnectSession(props->remote_id_);
      SyncMainWindow();
    }
  });
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
        ui_->gallery_synced = {};
      });
  main->on_recent_delete([this](slint::SharedString id) {
    const std::string remote_id(id);
    const auto connection = std::find_if(
        recent_connections_.begin(), recent_connections_.end(),
        [&](const auto& entry) { return entry.second.remote_id == remote_id; });
    if (connection != recent_connections_.end()) {
      if (thumbnail_->DeleteThumbnail(connection->first) != 0) return;
      if (ui_->gallery.HasAssignments(remote_id) && !ui_->gallery.Contains(remote_id)) {
        auto updated = ui_->gallery;
        updated.Forget(remote_id);
        if (updated.Save(ui_->gallery_path)) ui_->gallery = std::move(updated);
      }
      settings_.EraseRecentConnectionAlias(remote_id);
      settings_.SaveRecentConnectionAliases();
      reload_recent_connections_ = true;
    }
  });
  main->on_browse_save_path([this, main] {
    const char* folder = tinyfd_selectFolderDialog(
        localization::file_transfer_save_path[localization_language_index_]
            .c_str(),
        file_transfer_save_path_buf_[0] ? file_transfer_save_path_buf_
                                        : nullptr);
    if (folder) {
      main->set_file_save_path(folder);
    }
  });
  BindTrustedDeviceCallbacks(gallery_window);
  main->on_save_settings([this, main] {
    return SaveSettingsFromUi(main->get_gallery_mode());
  });
  main->on_cancel_settings([this, main] {
    ResetSettingsUi(main->get_gallery_mode());
  });
#if defined(__OHOS__)
  main->on_refresh_autostart_status([] { RefreshHarmonyAutostartStatus(); });
#endif
  main->on_save_self_hosted_settings([this, main] {
    // Stage the endpoint until the surrounding settings dialog is confirmed.
    return ValidateSelfHostedSettingsFromUi(main->get_gallery_mode());
  });
  main->on_cancel_self_hosted_settings([this, main] {
    main->set_self_host_settings_error("");
    const std::string host = config_center_->GetSignalServerHost();
    main->set_server_host(UiText(host));
    const int signal_port = config_center_->GetSignalServerPort();
    main->set_server_port(signal_port > 0
                                   ? UiText(std::to_string(signal_port))
                                   : slint::SharedString{});
  });
  main->on_cancel_update([this] {
#if CROSSDESK_HAS_UPDATER
    if (updater_ && updater_->busy()) updater_->Cancel();
#endif
  });
  main->on_open_manual_download([this] {
    const auto& latest = update_checker_->latest();
    OpenUrl(latest && !latest->download_url.empty() ? latest->download_url
                                                   : "https://crossdesk.cn");
  });
  main->on_open_download([this, main] {
#if CROSSDESK_HAS_UPDATER
    const auto& latest = update_checker_->latest();
    if (updater_ && updater_->busy()) return;
    if (updater_ && latest && updater_->Available(latest->update_envelope)) {
      main->set_about_open(false);
      main->set_update_open(true);
      main->set_update_confirm_open(true);
      return;
    }
#endif
    main->invoke_open_manual_download();
  });
  main->on_confirm_update([this, main] {
    if (!main->get_update_confirm_open()) return;
    for (const auto& window : {ui_->main, ui_->wall})
      window->set_update_confirm_open(false);
#if CROSSDESK_HAS_UPDATER
    const auto& latest = update_checker_->latest();
    if (!updater_ || updater_->busy() || !latest ||
        !updater_->Available(latest->update_envelope)) return;
    updater_->Start(latest->update_envelope);
    // Disable both windows immediately, before the next polling tick.
    for (const auto& window : {ui_->main, ui_->wall}) {
      window->set_update_install_busy(updater_->busy());
      window->set_update_install_failed(
          updater_->state() == updater::Controller::State::Failed);
      window->set_update_install_status("");
    }
#endif
  });
  about::Populate(ui_->Window(gallery_window));
  main->on_open_about_link([this](slint::SharedString url) {
    const std::string link(url);
    if (link.starts_with("https://")) OpenUrl(link);
  });
  main->on_open_website([this] { OpenUrl("https://crossdesk.cn"); });
  main->on_open_feedback_github([this, main] {
    if (!OpenUrl("https://github.com/kunkundi/crossdesk/issues/new/choose")) {
      main->set_feedback_succeeded(false);
      main->set_feedback_status(UiText(localization::Literal(
          "无法打开浏览器，请访问 GitHub 项目页面。", localization_language_index_)));
    }
  });
  main->on_export_diagnostic_logs([this] { ExportDiagnosticLogs(); });
  main->on_open_afdian_sponsor([this] {
    OpenUrl("https://afdian.com/a/kunkundi");
  });
  main->on_open_github_sponsor([this] {
    OpenUrl("https://github.com/sponsors/kunkundi");
  });
  main->on_open_kofi_sponsor([this] {
    OpenUrl("https://ko-fi.com/kunkundi");
  });
  main->on_dismiss_release_welcome([this] {
    ui_->main->set_release_welcome_open(false);
    // Retry if the initial write failed while the dialog was on screen.
    if (config_center_->GetLastPresentedRelease() != CROSSDESK_VERSION &&
        config_center_->SetLastPresentedRelease(CROSSDESK_VERSION) != 0) {
      LOG_WARN("Could not persist the displayed release version");
    }
  });
  main->on_check_for_updates([this] {
    update_checker_->RequestCheck();
    SyncMainWindow();
  });
  main->on_connection_cancel([this] {
    const auto props = FindRemoteSession(ui_->connection_dialog_remote_id);
    if (!props) {
      show_connection_status_window_ = false;
      return;
    }
    DisconnectSession(props->remote_id_);
    re_enter_remote_id_ = true;
    show_connection_status_window_ = false;
  });
  main->on_connection_acknowledge([this] {
    const auto props = FindRemoteSession(ui_->connection_dialog_remote_id);
    if (props) {
      const MiniRtcConnectionStatus status = props->connection_status_.load();
      if (status == MiniRtcConnectionStatus::MiniRtcConnectionNoSuchTransmissionId ||
          status == MiniRtcConnectionStatus::MiniRtcConnectionRemoteUnavailable) {
        DisconnectSession(props->remote_id_);
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
    if (!props->connection_auth_.Begin(password)) return;
    std::memset(props->remote_password_, 0, sizeof(props->remote_password_));
    std::memcpy(props->remote_password_, password.data(), password.size());
    props->remember_password_ = remember;
    props->rejoin_ = true;
    need_to_rejoin_ = true;
  });
  main->on_open_permissions([this, main] {
#ifdef __APPLE__
    mac_permission_dialog_dismissed_ = false;
    mac_permission_dialog_requested_ = true;
    show_request_permission_window_ = true;
    if (main->get_gallery_mode()) ShowMainWindow();
#endif
  });
  main->on_dismiss_permissions([this] {
#ifdef __APPLE__
    mac_permission_dialog_dismissed_ = true;
    mac_permission_dialog_requested_ = false;
    show_request_permission_window_ = false;
#endif
  });
  main->on_retry_permission_migration([this] {
#ifdef __APPLE__
    StartMacPermissionMigration(true);
#endif
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
  main->on_open_portable_service([this] {
#if _WIN32 && CROSSDESK_PORTABLE
    if (portable_service_install_state_.load(std::memory_order_acquire) ==
        PortableServiceInstallState::installing) return;
    JoinPortableWindowsServiceInstallThread();
    portable_service_prompt_checked_ = false;
    CheckPortableWindowsService();
    portable_service_needs_repair_ =
        portable_service_needs_repair_ || portable_service_installed_;
    portable_service_install_state_.store(PortableServiceInstallState::idle,
                                          std::memory_order_release);
    show_portable_service_install_window_ = true;
    ShowMainWindow();
#endif
  });
  main->on_install_portable_service([this, main] {
#if _WIN32 && CROSSDESK_PORTABLE
    portable_service_do_not_remind_ = false;
    main->set_portable_service_do_not_remind(false);
    if (main->get_gallery_mode()) ui_->main->show();
    StartPortableWindowsServiceInstall();
#endif
  });
  main->on_cancel_portable_service([this, main] {
#if _WIN32 && CROSSDESK_PORTABLE
    if (portable_service_install_state_.load(std::memory_order_acquire) ==
        PortableServiceInstallState::installing) {
      return;
    }
    portable_service_do_not_remind_ =
        main->get_portable_service_do_not_remind();
    if (portable_service_do_not_remind_ && !portable_service_needs_repair_) {
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

void GuiApplication::BindStreamCallbacks(StreamView& view) {
  auto& stream = view.window;
#if defined(__APPLE__)
  stream->on_fullscreen_video_inset([&view](float width, float height) {
    return StreamWindowVideoTransitionInset(view.window->window().appkit_view(), width, height);
  });
#endif
  stream->on_place_control_pointer([&view](float x, float y) {
    return PlaceStreamControlPointer(view.window->window(), x, y);
  });
  stream->set_mouse_sensitivity_percent(config_center_->GetMouseSensitivityPercent());
  stream->set_wheel_sensitivity_percent(
      config_center_->GetWheelSensitivityPercent(view.session->device_preference_key_));
  stream->on_save_input_sensitivity([this, &view](int mouse, int wheel) {
    auto& window = view.window;
    const int old_mouse = config_center_->GetMouseSensitivityPercent();
    const auto& device = view.session->device_preference_key_;
    const int old_wheel = config_center_->GetWheelSensitivityPercent(device);
    if (mouse == old_mouse && wheel == old_wheel) return;
    if (config_center_->SetInputSensitivity(device, mouse, wheel) != 0) {
      window->set_mouse_sensitivity_percent(old_mouse);
      window->set_wheel_sensitivity_percent(old_wheel);
      window->set_sensitivity_error(UiText(localization::Literal(
          "保存失败，请重试", localization_language_index_)));
      return;
    }
    for (auto& [_, other] : ui_->streams) {
      if (mouse != old_mouse) other.window->set_mouse_sensitivity_percent(mouse);
      if (other.session->device_preference_key_ == device) {
        other.window->set_wheel_sensitivity_percent(wheel);
        other.window->set_sensitivity_error("");
      }
    }
    if (mouse != old_mouse) ResetPointerMotion();
  });
  stream->on_dismiss_remote_update([&view] {
    std::lock_guard lock(view.session->remote_version_mutex_);
    view.session->remote_update_dismissed_ = true;
  });
  const auto close_window = [this, &view] {
    const auto& props = view.session;
    if (props->wall_managed_) {
      if (ui_->gallery_control_id.empty() && controlled_remote_id_ == props->remote_id_)
        ReleaseControl();
      props->stream_window_open_ = false;
    } else if (FindRemoteSession(props->remote_id_) == props) {
      DisconnectSession(props->remote_id_);
    }
  };
  stream->window().on_close_requested([close_window] {
    close_window();
    // The next UI tick detaches the renderer and native chrome before hiding.
    return slint::CloseRequestResponse::KeepWindowShown;
  });
  stream->on_close_stream_window(close_window);
  stream->on_stream_title_drag([&view](int phase, float x, float y) {
    DragWindow(view.window, phase, x, y, view.drag);
  });
  stream->on_minimize_stream_window([&view] {
    view.ObserveWindowState();
#if defined(__APPLE__)
    MinimizeMacStreamWindow(view.window->window().appkit_view());
#else
    view.window->window().set_minimized(true);
#endif
  });
  stream->on_toggle_maximize_stream_window([&view] {
    view.ObserveWindowState();
    auto& window = view.window->window();
    const bool maximize = !window.is_maximized();
    view.window->set_window_maximized(maximize);
    window.set_maximized(maximize);
  });
  stream->on_switch_display([this, &view](int index) {
    const auto& props = view.session;
    if (!props->peer_ || index < 0) return;
    {
      std::lock_guard lock(props->remote_version_mutex_);
      if (index >= static_cast<int>(props->display_info_list_.size())) return;
    }
    props->selected_display_ = index;
    view.window->global<ui::RemoteCursor>().invoke_reset();
    if (controlled_remote_id_ == props->remote_id_) ui_->pointer_motion = {};
    {
      std::lock_guard lock(props->remote_cursor_state_mutex_);
      props->cursor_presentation_ = {};
    }
    RemoteAction action{};
    action.type = ControlType::display_id;
    action.d = index;
    const std::string message = action.to_json();
    MiniRtcSendReliableDataFrame(props->peer_, message.c_str(), message.size(),
                          props->control_data_label_.c_str());
  });
  stream->on_send_shortcut([&view](slint::SharedString shortcut) {
    const auto& props = view.session;
    if (!props->peer_ || !props->control_mouse_ ||
        props->connection_status_.load() != MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
      return;
    }
    const std::string name(shortcut);
    if (name != "Ctrl+Alt+Del" && name != "Win+L") return;
    {
      std::lock_guard lock(props->remote_version_mutex_);
      if (props->remote_platform_ != HostPlatform::Windows) return;
    }
    RemoteAction action{};
    action.type = ControlType::service_command;
    action.c.flag = name == "Ctrl+Alt+Del"
                        ? ServiceCommandFlag::send_sas
                        : ServiceCommandFlag::lock_workstation;
    const std::string message = action.to_json();
    MiniRtcSendReliableDataFrame(props->peer_, message.c_str(), message.size(),
                          props->control_data_label_.c_str());
  });
  const auto send_privacy = [&view](bool relock) {
    const auto& props = view.session;
    if (!props->peer_ || (!relock && !props->control_mouse_) ||
        props->connection_status_.load() != MiniRtcConnectionStatus::MiniRtcConnectionConnected) return;
    RemoteAction action{};
    action.type = ControlType::privacy_command;
    {
      std::lock_guard lock(props->privacy_status_mutex_);
      const auto& status = props->privacy_status_;
      const uint64_t now = SDL_GetTicks();
      const auto controls = GetPrivacyControlState(
          status, props->privacy_status_received_, props->privacy_command_pending_,
          now - props->privacy_status_tick_, now - props->privacy_command_tick_);
      const bool can_lock = props->privacy_status_received_ && status.supported &&
          now - props->privacy_status_tick_ <= 5000 &&
          (!props->privacy_command_pending_ || now - props->privacy_command_tick_ >= 15000);
      if (!props->privacy_desktop_available_ || !(relock ? can_lock : controls.can_toggle)) return;
      action.pc.flag = relock ? PrivacyCommandFlag::lock
          : controls.active ? PrivacyCommandFlag::disable : PrivacyCommandFlag::enable;
      action.pc.block_local_input = true;
      props->privacy_command_pending_ = true;
      props->privacy_command_tick_ = now;
      props->privacy_request_revision_ = status.revision;
    }
    const auto message = action.to_json();
    if (MiniRtcSendReliableDataFrame(props->peer_, message.data(), message.size(),
                              props->control_data_label_.c_str()) != 0) {
      std::lock_guard lock(props->privacy_status_mutex_);
      props->privacy_command_pending_ = false;
      if (!relock) props->privacy_status_received_ = false;
      LOG_WARN("Privacy command send failed");
    }
  };
  stream->on_toggle_privacy([send_privacy] { send_privacy(false); });
  stream->on_lock_privacy([send_privacy] { send_privacy(true); });
  stream->on_dismiss_privacy_unlock([&view] {
    std::lock_guard lock(view.session->privacy_status_mutex_);
    view.session->privacy_unlock_notice_ = false;
  });
  stream->on_toggle_mouse_control([this, &view] {
    const auto& props = view.session;
    if (!props->connection_established_) {
      return;
    }
    props->control_mouse_ = !props->control_mouse_;
    props->enable_mouse_control_ = props->control_mouse_;
    SetStreamKeyboardFocus(view, true);
  });
  stream->on_change_video_settings([&view](int quality, int frame_rate_index,
                                          int preference) {
    const auto& props = view.session;
    if (!props->peer_ ||
        props->connection_status_.load() != MiniRtcConnectionStatus::MiniRtcConnectionConnected)
      return;
    RemoteAction action{};
    action.type = ControlType::video_settings;
    action.vs = {quality, frame_rate_index == 0 ? 30 : 60, preference};
    if (frame_rate_index < 0 || frame_rate_index > 1 ||
        !ValidVideoSettings(action.vs))
      return;
    {
      std::lock_guard lock(props->video_settings_mutex_);
      if (!props->video_settings_supported_) return;
      action.vs.request_id = props->video_settings_.request_id + 1;
      const auto message = action.to_json();
      if (MiniRtcSendReliableDataFrame(props->peer_, message.data(), message.size(),
                                props->control_data_label_.c_str()) != 0)
        return;
      props->video_settings_ = action.vs;
      props->video_settings_pending_ = true;
      props->video_settings_failed_ = false;
      props->video_settings_request_tick_ = SDL_GetTicks();
    }
  });
  stream->on_change_remote_resolution([this, &view](int index) {
    const auto& props = view.session;
    if (!props->peer_ || props->closing_ || index < 0 ||
        props->connection_status_ != MiniRtcConnectionStatus::MiniRtcConnectionConnected ||
        view.resolution_choices.display_id != props->selected_display_ ||
        index >= int(view.resolution_choices.modes.size())) return;
    RemoteAction action;
    action.type = ControlType::display_resolution;
    {
      std::lock_guard lock(props->display_resolution_mutex_);
      auto& state = props->display_resolution_;
      const DisplayResolutionChange selection{props->selected_display_,
          view.resolution_choices.display_key, view.resolution_choices.modes[index]};
      // Explicitly choosing the current mode also establishes a preference.
      if (!state.pending && std::any_of(state.displays.begin(), state.displays.end(),
          [&](const auto& display) {
            return SupportsDisplayResolution(display, selection) &&
                   display.current == selection.resolution;
          })) {
        if (!remote_resolutions_.Remember(props->device_preference_key_,
                {selection.display_key, selection.resolution}))
          LOG_WARN("Could not save remote display resolution");
        return;
      }
      if (!state.Begin(selection, SDL_GetTicks())) return;
      action.resolution_change = state.requested;
    }
    const auto message = action.to_json();
    if (MiniRtcSendReliableDataFrame(props->peer_, message.data(), message.size(),
                                    props->control_data_label_.c_str()) != 0) {
      std::lock_guard lock(props->display_resolution_mutex_);
      if (props->display_resolution_.requested.request_id == action.resolution_change.request_id) {
        props->display_resolution_.pending = false;
        props->display_resolution_.failed = true;
      }
    }
  });
  stream->on_toggle_audio([this, &view] {
    const auto& props = view.session;
    if (!props->connection_established_) {
      return;
    }
    if (props->audio_capture_button_pressed_)
      MuteRemoteAudio(props);
    else
      UnmuteRemoteAudio(props);
    SyncAudioPlayback();
  });
  stream->on_select_file([this, &view] {
    OpenFileManager(view.session->remote_id_);
  });
  stream->on_can_drop_file(
      [](const slint::DataTransfer& data) { return data.has_plain_text(); });
  stream->on_file_dropped([this, &view](const slint::DataTransfer& data) {
    const auto text = data.plain_text();
    const auto& props = view.session;
    if (!text) {
      return;
    }
    const std::string path = DecodeDroppedPath(std::string(*text));
    if (!path.empty()) {
      transfers_.ProcessSelectedFile(path, props, props->file_label_,
                                     props->remote_id_);
    }
  });
  stream->on_toggle_stats([&view] {
    view.session->net_traffic_stats_button_pressed_ =
        !view.session->net_traffic_stats_button_pressed_;
  });
  stream->on_toggle_fullscreen([this, &view] {
    auto& stream = view.window;
    view.ObserveWindowState();
    const bool enter_fullscreen = !stream->get_fullscreen_enabled();
    stream->set_fullscreen_enabled(enter_fullscreen);
    auto& window = stream->window();
    window.request_redraw();
    window.set_fullscreen(enter_fullscreen);
#if defined(__APPLE__)
    NotifyVideoFrameReady(*view.session);
#endif
  });
  stream->on_disconnect([this, &view] {
    if (FindRemoteSession(view.session->remote_id_) == view.session)
      DisconnectSession(view.session->remote_id_);
  });
  stream->on_pointer_input([this, &view](slint::PointerEventButton button,
                                  slint::PointerEventKind kind, float x,
                                  float y) {
    if (!AcceptStreamInput(view)) return;
    SendPointerInput(static_cast<int>(button), static_cast<int>(kind), x, y);
  });
  stream->on_scroll_input([this, &view](float dx, float dy, float x, float y) {
    if (!AcceptStreamInput(view)) return;
    SendScrollInput(dx, dy, x, y);
  });
  stream->on_key_input([this, &view](slint::SharedString text, bool pressed,
                              bool control, bool alt, bool shift, bool meta) {
    if (!AcceptStreamInput(view)) return;
    SendKeyInput(std::string(text), pressed, control, alt, shift, meta);
  });
  stream->on_keyboard_focus_changed([this, &view](bool focused) {
    SetStreamKeyboardFocus(view, focused);
  });
}

void GuiApplication::Tick() {
#if _WIN32
  if (ui_) {
    ui_->session_end.Start();  // Retry a transient native-window creation failure.
    if (ui_->session_end.Ending()) exit_ = true;
  }
#endif
  if (exit_) {
    BeginShutdown();
    if (ShutdownDrained()) slint::quit_event_loop();
    return;
  }
  PollDiagnosticExport();
#ifdef _WIN32
  TickLocalHost();
#endif
  TickLan();
  TickControllerTrust();
  if (LanAvailable()) {
    const auto address = lan_->identity().settings().value("address", "");
    ui_->main->set_lan_listen_all(lan_->identity().listen_all());
    ui_->main->set_lan_password_pending(lan_->password_pending());
    ui_->main->set_lan_listening(lan_->listening());
    if (lan_connecting_ && lan_connecting_->authorized) {
      ui_->main->set_lan_open(false);
      lan_connecting_.reset();
    } else if (lan_connecting_ && lan_connecting_->closing) {
      lan_connecting_.reset();
    }
    ui_->main->set_lan_connecting(lan_connecting_ != nullptr);
    int retry_after = 0;
    if (ui_->main->get_lan_open()) {
      try {
        retry_after = lan_->RetryAfter(
            std::string(ui_->main->get_lan_remote_address()),
            lan::ParsePort(std::string(ui_->main->get_lan_remote_port())));
      } catch (const std::exception&) {
        // An incomplete address has no cooldown; input validation handles it.
      }
    }
    ui_->main->set_lan_retry_after(retry_after);
    if (auto password = lan_->TakeNewPassword(); !password.empty())
      ui_->main->set_lan_new_password(UiText(password));
    const auto& available_interfaces = lan_->interfaces();
    const auto previous = ui_->main->get_lan_interfaces();
    bool changed = !previous || previous->row_count() != available_interfaces.size();
    for (size_t i = 0; !changed && i < available_interfaces.size(); ++i)
      changed = std::string_view(previous->row_data(i).value()) !=
                available_interfaces[i].address;
    if (changed) {
      std::vector<slint::SharedString> addresses;
      addresses.reserve(available_interfaces.size());
      std::vector<slint::SharedString> labels;
      labels.reserve(available_interfaces.size());
      for (const auto& network_interface : available_interfaces) {
        const auto& ip = network_interface.address;
        addresses.push_back(UiText(ip));
        labels.push_back(
            UiText(ip.find(':') == std::string::npos ? ip : "[" + ip + "]"));
      }
      ui_->main->set_lan_interface_labels(
          std::make_shared<slint::VectorModel<slint::SharedString>>(
              std::move(labels)));
      ui_->main->set_lan_interfaces(
          std::make_shared<slint::VectorModel<slint::SharedString>>(std::move(addresses)));
    }
    // Keep the current displayed address while it remains available, including
    // when a previously missing preferred address returns during a port edit.
    const auto previous_address = std::string(ui_->main->get_lan_listener_ip());
    auto display_address = previous_address.empty() ? address : previous_address;
    int interface_index = -1;
    for (size_t i = 0; i < available_interfaces.size(); ++i)
      if (available_interfaces[i].address == display_address) {
        interface_index = static_cast<int>(i);
        break;
      }
    if (interface_index < 0) {
      interface_index = available_interfaces.empty() ? -1 : 0;
      display_address = interface_index == 0
          ? available_interfaces.front().address : "";
    }
    const auto port =
        UiText(std::to_string(lan_->PortFor(display_address)));
    if (previous_address != display_address) {
      // A disappearing address must not transfer its unfinished port edit to
      // another interface. Explicit selection commits the old edit first.
      if (!previous_address.empty() && ui_->main->get_lan_port_dirty()) {
        ui_->main->set_lan_access_warning_endpoint(UiText(previous_address));
        ui_->main->set_lan_access_warning("Selected LAN interface is unavailable");
      }
      ui_->main->set_lan_port_dirty(false);
      ui_->main->set_lan_endpoint_error("");
      ui_->main->set_lan_local_port(port);
    }
    ui_->main->set_lan_listener_ip(UiText(display_address));
    ui_->main->set_lan_listener_ipv6(display_address.find(':') !=
                                     std::string::npos);
    ui_->main->set_lan_listener_port(port);
    ui_->main->set_lan_interface_index(interface_index);
    ui_->main->set_lan_interface_draft_index(interface_index);
    if (!ui_->main->get_lan_port_editing() && !ui_->main->get_lan_port_dirty())
      ui_->main->set_lan_local_port(port);
    ui_->main->set_lan_listener_error(UiText(
        display_address.empty() && !lan_->identity().listen_all()
                                ? "Selected LAN interface is unavailable"
                                : lan_->ListenerError(display_address)));
    // Show failures one address at a time, without interrupting edits or another
    // dialog. A working listener must not clear another address's error.
    if (ui_->main->get_lan_mode() && ui_->main->get_can_show_release_welcome() &&
        !ui_->main->get_lan_port_editing() && !ui_->main->get_lan_port_dirty()) {
      if (auto issue = lan_->TakeListenerWarning()) {
        ui_->main->set_lan_access_warning_endpoint(
            UiText(lan::FormatEndpoint(issue->address, issue->port)));
        ui_->main->set_lan_access_warning(UiText(issue->error));
      }
    }
  }
  const auto lan_message = lan_error_.rfind("ConnectionFailed", 0) == 0
                               ? std::string("ConnectionFailed")
                               : lan_error_;
  ui_->main->set_lan_message(UiText(lan_message));
  PollUpdateCheck();
  HandleSessionCleanup();
  HandlePasswordChangeResult();
  HandleCredentialRecovery();
  if (!peer_) {
    signal_connection_indicator_.Reset();
    CreateConnectionPeer();
  }
#if defined(__linux__) && !defined(__OHOS__)
  HeadlessConsole::Instance().Poll(
      signal_connected_.load(),
      [this](const std::string& password) { RequestPasswordChange(password); },
      [this] {
        const auto password = GenerateRandomPassword(password_saved_);
        if (password) RequestPasswordChange(*password);
        else HeadlessConsole::Instance().NotifyKey("console_random_failed");
      },
      [this] { exit_ = true; });
#endif

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
#ifdef __OHOS__
      // Harmony's pasteboard reports content changes without an SDL owner.
      clipboard_.HandleLocalUpdate();
#else
      clipboard_.HandleLocalUpdate(!event.clipboard.owner);
#endif
    }
  }
  clipboard_.ApplyPendingRemoteText();
  ProcessAutomationRequests();
#ifndef __OHOS__
  std::string activation_token;
  bool activation_requested =
      ui_->activation_receiver && ui_->activation_receiver->TakeRequest(&activation_token);
#ifdef __APPLE__
  if (ui_->tray && ui_->tray->TakeReopenRequest()) activation_requested = true;
#endif
  if (activation_requested) ShowMainWindow(activation_token);
#endif
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
  if (ui_->main_activation_attempts > 0) {
    --ui_->main_activation_attempts;
    const auto& window = ui_->main->window();
    if (window.wayland_surface()) {
      if (platform::ActivateWaylandWindow(window.wayland_display(),
              window.wayland_surface(), ui_->main_activation_token))
        ui_->main_activation_attempts = 0;
    } else {
      if (!ui_->x11_focus_display) ui_->x11_focus_display = XOpenDisplay(nullptr);
      if (X11ActivateWindow(ui_->x11_focus_display, "CrossDesk"))
        ui_->main_activation_attempts = 0;
    }
    if (ui_->main_activation_attempts == 0) ui_->main_activation_token.clear();
  }
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
    HandlePendingRejoins();
  }

  const bool was_reloading_recent_connections = reload_recent_connections_;
  HandleRecentConnections();
  if (was_reloading_recent_connections && !reload_recent_connections_) {
    ui_->recent_model_signature.clear();
    ui_->recent_preview_images.clear();
  }
  HandleConnectionStatusChange();
  HandlePendingPresenceProbe();
  HandlePresenceProbeTimeout();
  HandlePrivacy();
  if (privacy_settings_changed_) {
    for (const auto& window : {ui_->main, ui_->wall})
      window->set_privacy_local_unlock_allowed(config_center_->IsPrivacyLocalUnlockAllowed());
    privacy_settings_changed_ = false;
  }
  HandleScreenLock();
  HandleWindowsServiceIntegration();
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
  SyncXWaylandWindowActivation();
#else
  SyncStreamKeyboardFocus();
#endif
  devices_.UpdateInteractions();
  if (!ui_->gallery_control_id.empty()) keyboard_.SendHeartbeat(false);
  ShareLocalCursorState();

  CloseDisconnectedSessions();
  UpdateLocalization();
  SyncMainWindow();
  SyncGallery();
  SyncConnectionDialog();
  SyncPlatformDialogs();
  // A background or obstructed launch must not consume the first presentation.
  if (ui_->main->get_release_welcome_pending() &&
      ui_->main->window().is_visible() &&
      !ui_->main->window().is_minimized() &&
      ui_->main->get_can_show_release_welcome() &&
      !ui_->file_dialog_open && !HasActiveSession()) {
    ui_->main->set_release_welcome_open(true);
    ui_->main->set_release_welcome_pending(false);
    if (config_center_->SetLastPresentedRelease(CROSSDESK_VERSION) != 0) {
      LOG_WARN("Could not persist the displayed release version");
    }
  }
  SyncStreamWindows();
  SyncVideoFrameGapStats();
#if defined(__APPLE__)
  UpdateVideoFrameScheduler();
#endif
  SyncIncomingControllers();
  SyncTrustedDevices();
  for (auto it = ui_->file_browsers.begin(); it != ui_->file_browsers.end();) {
    if (!it->second->Tick()) it = ui_->file_browsers.erase(it);
    else {
      if (it->second->Visible())
        it->second->SetPeerName(SessionDisplayName(it->first));
      ++it;
    }
  }
  SyncClipboardFileNotification();
  SyncFileTransferNotifications();
#if defined(__APPLE__)
  // Native hide/show may complete after its callback. Re-evaluate after all
  // session windows have been synchronized, including the last session close.
  if (ui_->tray) ui_->tray->SyncDockVisibility();
#endif
}

void GuiApplication::PollUpdateCheck() {
#if CROSSDESK_HAS_UPDATER
  if (updater_) {
    // The event loop is running and initialization has completed.
    static bool startup_confirmed = false;
    if (!startup_confirmed) {
      updater_->ConfirmStartup();
      startup_confirmed = true;
    }
    if (updater_->Poll()) {
      exit_ = true;
      return;
    }
    const auto& latest = update_checker_->latest();
    const bool can_install = latest && updater_->Available(latest->update_envelope);
    const bool failed = updater_->state() == updater::Controller::State::Failed;
    const int language = localization::detail::ClampLanguageIndex(localization_language_index_);
    std::string status;
    if (updater_->busy()) {
      status = localization::update_preparing[language];
      if (updater_->state() == updater::Controller::State::Downloading)
        status += " " + std::to_string(updater_->progress()) + "%";
    } else if (failed) {
      status = (can_install ? localization::update_install_failed[language]
                            : localization::update_install_failed_manual[language]) +
               "\n" + updater_->error();
    }
    for (const auto& main : {ui_->main, ui_->wall}) {
      main->set_update_install_available(can_install);
      main->set_update_install_busy(updater_->busy());
      main->set_update_install_failed(failed);
      main->set_update_install_status(UiText(status));
    }
  }
#endif
  // The first tick runs after show()/event-loop startup. Poll never waits for
  // networking and all application/model changes remain on this UI thread.
  if (!update_checker_->Poll() ||
      update_checker_->status() == UpdateChecker::Status::Failed) {
    return;
  }
  const auto& info = *update_checker_->latest();
  latest_version_ = "v" + info.version;
  release_name_ = info.release_name;
  release_notes_ = info.release_notes;
  release_date_ = info.release_date;
  update_available_ = update_checker_->update_available();
  std::vector<ui::ReleaseNoteBlock> blocks;
  for (const auto& parsed : release_notes_text::Parse(release_notes_)) {
    ui::ReleaseNoteBlock block;
    block.content = parsed.content;
    block.section_gap = parsed.section_gap;
    blocks.push_back(std::move(block));
  }
  ui_->release_note_blocks_model->set_vector(std::move(blocks));
}

void GuiApplication::ShareLocalCursorState() {
  constexpr auto kCursorEchoSuppressionInterval = 300ms;
  constexpr auto kCursorStateHeartbeatInterval = 500ms;
  constexpr auto kCursorPositionShareInterval = 16ms;
  constexpr float kCursorPositionEpsilon = 0.00005f;

  std::vector<std::string> connected_controllers;
  {
    std::shared_lock lock(connection_status_mutex_);
    connected_controllers.reserve(connection_status_.size());
    for (const auto& [remote_id, status] : connection_status_) {
      if (managed_host_frontend_ && remote_id.rfind("lan/", 0) != 0) continue;
      if (status == MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
        connected_controllers.push_back(remote_id);
      }
    }
  }
  if (!is_server_mode_ || connected_controllers.empty()) {
    cursor_delivery_states_.clear();
    return;
  }

  CursorState sampled{};
  const bool sample_ok = cursor_state_provider_.Sample(
      devices_.display_info_list(), selected_display_, &sampled);
  if (!sample_ok) {
    sampled.render_mode = CursorRenderMode::unknown;
    if (sampled.hidden_reason == CursorHiddenReason::unspecified) {
      sampled.hidden_reason = CursorHiddenReason::sampling_failed;
    }
  } else if (sampled.render_mode == CursorRenderMode::legacy) {
    // Providers without platform-specific presentation metadata only need to
    // sample visible/shape; normalize that common case once for every peer.
    sampled.render_mode = sampled.visible ? CursorRenderMode::separate
                                          : CursorRenderMode::hidden;
    sampled.hidden_reason = sampled.visible ? CursorHiddenReason::unspecified
                                            : CursorHiddenReason::system_hidden;
  }

  const auto now = std::chrono::steady_clock::now();

  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      last_input_times;
  {
    std::lock_guard lock(remote_pointer_input_mutex_);
    for (const auto& remote_id : connected_controllers) {
      const auto input_it = last_remote_pointer_input_time_.find(remote_id);
      if (input_it != last_remote_pointer_input_time_.end()) {
        last_input_times.emplace(remote_id, input_it->second);
      }
    }
  }

  std::unordered_set<std::string> connected_ids(connected_controllers.begin(),
                                                connected_controllers.end());
  std::erase_if(cursor_delivery_states_, [&](const auto& entry) {
    return connected_ids.find(entry.first) == connected_ids.end();
  });

  struct CursorRecipient {
    std::string remote_id;
    bool include_position = true;
  };
  std::vector<CursorRecipient> recipients;
  for (const auto& remote_id : connected_controllers) {
    auto& delivery = cursor_delivery_states_[remote_id];
    const auto input_it = last_input_times.find(remote_id);
    const bool suppress_position =
        input_it != last_input_times.end() &&
        now - input_it->second < kCursorEchoSuppressionInterval;
    delivery.feedback_pending =
        delivery.feedback_pending || suppress_position;

    const CursorState& previous = delivery.last_sent;
    const bool shape_changed =
        !delivery.has_sent || sampled.render_mode != previous.render_mode ||
        sampled.hidden_reason != previous.hidden_reason ||
        (sample_ok &&
         (sampled.visible != previous.visible || sampled.shape != previous.shape ||
          std::abs(sampled.visual_offset_x - previous.visual_offset_x) >
              kCursorPositionEpsilon ||
          std::abs(sampled.visual_offset_y - previous.visual_offset_y) >
              kCursorPositionEpsilon));
    // Preserve sub-point cursor motion. At 10x client zoom, the old roughly
    // one-logical-pixel threshold became several visible phone points.
    const bool position_changed =
        sample_ok &&
        (!delivery.has_sent || sampled.position_valid != previous.position_valid ||
         sampled.display_id != previous.display_id ||
         (sampled.position_valid && previous.position_valid &&
          (std::abs(sampled.x - previous.x) > kCursorPositionEpsilon ||
           std::abs(sampled.y - previous.y) > kCursorPositionEpsilon)));
    const bool position_update_due =
        position_changed &&
        (delivery.last_sent_time.time_since_epoch().count() == 0 ||
         now - delivery.last_sent_time >= kCursorPositionShareInterval);
    const bool heartbeat_due =
        delivery.last_sent_time.time_since_epoch().count() == 0 ||
        now - delivery.last_sent_time >= kCursorStateHeartbeatInterval;
    // Position feedback waits for a good sample and the echo-suppression
    // interval. Appearance changes and heartbeats are independent of both.
    const bool include_position = sample_ok && !suppress_position;
    if (shape_changed || heartbeat_due ||
        (include_position && (delivery.feedback_pending || position_update_due))) {
      recipients.push_back({remote_id, include_position});
    }
  }
  if (recipients.empty()) return;

  sampled.seq = ++cursor_state_sequence_;
  for (const auto& recipient : recipients) {
    auto& delivery = cursor_delivery_states_[recipient.remote_id];
    CursorState outgoing = sampled;
    if (!sample_ok) {
      // Explicitly report unknown to new receivers, but retain the last
      // presentation for peers that only understand visible/shape.
      outgoing = delivery.has_sent ? delivery.last_sent : CursorState{};
      if (!delivery.has_sent) {
        outgoing.visible = true;
        outgoing.shape = RemoteCursorShape::default_cursor;
      }
      outgoing.seq = sampled.seq;
      outgoing.render_mode = sampled.render_mode;
      outgoing.hidden_reason = sampled.hidden_reason;
    }
    outgoing.position_update = recipient.include_position;
    if (!recipient.include_position) {
      // Keep legacy receivers at their last acknowledged position as well.
      // New receivers honor position_update=false and leave their optimistic
      // local position untouched.
      outgoing.position_valid =
          delivery.has_sent && delivery.last_sent.position_valid;
      outgoing.x = delivery.has_sent ? delivery.last_sent.x : 0.5f;
      outgoing.y = delivery.has_sent ? delivery.last_sent.y : 0.5f;
      outgoing.display_id = delivery.has_sent
                                ? delivery.last_sent.display_id
                                : -1;
    }

    RemoteAction action{};
    action.type = ControlType::cursor_state;
    action.cs = outgoing;
    const std::string message = action.to_json();
    const int result = SendControllerData(recipient.remote_id, message.c_str(),
                                          message.size(), mouse_label_.c_str());
    if (result != 0) {
      LOG_WARN("Send cursor state failed, ret={}", result);
      continue;
    }

    if (!delivery.has_sent ||
        delivery.last_sent.render_mode != outgoing.render_mode ||
        delivery.last_sent.hidden_reason != outgoing.hidden_reason) {
      LOG_INFO("Send cursor presentation: mode={}, reason={}",
               static_cast<int>(outgoing.render_mode),
               static_cast<int>(outgoing.hidden_reason));
    }
    delivery.last_sent = outgoing;
    delivery.has_sent = true;
    if (recipient.include_position) {
      delivery.feedback_pending = false;
    }
    delivery.last_sent_time = now;
  }
}

bool GuiApplication::RequestPasswordChange(const std::string &password) {
#ifdef _WIN32
  if (managed_host_frontend_) {
    if (password.size() != 6 || password.find_first_not_of(
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos)
      return false;
    if (!SendLocalHostCommand({{"op", "password"}, {"password", password}})) {
      offline_warning_text_ = localization::signal_disconnected[localization_language_index_];
      show_offline_warning_window_ = true;
    }
    return true;
  }
#endif
  const auto notify = [](const std::string& message) {
#if defined(__linux__) && !defined(__OHOS__)
    HeadlessConsole::Instance().NotifyKey(message.c_str());
#else
    (void)message;
#endif
  };
  if (password.size() != 6 ||
      password.find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") !=
          std::string::npos) {
    notify("console_password_invalid");
    return false;
  }

  if (!peer_ || !signal_connected_) {
    offline_warning_text_ =
        localization::signal_disconnected[localization_language_index_];
    show_offline_warning_window_ = true;
    notify("console_password_offline");
    return true;
  }

  std::string request_id;
  {
    std::lock_guard<std::mutex> lock(password_change_mutex_);
    if (password_change_pending_) {
      notify("console_password_pending");
      return true;
    }
    request_id =
        CreatePasswordChangeRequestId(++next_password_change_request_id_);
    password_change_pending_ = true;
    password_change_result_ready_ = false;
    password_change_succeeded_ = false;
    password_change_result_uncertain_ = false;
    password_change_recovery_ = false;
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
  const std::string pending_identity = std::string(client_id_) + "@" + password;
  if (!settings_.StagePendingPasswordChange(
          pending_identity, self_hosted, server_host, server_port, request_id)) {
    std::lock_guard<std::mutex> lock(password_change_mutex_);
    password_change_pending_ = false;
    pending_password_change_request_id_.clear();
    pending_local_password_.clear();
    password_change_error_.clear();
    offline_warning_text_ = localization::failed[localization_language_index_];
    show_offline_warning_window_ = true;
    LOG_ERROR("Could not durably stage password change");
    notify("console_password_stage_failed");
    return true;
  }

  const nlohmann::json request = {{"type", "change_password"},
                                {"request_id", request_id},
                                {"new_password", password}};
  const std::string message = request.dump();
  if (PeerSendSignalMessage(peer_, message.data(), message.size()) != 0) {
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
    notify("console_password_send_failed");
  } else {
    notify("console_password_submitted");
  }
  return true;
}

void GuiApplication::HandlePasswordChangeResult() {
  if (managed_host_frontend_) return;
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
    password_change_recovery_ = false;
    pending_password_change_request_id_.clear();
    pending_local_password_.clear();
    password_change_error_.clear();
  }

  if (!succeeded) {
#if defined(__linux__) && !defined(__OHOS__)
    HeadlessConsole::Instance().NotifyKey(
        uncertain ? "console_password_uncertain" : "console_password_rejected");
#endif
    if (uncertain) {
      LOG_WARN("Password change outcome is unknown; re-authenticating");
    } else {
      LOG_WARN("Password change failed");
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
      CloseConnectionPeer();
    }
    return;
  }

  if (!settings_.PromotePendingPasswordChange()) {
    // The pending credential was persisted before the request was sent, so it
    // remains sufficient for recovery even when promotion cannot be written.
    LOG_WARN("Password changed on server; pending recovery record retained");
  }

  LOG_INFO("Password changed successfully");
#if defined(__linux__) && !defined(__OHOS__)
  HeadlessConsole::Instance().NotifyKey("console_password_confirmed");
#endif
  if (peer_) {
    CloseConnectionPeer();
  }
}

void GuiApplication::HandleCredentialRecovery() {
  if (managed_host_frontend_) return;
  CredentialRecoveryAction action;
  {
    std::lock_guard<std::mutex> lock(password_change_mutex_);
    action = std::exchange(credential_recovery_action_, CredentialRecoveryAction::Idle);
    if (action == CredentialRecoveryAction::RetryActive) {
      credential_recovery_attempt_pending_ = false;
    }
  }

  if (action == CredentialRecoveryAction::ResumePending) {
    // Key authentication proves device ownership, not which password committed.
    // Replay the durable request and promote only its successful server reply.
    const bool self_hosted = config_center_->IsSelfHosted();
    std::string request_id;
    const auto identity = settings_.PendingPasswordChangeIdentity(self_hosted,
        self_hosted ? config_center_->GetSignalServerHost() : config_center_->GetDefaultServerHost(),
        self_hosted ? config_center_->GetSignalServerPort() : config_center_->GetDefaultSignalServerPort(),
        &request_id);
    const auto at = identity.find('@');
    if (!peer_ || !signal_connected_ || at == std::string::npos || request_id.empty() ||
        identity.substr(0, at) != client_id_) return;
    const auto password = identity.substr(at + 1);
    {
      std::lock_guard lock(password_change_mutex_);
      if (password_change_pending_) return;
      password_change_pending_ = true;
      password_change_result_ready_ = false;
      password_change_succeeded_ = false;
      password_change_result_uncertain_ = false;
      password_change_recovery_ = true;
      password_change_requested_at_ = std::chrono::steady_clock::now();
      pending_password_change_request_id_ = request_id;
      pending_local_password_ = password;
      password_change_error_.clear();
    }
    const auto message = nlohmann::json{{"type", "change_password"},
        {"request_id", request_id}, {"new_password", password}}.dump();
    if (PeerSendSignalMessage(peer_, message.data(), message.size()) != 0) {
      std::lock_guard lock(password_change_mutex_);
      password_change_result_ready_ = true;
      password_change_result_uncertain_ = true;
      password_change_error_ = "Password recovery connection unavailable";
    }
    return;
  }

  if (action == CredentialRecoveryAction::RetryActive) {
    LOG_INFO("Pending credential was not accepted; retrying active credential");
    if (peer_) {
      CloseConnectionPeer();
    }
    return;
  }

  if (action == CredentialRecoveryAction::PromotePending) {
#if defined(__linux__) && !defined(__OHOS__)
    HeadlessConsole::Instance().NotifyKey("console_password_recovered");
#endif
    if (!settings_.PromotePendingPasswordChange()) {
      LOG_WARN("Recovered credential is active but promotion remains pending");
    } else {
      LOG_INFO("Recovered password change with pending credential");
    }
  } else if (action == CredentialRecoveryAction::ClearPending) {
#if defined(__linux__) && !defined(__OHOS__)
    HeadlessConsole::Instance().NotifyKey("console_password_retained");
#endif
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
#if _WIN32
  const bool available = SupportsServiceAutostart();
  for (const auto& main : {ui_->main, ui_->wall}) {
    main->set_service_autostart_platform(true);
    main->set_autostart_available(available);
    main->set_service_autostart_hint(UiText(localization::Literal(available
        ? "开机后无需登录即可连接；登录后继续使用同一 ID、密码和完整功能。"
        : "开机自启需要安装版 Release；便携版和 Debug 不支持无人值守服务。", language)));
  }
#endif
  about::SetInstalledReleaseNotes(ui_->main, localization::GetSupportedLanguages()[language].code);
  ui_localization::ApplyMainWindowStrings(ui_->main, language);
  ui_localization::ApplyServerStrings(ui_->main, language);
  ui_localization::ApplyStreamWindowStrings(ui_->main, language);
  ui_localization::ApplyMainWindowStrings(ui_->wall, language);
  ui_localization::ApplyStreamWindowStrings(ui_->wall, language);
  for (auto& [_, view] : ui_->streams)
    ui_localization::ApplyStreamWindowStrings(view.window, language);
  for (auto& [_, browser] : ui_->file_browsers)
    browser->UpdateActionLabels(language);
  ui_->localized_language = language;
}

void GuiApplication::SyncAnnouncements() {
  if (signal_connected_) {
    announcements_.Configure(params_.signal_server_ip,
                             params_.signal_server_port, client_id_);
  }
  const auto request = announcements_.TakePendingRequest();
  if (!request.is_null()) {
    const auto payload = request.dump();
    if (!peer_ || PeerSendSignalMessage(peer_, payload.data(), payload.size()) != 0)
      announcements_.Failed(request["request_id"]);
  }
  if (ui_->announcement_version == announcements_.Version()) return;
  const auto state = announcements_.Get();
  ui_->announcement_version = state.version;
  for (const auto& main : {ui_->main, ui_->wall}) {
    main->set_announcement_unread(state.unread);
    main->set_announcement_total(state.total);
    main->set_announcement_loading(state.loading);
    main->set_announcement_failed(state.failed);
    main->set_announcement_read_save_failed(state.read_save_failed);
    main->set_announcement_delete_failed(state.delete_failed);
    main->set_announcement_loaded(state.loaded);
    std::vector<ui::AnnouncementItem> rows;
    bool selected_found = false;
    for (const auto& item : state.items) {
      ui::AnnouncementItem row;
      const auto id = std::to_string(item["id"].get<int64_t>());
      const auto revision = std::to_string(item["revision"].get<int64_t>());
      row.id = UiText(id);
      row.revision = UiText(revision);
      row.title = UiText(item["title"].get<std::string>());
      const auto timestamp =
          static_cast<time_t>(item["updated_at"].get<int64_t>());
      std::tm local{};
  #ifdef _WIN32
      localtime_s(&local, &timestamp);
  #else
      localtime_r(&timestamp, &local);
  #endif
      char date[32]{};
      std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M", &local);
      row.date = UiText(date);
      row.read = item["read"].get<bool>();
      if (id == ui_->announcement_selected_id &&
          revision == ui_->announcement_selected_revision) {
        selected_found = true;
        main->set_announcement_selected_id(row.id);
        main->set_announcement_selected_revision(row.revision);
        main->set_announcement_selected_title(row.title);
        main->set_announcement_selected_body(
            announcement_text::Format(item["body"].get<std::string>()));
        main->set_announcement_selected_date(row.date);
        main->set_announcement_selected_read(row.read);
      }
      rows.push_back(std::move(row));
    }
    if (!selected_found) {
      ui_->announcement_selected_id.clear();
      main->set_announcement_selected_id("");
      main->set_announcement_selected_revision("");
      main->set_announcement_selected_body(slint::StyledText{});
    }
    main->set_announcements(
        std::make_shared<slint::VectorModel<ui::AnnouncementItem>>(
            std::move(rows)));
  }
}

void GuiApplication::SyncMainWindow() {
  if (!ui_) {
    return;
  }
  SyncAnnouncements();
#if _WIN32
  ConfigureWindowsWindowIcons(ui_->main->window().win32_hwnd());
#endif
#if defined(__APPLE__)
  if (ui_->main_native_titlebar_attempts > 0) {
    if (ConfigureMainWindowChrome(ui_->main->window().appkit_view())) {
      ui_->main_native_titlebar_attempts = 0;
    } else {
      --ui_->main_native_titlebar_attempts;
    }
  }
  if (ui_->gallery_native_titlebar_attempts > 0) {
    if (ConfigureMainWindowChrome(ui_->wall->window().appkit_view(), true))
      ui_->gallery_native_titlebar_attempts = 0;
    else
      --ui_->gallery_native_titlebar_attempts;
  }
#endif
  if (re_enter_remote_id_) {
    ui_->main->invoke_reset_remote_id();
    re_enter_remote_id_ = false;
  }
  ui_->main->set_local_id(UiText(FormatPeerId(client_id_)));
  ui_->main->set_local_password(password_saved_);
  ui_->main->set_password_visible(show_password_);
  {
    std::lock_guard<std::mutex> lock(password_change_mutex_);
    ui_->main->set_password_change_pending(password_change_pending_);
  }
  const bool signal_online = signal_connected_.load();
  const bool signal_reconnecting = signal_connection_indicator_.Update(signal_online);
  for (const auto& main : {ui_->main, ui_->wall}) {
#if defined(__OHOS__)
    // Only the operating system owns this setting. Never bind it to the local
    // saved checkbox or wait for an ArkTS request in this 16 ms GUI tick.
    switch (GetHarmonyAutostartStatus()) {
      case HarmonyAutostartStatus::checking: main->set_autostart_status(ui::AutostartStatus::Checking); break;
      case HarmonyAutostartStatus::enabled: main->set_autostart_status(ui::AutostartStatus::Enabled); break;
      case HarmonyAutostartStatus::disabled: main->set_autostart_status(ui::AutostartStatus::Disabled); break;
      case HarmonyAutostartStatus::unsupported: main->set_autostart_status(ui::AutostartStatus::Unsupported); break;
      case HarmonyAutostartStatus::failed: main->set_autostart_status(ui::AutostartStatus::Failed); break;
    }
#endif
    main->set_signal_connected(signal_online);
    main->set_signal_reconnecting(signal_reconnecting);
    main->set_signal_tls_error(signal_status_ ==
                                    MiniRtcSignalStatus::MiniRtcSignalTlsCertError);
    main->set_update_available(update_available_);
    switch (update_checker_->status()) {
      case UpdateChecker::Status::Idle:
        main->set_update_check_status(ui::UpdateCheckStatus::Idle);
        break;
      case UpdateChecker::Status::Checking:
        main->set_update_check_status(ui::UpdateCheckStatus::Checking);
        break;
      case UpdateChecker::Status::UpToDate:
        main->set_update_check_status(ui::UpdateCheckStatus::UpToDate);
        break;
      case UpdateChecker::Status::Available:
        main->set_update_check_status(ui::UpdateCheckStatus::Available);
        break;
      case UpdateChecker::Status::Failed:
        main->set_update_check_status(ui::UpdateCheckStatus::Failed);
        break;
    }
    main->set_current_version(CROSSDESK_VERSION);
    main->set_latest_version(UiText(latest_version_));
    main->set_release_name(UiText(release_name_));
    main->set_release_date(UiText(release_date_));
    main->set_settings_session_active(HasActiveSession());
  }
  if (show_offline_warning_window_) {
    ui_->main->set_offline_warning(UiText(offline_warning_text_));
    show_offline_warning_window_ = false;
  }

  // Keep this filter separate from DeskHub's current group, keyed by name so
  // adding/removing groups does not silently select a different one by index.
  const auto tr_group = [this](const char* zh) {
    return UiText(localization::Literal(zh, localization_language_index_).c_str());
  };
  std::vector<slint::SharedString> recent_groups{
      tr_group("全部设备"),
      tr_group("未分组")};
  const bool lan_mode = ui_->main->get_lan_mode();
  int recent_group_index = 0;
  if (!lan_mode && ui_->recent_group_filter) {
    if (ui_->recent_group_filter->empty()) recent_group_index = 1;
    else {
      const auto found = std::find(ui_->gallery.groups.begin(),
                                  ui_->gallery.groups.end(), *ui_->recent_group_filter);
      if (found == ui_->gallery.groups.end()) ui_->recent_group_filter.reset();
      else recent_group_index = int(found - ui_->gallery.groups.begin()) + 2;
    }
  }
  ui_->recent_filter_groups = ui_->gallery.groups;
  for (const auto& group : ui_->recent_filter_groups)
    recent_groups.push_back(UiText(group));
  const auto old_recent_groups = ui_->main->get_recent_groups();
  bool recent_groups_changed = !old_recent_groups ||
      old_recent_groups->row_count() != recent_groups.size();
  if (!recent_groups_changed)
    for (size_t i = 0; i < recent_groups.size(); ++i)
      recent_groups_changed |= old_recent_groups->row_data(i) != recent_groups[i];
  if (recent_groups_changed)
    ui_->main->set_recent_groups(
        std::make_shared<slint::VectorModel<slint::SharedString>>(recent_groups));
  ui_->main->set_recent_group_index(recent_group_index);

  // Derive group labels from the shared workspace, including devices with
  // multiple memberships. No separate grouping metadata is stored in history.
  std::unordered_map<std::string, std::string> group_labels;
  for (const auto& member : ui_->gallery.members) {
    if (member.group.empty()) continue;
    auto& label = group_labels[member.id];
    if (!label.empty()) label += " · ";
    label += member.group;
  }
  const auto is_connected = [this](const std::string& id) {
    const auto props = FindRemoteSession(id);
    return props && props->connection_status_ == MiniRtcConnectionStatus::MiniRtcConnectionConnected &&
           !props->closing_;
  };
  const auto preview_size = ui_->recent_preview_resolution.Update(
      gallery::PreviewResolution::Viewport(
          ui_->main->get_recent_preview_width(),
          ui_->main->get_recent_preview_height(),
          ui_->main->window().scale_factor()),
      gallery::PreviewResolutionDebouncer::Clock::now());
  const bool save_previews = config_center_->IsSaveRemotePreviews();
  if (!(preview_size == ui_->recent_preview_size) || !save_previews) {
    ui_->recent_preview_size = preview_size;
    ui_->recent_preview_images.clear();
  }
  std::ostringstream signature_builder;
  signature_builder << save_previews << '\n'
                    << preview_size.width << ':' << preview_size.height << '\n'
                    << lan_mode << '\n'
                    << bool(ui_->recent_group_filter) << '\0'
                    << ui_->recent_group_filter.value_or("") << '\n';
  for (const auto& [key, connection] : recent_connections_) {
    signature_builder << key << '\0' << connection.remote_id << '\0'
                      << connection.lan.address << ':' << connection.lan.port << '\0'
                      << connection.remote_host_name << '\0'
                      << static_cast<int>(connection.platform) << '\0'
                      << settings_.RecentConnectionDisplayName(connection)
                      << '\0'
                      << group_labels[connection.remote_id] << '\0'
                      << ui_->gallery.MatchesGroup(connection.remote_id,
                                                   ui_->recent_group_filter) << '\0'
                      << device_presence_cache_.IsOnline(connection.remote_id)
                      << '\0' << is_connected(connection.remote_id)
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
    if (lan_mode != !connection.lan.address.empty()) continue;
    if (!lan_mode &&
        !ui_->gallery.MatchesGroup(connection.remote_id, ui_->recent_group_filter))
      continue;
    // Slint-generated scalar fields have no defaults. With no image,
    // has_thumbnail must remain false so the remote platform is rendered.
    ui::RecentConnection item{};
    item.remote_id = UiText(connection.remote_id);
    item.host_name = UiText(connection.remote_host_name);
    item.group_names = UiText(group_labels[connection.remote_id]);
    item.display_name =
        UiText(settings_.RecentConnectionDisplayName(connection));
    item.lan = !connection.lan.address.empty();
    item.endpoint =
        UiText(item.lan ? lan::FormatEndpoint(connection.lan.address,
                                              connection.lan.port)
                        : "");
    item.online =
        !item.lan && device_presence_cache_.IsOnline(connection.remote_id);
    item.connected = is_connected(connection.remote_id);
    item.platform = UiText(HostPlatformName(connection.platform));
    if (save_previews && !connection.image_path.empty()) {
      const auto path = connection.image_path.string();
      auto cached = ui_->recent_preview_images.find(path);
      if (cached == ui_->recent_preview_images.end()) {
        std::vector<unsigned char> rgba;
        int image_width = 0, image_height = 0;
        if (thumbnail_->DecodeImage(connection.image_path, preview_size.width,
                                    preview_size.height, &rgba, &image_width,
                                    &image_height)) {
          slint::SharedPixelBuffer<slint::Rgba8Pixel> pixels(image_width,
                                                           image_height);
          std::memcpy(pixels.begin(), rgba.data(), rgba.size());
          cached = ui_->recent_preview_images.emplace(
              path, slint::Image(std::move(pixels))).first;
        }
      }
      if (cached != ui_->recent_preview_images.end()) {
        item.thumbnail = cached->second;
        item.has_thumbnail = true;
      }
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
  const bool gallery_dialog = !ui_->gallery_auth_id.empty();
  const auto props = show_connection_status_window_
      ? FindRemoteSession(gallery_dialog ? ui_->gallery_auth_id : connection_dialog_target_)
      : nullptr;
  if (!props || props->connection_status_ == MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
    if (props) ui_->gallery_auth_id.clear();
    show_connection_status_window_ = false;
    for (const auto& window : {ui_->main, ui_->wall}) {
      window->set_connection_pending(false);
      window->set_connection_dialog_open(false);
    }
    ui_->connection_password_initialized = false;
    return;
  }

  if (ui_->connection_dialog_gallery != gallery_dialog ||
      ui_->connection_dialog_remote_id != props->remote_id_)
    ui_->connection_password_initialized = false;
  ui_->connection_dialog_gallery = gallery_dialog;
  ui_->connection_dialog_remote_id = props->remote_id_;
  auto& dialog = ui_->Window(gallery_dialog);
  auto& other = ui_->Window(!gallery_dialog);
  other->set_connection_pending(false);
  other->set_connection_dialog_open(false);
  const MiniRtcConnectionStatus status = props->connection_status_.load();
  const auto auth = props->connection_auth_.Get();
  const bool password_required = auth.password_required ||
      (status == MiniRtcConnectionStatus::MiniRtcConnectionIncorrectPassword && auth.validating);
  const bool pending = !password_required &&
      (status == MiniRtcConnectionStatus::MiniRtcConnectionConnecting || status == MiniRtcConnectionStatus::MiniRtcConnectionGathering);
  std::string text;
  if (password_required) {
    text = auth.validating
               ? localization::validate_password[localization_language_index_]
           : auth.incorrect_password
               ? localization::reinput_password[localization_language_index_]
               : localization::input_password[localization_language_index_];
  } else if (auth.error == MiniRtcJoinFailureKind::Throttled) {
    text = localization::connection_auth_throttled[localization_language_index_];
  } else if (auth.error == MiniRtcJoinFailureKind::Busy) {
    text = localization::connection_service_busy[localization_language_index_];
  } else if (auth.error == MiniRtcJoinFailureKind::NotAuthenticated) {
    text = localization::connection_not_authenticated[localization_language_index_];
  } else {
    text = props->lan_session_ && !props->lan_session_->error.empty()
               ? props->lan_session_->error
               : ConnectionStatusText(status, localization_language_index_);
  }
  if (auth.retry_after > 0) {
    char retry_text[256];
    std::snprintf(retry_text, sizeof(retry_text),
        localization::connection_retry_after[localization_language_index_].c_str(),
        auth.retry_after);
    text += "\n" + std::string(retry_text);
  }

  dialog->set_connection_dialog_open(true);
  dialog->set_connection_status_text(UiText(text));
  dialog->set_connection_pending(pending);
  dialog->set_connection_password_required(password_required);
  dialog->set_connection_validating(auth.validating);
  if (password_required && !auth.validating &&
      !ui_->connection_password_initialized) {
    dialog->set_connection_password(props->remote_password_);
    dialog->set_connection_remember_password(props->remember_password_);
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
  ui_->wall->set_portable_service_settings_visible(true);
  ui_->wall->set_portable_service_installed(portable_service_installed_);
  ui_->main->set_portable_service_needs_repair(portable_service_needs_repair_);
  ui_->wall->set_portable_service_needs_repair(portable_service_needs_repair_);
  ui_->wall->set_portable_service_installing(
      state == PortableServiceInstallState::installing);
  ui_->main->set_portable_service_installing(
      state == PortableServiceInstallState::installing);
  ui_->main->set_portable_service_succeeded(
      state == PortableServiceInstallState::succeeded);
  const std::string* status = nullptr;
  if (state == PortableServiceInstallState::installing) {
    status = &(portable_service_needs_repair_
                   ? localization::windows_service_repairing
                   : localization::installing_windows_service)[localization_language_index_];
  } else if (state == PortableServiceInstallState::succeeded) {
    status = &(portable_service_needs_repair_
                   ? localization::windows_service_repair_success
                   : localization::windows_service_install_success)[localization_language_index_];
  } else if (state == PortableServiceInstallState::failed) {
    status = &(portable_service_needs_repair_
                   ? localization::windows_service_repair_failed
                   : localization::windows_service_install_failed)[localization_language_index_];
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
  const bool migration_running = mac_permission_migration_task_.valid();
  const bool permissions_pending = migration_running ||
      mac_permission_migration_failed_ || !mac_screen_recording_permission_granted_ ||
      !mac_accessibility_permission_granted_;
  show_request_permission_window_ = !mac_permission_dialog_dismissed_ &&
      (mac_permission_dialog_requested_ || permissions_pending);
  ui_->main->set_mac_permission_settings_visible(true);
  ui_->wall->set_mac_permission_settings_visible(true);
  ui_->main->set_permission_migration_running(migration_running);
  ui_->main->set_permission_migration_failed(mac_permission_migration_failed_);
  ui_->main->set_permission_dialog_open(show_request_permission_window_);
  ui_->main->set_screen_recording_granted(
      mac_screen_recording_permission_granted_);
  ui_->main->set_accessibility_granted(mac_accessibility_permission_granted_);
#else
  ui_->main->set_permission_dialog_open(false);
#endif
}

#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
void GuiApplication::SyncXWaylandWindowActivation() {
  if (!use_xwayland_gui_) {
    SyncStreamKeyboardFocus();
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
  const auto pid = active_window ? X11GetWindowProcessId(display, active_window) : std::nullopt;
  const auto title = active_window && (!pid || *pid == static_cast<unsigned long>(getpid()))
                         ? X11WindowTitle(display, active_window) : std::string{};
  ui_->main->set_window_active(title == "CrossDesk");
  const bool wall_active = title == std::string(ui_->wall->get_window_title());
  ui_->wall->set_window_active(wall_active);
  if (!wall_active && !ui_->gallery_control_id.empty()) ReleaseControl();
  for (auto& [_, view] : ui_->streams) {
    const bool active = view.Visible() && title == std::string(view.window->get_session_title());
    view.window->set_window_active(active);
    SetStreamKeyboardFocus(view, active);
  }
}
#endif

void GuiApplication::SetStreamKeyboardFocus(StreamView& view, bool focused) {
  if (ui_->releasing_input) return;
  const auto& props = view.session;
  const auto& id = props->remote_id_;
  const bool local_ui = view.window->get_local_input_active();
  focused = focused && view.Visible() && props->stream_window_open_ &&
      !props->closing_ && !local_ui &&
      props->connection_status_ == MiniRtcConnectionStatus::MiniRtcConnectionConnected;
#if defined(__APPLE__)
  focused = focused && IsStreamWindowActive(view.window->window().appkit_view());
#elif defined(_WIN32)
  focused = focused && GetForegroundWindow() == view.window->window().win32_hwnd();
#elif defined(__OHOS__)
  focused = focused && IsHarmonyWindowActive(view.window->window());
#else
  focused = focused && view.window->get_video_input_focused();
#endif
  if (!focused) {
    if (ui_->gallery_control_id.empty() && controlled_remote_id_ == id) {
      UpdateStreamKeyboardFocus(false, local_ui, focus_on_stream_window_,
          [this](bool preserve_local_ui) { ReleaseControl(preserve_local_ui); });
    }
    return;
  }
  if (focus_on_stream_window_ && controlled_remote_id_ == id &&
      start_keyboard_capturer_ == props->control_mouse_) return;
  if (!controlled_remote_id_.empty()) ReleaseControl();
  controlled_remote_id_ = id;
  focus_on_stream_window_ = true;
  start_keyboard_capturer_ = props->control_mouse_;
  {
    std::unique_lock lock(remote_sessions_mutex_);
    for (const auto& [_, session] : remote_sessions_)
      session->last_focused_stream_ = session == props;
    props->clipboard_allowed_ = props->control_mouse_;
  }
  if (props->control_mouse_) clipboard_.HandleLocalUpdate();
  SyncAudioPlayback();
}

bool GuiApplication::AcceptStreamInput(StreamView& view) {
  SetStreamKeyboardFocus(view, true);
  return !ui_->releasing_input && focus_on_stream_window_ && view.session->control_mouse_ &&
         controlled_remote_id_ == view.session->remote_id_;
}

void GuiApplication::SyncStreamKeyboardFocus() {
#if defined(__OHOS__)
  ui_->main->set_window_active(IsHarmonyWindowActive(ui_->main->window()));
  ui_->wall->set_window_active(IsHarmonyWindowActive(ui_->wall->window()));
#endif
  if (!ui_->gallery_control_id.empty()) {
#if defined(__APPLE__)
    if (!IsGalleryWindowActive()) ReleaseControl();
#elif defined(_WIN32)
    if (GetForegroundWindow() != ui_->wall->window().win32_hwnd()) ReleaseControl();
#elif defined(__OHOS__)
    if (!IsHarmonyWindowActive(ui_->wall->window())) ReleaseControl();
#endif
  }
  for (auto& [_, view] : ui_->streams) {
#if defined(__OHOS__)
    view.window->set_window_active(IsHarmonyWindowActive(view.window->window()));
#endif
    SetStreamKeyboardFocus(view, true);
  }
}

GuiApplication::StreamView* GuiApplication::FindStreamView(const std::string& id) {
  if (!ui_) return nullptr;
  auto it = ui_->streams.find(id);
  return it == ui_->streams.end() ? nullptr : &it->second;
}

void GuiApplication::SyncStreamWindows() {
  std::map<std::string, std::shared_ptr<RemoteSession>> open;
  {
    std::shared_lock lock(remote_sessions_mutex_);
    for (const auto& [id, session] : remote_sessions_)
      if (!session->closing_ && session->stream_window_open_ && session->was_connected_)
        open.emplace(id, session);
  }
  for (auto it = ui_->streams.begin(); it != ui_->streams.end();) {
    auto session = open.find(it->first);
    if (session != open.end() && session->second == it->second.session) {
      ++it;
      continue;
    }
    if (ui_->gallery_control_id.empty() && controlled_remote_id_ == it->first)
      ReleaseControl();
    it->second.SaveSize(*config_center_);
    it = ui_->streams.erase(it);
  }
  for (const auto& [id, session] : open) {
    auto [it, created] = ui_->streams.try_emplace(id, session);
    if (created) InitializeStreamWindow(it->second);
    SyncStreamWindow(it->second);
  }
}

void GuiApplication::InitializeStreamWindow(StreamView& view) {
  auto& stream = view.window;
  if (const auto& direct = view.session->lan_session_) {
    view.endpoint = lan::FormatEndpoint(direct->target.address, direct->target.port);
    // LAN permits simultaneous connections to the same address. Give each
    // window a stable title even when the other connection closes first.
    for (const auto& [_, other] : ui_->streams)
      if (&view != &other && other.endpoint == view.endpoint)
        view.endpoint_window_number = std::max(view.endpoint_window_number,
                                               other.endpoint_window_number + 1);
  } else {
    view.endpoint = FormatPeerId(view.session->remote_id_.c_str());
  }
#if defined(__linux__) && !defined(__OHOS__)
  stream->window().set_xdg_app_id("crossdesk-remote-control");
  stream->set_custom_titlebar(use_xwayland_gui_);
#endif
  stream->set_control_hand_cursor_supported(SupportsControlHandCursor(ui_->main->window()));
  RegisterFontAwesome(stream->window());
  ui_localization::ApplyStreamWindowStrings(stream, localization_language_index_);
  stream->set_displays(view.display_model);
  stream->set_remote_resolutions(view.resolution_model);
  stream->set_stats_rows(view.stats_model);
  BindStreamCallbacks(view);
  view.presenter.PrepareWindow(stream, [this, &view] {
    NotifyVideoFrameReady(*view.session);
  });
  view.session->SetVideoRenderer(view.renderer);
  if (stream->get_custom_titlebar()) view.normal_size.height += kWaylandTitlebarLogicalHeight;
  if (auto saved = config_center_->GetStreamWindowSize(view.session->device_preference_key_))
    view.normal_size = *saved;
  stream->window().set_size(slint::LogicalSize({
      static_cast<float>(view.normal_size.width), static_cast<float>(view.normal_size.height)}));
}

void GuiApplication::SyncStreamWindow(StreamView& view) {
  auto& stream = view.window;
  const auto& props = view.session;
  const auto name = SessionDisplayName(props->remote_id_);
  auto title = "CrossDesk — " + (name.empty() ? view.endpoint : name + " (" + view.endpoint + ")");
  if (view.endpoint_window_number > 1) title += " #" + std::to_string(view.endpoint_window_number);
  stream->set_session_title(UiText(title));
#if _WIN32
  ConfigureWindowsStreamTaskbar(stream->window().win32_hwnd());
  ConfigureWindowsWindowIcons(stream->window().win32_hwnd());
#elif defined(__APPLE__)
  if (view.chrome_attempts > 0) {
    if (ConfigureStreamWindowLiveResize(stream->window().appkit_view(),
            [weak = slint::ComponentWeakHandle(stream)](bool active) {
              if (auto handle = weak.lock()) {
                (*handle)->set_fullscreen_transitioning(active);
                return (*handle)->get_native_video_enabled();
              }
              return false;
            })) {
      view.chrome_attempts = 0;
    } else {
      --view.chrome_attempts;
    }
  }
#endif
  if (view.position_attempts > 0 && NativeWindowReady(stream->window())) {
    if (PositionWindowAtCenter(stream->window(), ui_->main->window(),
            view.normal_size.width, view.normal_size.height))
      --view.position_attempts;
    else
      view.position_attempts = 0;
    if (view.position_attempts == 0) stream->show();
  }
  const bool visible = view.Visible();
  if (view.presenter.EnsureAttached() || (visible && !view.was_visible)) {
    NotifyVideoFrameReady(*props);
  }
  view.was_visible = visible;
  const MiniRtcConnectionStatus status = props->connection_status_.load();
  bool remote_update_available = false;
  HostPlatform remote_platform = HostPlatform::Unknown;
  if (status == MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
    std::lock_guard lock(props->remote_version_mutex_);
    remote_platform = props->remote_platform_;
    // Wait for host_info: a version still in transit is not a legacy peer.
    if (props->remote_host_info_received_ && !props->remote_update_dismissed_) {
      remote_update_available = props->remote_app_version_.empty() ||
          (update_checker_->latest() &&
           AvailableAppUpdate(props->remote_app_version_,
                              *update_checker_->latest()).has_value());
    }
  }
  stream->set_remote_update_visible(remote_update_available);
  stream->set_secure_attention_supported(remote_platform == HostPlatform::Windows);
  stream->set_service_warning(UiText(
      status == MiniRtcConnectionStatus::MiniRtcConnectionConnected &&
              remote_platform == HostPlatform::Windows &&
              GetRemoteUnlockState(*props) == RemoteUnlockState::service_unavailable
          ? localization::remote_service_unavailable_message[localization_language_index_]
          : std::string{}));
  const bool waiting_for_frame =
      status == MiniRtcConnectionStatus::MiniRtcConnectionConnected &&
      !stream->get_has_frame();
  // Once the peer is connected, the frame-waiting hint replaces the
  // connection status. Keeping these states mutually exclusive also avoids a
  // transient overlap when both properties are synchronized in one tick.
  stream->set_status_text(UiText(
          status == MiniRtcConnectionStatus::MiniRtcConnectionConnected
              ? std::string{}
              : ConnectionStatusText(status, localization_language_index_)));
  stream->set_receiving_text(UiText(
          waiting_for_frame
              ? localization::receiving_screen[localization_language_index_]
              : std::string{}));
  stream->set_mouse_control_enabled(props->control_mouse_);
  {
    std::lock_guard lock(props->privacy_status_mutex_);
    const auto& status = props->privacy_status_;
    const uint64_t now = SDL_GetTicks();
    const auto controls = GetPrivacyControlState(
        status, props->privacy_status_received_, props->privacy_command_pending_,
        now - props->privacy_status_tick_, now - props->privacy_command_tick_);
    const bool connected =
        props->connection_status_.load() == MiniRtcConnectionStatus::MiniRtcConnectionConnected;
    stream->set_privacy_unlock_visible(connected && props->privacy_unlock_notice_);
    stream->set_privacy_can_lock(connected && props->privacy_desktop_available_ &&
        props->privacy_status_received_ && status.supported &&
        now - props->privacy_status_tick_ <= 5000 &&
        (!props->privacy_command_pending_ || now - props->privacy_command_tick_ >= 15000));
    stream->set_privacy_active(controls.active);
    stream->set_privacy_off(connected && controls.off);
    stream->set_privacy_unsupported(controls.unsupported);
    stream->set_privacy_visible(props->privacy_desktop_available_);
    stream->set_privacy_can_toggle(
        props->privacy_desktop_available_ && props->control_mouse_ &&
        connected && controls.can_toggle);
    const int language = localization_language_index_;
    std::string text = !connected || !props->privacy_status_received_
        ? localization::privacy_screen[language]
        : controls.unsupported ? localization::privacy_unsupported[language]
        : controls.enabled ? localization::privacy_on[language]
        : controls.off ? localization::privacy_off[language]
        : localization::privacy_screen[language];
    stream->set_privacy_status_text(UiText(text));
  }
  int remote_cursor_shape =
      static_cast<int>(RemoteCursorShape::default_cursor);
  bool remote_cursor_active = false;
  {
    std::lock_guard lock(props->remote_cursor_state_mutex_);
    remote_cursor_active =
        status == MiniRtcConnectionStatus::MiniRtcConnectionConnected && props->control_mouse_ &&
        props->remote_cursor_state_received_;
    if (remote_cursor_active) {
      remote_cursor_shape = static_cast<int>(
          props->cursor_presentation_.Resolve(SDL_GetTicks(), remote_platform));
    }
  }
  stream->set_remote_cursor_active(remote_cursor_active);
  stream->set_remote_cursor_shape(remote_cursor_shape);
  stream->set_audio_enabled(props->audio_capture_button_pressed_);
  {
    std::lock_guard lock(props->video_settings_mutex_);
    if (props->video_settings_pending_ &&
        SDL_GetTicks() - props->video_settings_request_tick_ > 5000) {
      props->video_settings_pending_ = false;
      props->video_settings_failed_ = true;
      const auto request_id = props->video_settings_.request_id;
      props->video_settings_ = props->applied_video_settings_;
      props->video_settings_.request_id = request_id;
    }
    stream->set_video_settings_enabled(status == MiniRtcConnectionStatus::MiniRtcConnectionConnected &&
                                     props->video_settings_supported_);
    stream->set_video_settings_feedback(UiText(
        props->video_settings_failed_
            ? localization::video_settings_failed[localization_language_index_] : ""));
    stream->set_video_quality_index(props->video_settings_.quality);
    stream->set_frame_rate_index(props->video_settings_.frame_rate == 30 ? 0 : 1);
    stream->set_adaptation_policy_index(props->video_settings_.preference);
  }
  view.ObserveWindowState();
  stream->set_stats_visible(props->net_traffic_stats_button_pressed_);

  std::vector<slint::SharedString> displays;
  {
    std::lock_guard lock(props->remote_version_mutex_);
    displays.reserve(props->display_info_list_.size());
    for (size_t index = 0; index < props->display_info_list_.size(); ++index) {
      displays.emplace_back(UiText(localization::FormatDisplayLabel(
          index, props->display_info_list_[index].name,
          localization_language_index_)));
    }
    if (props->selected_display_ >= int(displays.size())) props->selected_display_ = 0;
  }
  ui::UpdateModelRows(*view.display_model, std::move(displays));
  stream->set_selected_display(props->selected_display_);
  {
    std::lock_guard lock(props->display_resolution_mutex_);
    auto& state = props->display_resolution_;
    state.Expire(SDL_GetTicks());
    view.resolution_choices = {};
    int selected = -1;
    std::vector<slint::SharedString> resolutions;
    for (const auto& display : state.displays) {
      if (display.display_id != props->selected_display_) continue;
      view.resolution_choices = display;
      for (const auto& mode : display.modes) {
        if (mode == display.current) selected = int(resolutions.size());
        resolutions.emplace_back(UiText(FormatResolution(mode.width, mode.height)));
      }
      break;
    }
    const bool visible = status == MiniRtcConnectionStatus::MiniRtcConnectionConnected &&
                         !resolutions.empty();
    ui::UpdateModelRows(*view.resolution_model, std::move(resolutions));
    stream->set_remote_resolution_visible(visible);
    stream->set_remote_resolution_enabled(visible && !state.pending);
    stream->set_remote_resolution_index(selected);
    stream->set_remote_resolution_feedback(UiText(
        state.failed ? localization::video_settings_failed[localization_language_index_] : ""));
  }

  const auto net = props->net_traffic_stats_.Load();
  const auto codecs = props->net_traffic_stats_.LoadVideoCodecs();
  stream->set_stats_encoder(UiText(
      status == MiniRtcConnectionStatus::MiniRtcConnectionConnected && !codecs.remote_encoder.empty()
          ? codecs.remote_encoder : "—"));
  stream->set_stats_decoder(UiText(
      status == MiniRtcConnectionStatus::MiniRtcConnectionConnected && !codecs.local_decoder.empty()
          ? codecs.local_decoder : "—"));
  stream->set_stats_encryption_enabled(
      status == MiniRtcConnectionStatus::MiniRtcConnectionConnected && net.srtp_active);
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
  ui::UpdateModelRows(*view.stats_model, std::move(stats_rows));
  {
    std::lock_guard lock(props->video_frame_mutex_);
    stream->set_stats_resolution(UiText(FormatResolution(props->video_width_, props->video_height_)));
  }
  const auto mode = props->traversal_mode_.load();
  const bool direct = mode == MiniRtcTraversalP2P &&
                      status == MiniRtcConnectionConnected;
  stream->set_stats_connection_mode(UiText(mode == MiniRtcTraversalP2P
      ? localization::connection_mode_direct[localization_language_index_]
      : mode == MiniRtcTraversalRelay
          ? localization::connection_mode_relay[localization_language_index_] : "—"));
  stream->set_stats_local_endpoint(UiText(direct ? net.local_endpoint : ""));
  stream->set_stats_remote_endpoint(UiText(direct ? net.remote_endpoint : ""));

  const auto fps_now = std::chrono::steady_clock::now();
  bool refresh_stats = false;
  if (!props->net_traffic_stats_button_pressed_) {
    props->fps_ = 0;
    props->frame_count_ = 0;
    props->last_time_ = {};
  } else if (props->last_time_.time_since_epoch().count() == 0) {
    props->last_time_ = fps_now;
    refresh_stats = true;
  } else {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             fps_now - props->last_time_)
                             .count();
    if (elapsed >= 1000) {
      props->fps_ = static_cast<int>(props->frame_count_ * 1000 / elapsed);
      props->frame_count_ = 0;
      props->last_time_ = fps_now;
      refresh_stats = true;
    }
  }
  stream->set_stats_fps(UiText(std::to_string(props->fps_)));
  if (!props->net_traffic_stats_button_pressed_ ||
      status != MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
    props->video_latency_snapshot_.reset();
  } else if (refresh_stats) {
    props->video_latency_snapshot_ = props->video_latency_->Get(fps_now);
  }
  const auto& latency = props->video_latency_snapshot_;
  stream->set_stats_video_latency(UiText(
          latency ? std::to_string(
                        static_cast<int>(std::lround(latency->average_ms))) +
                        " ms"
                  : "—"));
  const auto display_rtt = props->net_traffic_stats_.SmoothedRttMs(fps_now);
  const bool rtt_available = props->net_traffic_stats_button_pressed_ &&
                            status == MiniRtcConnectionStatus::MiniRtcConnectionConnected &&
                            display_rtt.has_value();
  std::string connection_latency = "—";
  if (rtt_available) {
    connection_latency =
        *display_rtt < 1
            ? "<1 ms"
            : std::to_string(static_cast<int>(std::lround(*display_rtt))) + " ms";
  }
  stream->set_stats_connection_latency(UiText(connection_latency));
  if (refresh_stats && status == MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
    LOG_DEBUG("Latency stats: remote={} video_ms={} video_samples={} video_endpoint={} "
              "rtt_raw_ms={} rtt_display_ms={}",
              props->remote_id_, latency ? latency->average_ms : -1,
              latency ? latency->samples : 0,
              !latency ? "unavailable" : latency->estimated ? "estimated" : "presented",
              net.rtt_ms,
              display_rtt.value_or(-1));
  }
}

void GuiApplication::SyncVideoFrameGapStats() {
  const auto now = VideoLatencyFrame::Clock::now();
  const bool log = last_video_frame_gap_log_time_ ==
                       VideoLatencyFrame::Clock::time_point{} ||
      now - last_video_frame_gap_log_time_ >= std::chrono::seconds(5);
  if (log) last_video_frame_gap_log_time_ = now;
  std::shared_lock lock(remote_sessions_mutex_);
  for (const auto& [id, props] : remote_sessions_) {
    if (!props) continue;
    const bool paused = props->preview_subscription_supported_ &&
                        props->preview_requested_.fps == 0;
    const auto* view = FindStreamView(id);
    const bool active = view && view->Visible() && !props->closing_ && !paused &&
        props->connection_status_ == MiniRtcConnectionStatus::MiniRtcConnectionConnected;
    LogVideoFrameDiagnostics(id,
        props->video_latency_->SetFrameGapTrackingEnabled(
            active, props->selected_display_, now));
    if (active && log) {
      LogVideoFrameDiagnostics(id, props->video_latency_->TakeDiagnostics(now));
    }
  }
}

void GuiApplication::SyncStreamVideoFrame(
    StreamView& view, std::optional<double> timer_lateness_ms) {
  const auto& props = view.session;
  props->video_latency_->RecordRenderTick(timer_lateness_ms);

  SlintVideoPresenter::Frame frame;
  {
    std::lock_guard lock(props->video_frame_mutex_);
    frame.nv12 = props->front_frame_;
    frame.width = props->video_width_;
    frame.height = props->video_height_;
    frame.sequence = props->video_frame_sequence_;
    frame.timing = props->video_frame_timing_;
  }

  const auto result = view.presenter.Present(frame);
  if (result.width > 0 && result.height > 0) {
    view.window
        ->set_stats_resolution(UiText(FormatResolution(result.width, result.height)));
  }
  if (result.new_frame) {
    ++props->frame_count_;
  }
}

void GuiApplication::PresentPendingVideoFrame(std::optional<double> timer_lateness_ms) {
  for (auto& [_, view] : ui_->streams) {
    if (view.Visible() && (view.session->frame_dirty_.exchange(false) || view.presenter.NeedsRedraw()))
      SyncStreamVideoFrame(view, timer_lateness_ms);
  }
}

void GuiApplication::RequestVideoPresent() {
  if (!ui_) return;
  // A decoder thread can produce frames faster than the UI drains wakeups, and
  // the post itself allocates on the event queue. Coalesce to one in-flight
  // task; the newest frame is picked up when it runs because presentation reads
  // the session's latest frame, not a queued copy.
  if (video_present_queued_.exchange(true, std::memory_order_acq_rel)) return;
  const auto queued_at = std::chrono::steady_clock::now();
  // Safe to capture this: the task only runs while the event loop is running,
  // and Cleanup() (which resets ui_) runs after run_event_loop() returns.
  slint::invoke_from_event_loop([this, queued_at] {
    video_present_queued_.store(false, std::memory_order_release);
    if (exit_ || !ui_) return;
    const double queued_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - queued_at).count();
    for (auto& [_, view] : ui_->streams) {
      if (view.Visible() && view.session->frame_dirty_)
        view.session->video_latency_->RecordFrameWake(queued_ms);
    }
    PresentPendingVideoFrame();
  });
}

#if defined(__APPLE__)
void GuiApplication::UpdateVideoFrameScheduler() {
  if (!ui_->video_scheduler.IsRunning()) return;
  bool enabled = false, pending = false;
  for (auto& [_, view] : ui_->streams) {
    if (!view.Visible()) continue;
    enabled = true;
    pending |= view.session->frame_dirty_ || view.presenter.NeedsRedraw();
  }
  ui_->video_scheduler.SetEnabled(enabled);
  if (pending) ui_->video_scheduler.Request();
}
#endif

void GuiApplication::ScheduleNextVideoFrame() {
  if (!ui_) {
    return;
  }
#if defined(__APPLE__)
  // CAMetalLayer already synchronizes presentation. A second fixed-frequency
  // timer can replace uniformly arriving frames at the timer phase boundary.
  if (ui_->video_scheduler.IsRunning()) {
    ui_->video_timer.stop();
    return;
  }
#endif

  constexpr auto frame_interval = std::chrono::nanoseconds(1'000'000'000 / 60);
  const auto now = std::chrono::steady_clock::now();
  if (next_video_frame_time_ == std::chrono::steady_clock::time_point{}) {
    next_video_frame_time_ = now;
  }

  if (now >= next_video_frame_time_) {
    const double lateness_ms = std::chrono::duration<double, std::milli>(
        now - next_video_frame_time_).count();
    for (auto& [_, view] : ui_->streams) {
      if (!view.Visible()) continue;
      view.session->video_latency_->RecordSchedulerTick(
          lateness_ms, (now - next_video_frame_time_) / frame_interval);
    }
    // Measure a long stall before resynchronizing, without replaying missed
    // frames. Otherwise the most important scheduler delays appear as zero.
    if (now - next_video_frame_time_ > 250ms) next_video_frame_time_ = now;
    PresentPendingVideoFrame(lateness_ms);
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

void GuiApplication::BindTrustedDeviceCallbacks(bool gallery_window) {
  const auto main = ui_->Window(gallery_window);
  main->on_trust_action([this, main](
      slint::SharedString action, slint::SharedString id, slint::SharedString key) {
    const auto message = [this](const char* text) {
      return UiText(localization::Literal(text, localization_language_index_));
    };
    const bool copying = action == "copy";
    const auto set_error = [&](const char* text) {
      if (copying) main->set_trust_copy_error(message(text));
      else main->set_trusted_devices_error(message(text));
    };
    if (copying || action == "open" || action == "close") {
      main->set_trust_copy_error("");
      main->set_trust_copy_notice("");
    }
    if (!copying) {
      main->set_trusted_devices_error("");
      main->set_trusted_devices_notice("");
    }
    if (action == "open" || action == "close") {
      main->set_trust_pairing_input("");
      SyncTrustedDevices();
      return;
    }
    if (!trusted_devices_) {
      set_error("无法读取设备身份或可信列表，请修复后再启用。");
      return;
    }
    if (action == "edit") return;
    try {
      if (copying) {
        const auto text = trusted_devices_->PairingInfo(client_id_, GetHostName());
        if (SDL_SetClipboardText(text.c_str()))
          main->set_trust_copy_notice(message("配对信息已复制。"));
        else set_error("无法访问剪贴板，请重试。");
      } else if (action == "import") {
        const auto input = std::string(main->get_trust_pairing_input());
        if (input.empty()) return;
        const auto device = TrustedDevices::ParsePairingInfo(input);
        if (!device) {
          set_error("配对信息无效，请从控制端重新复制。");
          return;
        }
        // The Add action approves the current input. Import only updates the
        // list; a live connection must still complete its own authentication.
        const bool existing = trusted_devices_->Contains(device->public_key);
        if (existing ? PublishSharedIdentity() : AddTrustedDevice(*device)) {
          main->set_trusted_devices_notice(message(existing
              ? "已在可信列表中" : "已加入可信设备。"));
          main->set_trust_pairing_input("");
        } else set_error("无法更新可信设备，请检查列表容量和存储权限后重试。");
      } else if ((action == "add" && !TrustController(std::string(id), std::string(key))) ||
                 (action == "remove" && !RemoveTrustedDevice(std::string(key)))) {
        set_error("无法更新可信设备，请检查列表容量和存储权限后重试。");
      }
    } catch (const std::exception&) {
      set_error(copying ? "无法复制配对信息，请重试。"
                        : "无法更新可信设备，请检查列表容量和存储权限后重试。");
    }
    SyncTrustedDevices();
  });
}

void GuiApplication::SyncTrustedDevices() {
  if (!ui_->main->get_trusted_devices_open() && !ui_->wall->get_trusted_devices_open()) return;
  std::vector<ui::TrustedDeviceEntry> saved, candidates;
  if (trusted_devices_) {
    for (const auto& device : trusted_devices_->List()) saved.push_back(TrustRow(device));
    for (const auto& id : ConnectedControllers())
      if (const auto device = VerifiedController(id);
          device && !trusted_devices_->Contains(device->public_key))
        candidates.push_back(TrustRow(*device, id));
  }
  std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
    return std::string(a.remote_id) < std::string(b.remote_id);
  });
  std::string signature = trusted_devices_ ? "ready" : "unavailable";
  for (const auto* rows : {&saved, &candidates}) {
    signature += "|";
    for (const auto& device : *rows)
      signature += std::string(device.remote_id) + '\0' + std::string(device.public_key) + '\0' +
          std::string(device.display_name) + '\0' + std::string(device.device_id) + '\0';
  }
  if (signature != ui_->trusted_device_signature) {
    // Tick runs frequently. Hash keys and update Slint only when the rows change.
    for (auto* rows : {&saved, &candidates})
      for (auto& device : *rows)
        device.fingerprint = TrustFingerprint(TrustedDevices::Fingerprint(std::string(device.public_key)));
    ui_->trusted_device_model->set_vector(std::move(saved));
    ui_->trust_candidate_model->set_vector(std::move(candidates));
    ui_->trusted_device_signature = std::move(signature);
    const auto fingerprint = TrustFingerprint(trusted_devices_ ? trusted_devices_->Fingerprint() : "");
    for (auto& window : {ui_->main, ui_->wall}) {
      window->set_trusted_devices(ui_->trusted_device_model);
      window->set_trust_candidates(ui_->trust_candidate_model);
      window->set_device_fingerprint(fingerprint);
    }
  }
  if (!trusted_devices_) {
    for (auto& window : {ui_->main, ui_->wall})
      window->set_trusted_devices_error(UiText(localization::Literal(
          "无法读取设备身份或可信列表，请修复后再启用。", localization_language_index_)));
  }
}

void GuiApplication::SyncIncomingControllers() {
  if (!incoming_controllers_changed_.exchange(false)) return;
  std::vector<ui::ControllerEntry> controllers;
  {
    std::shared_lock lock(connection_status_mutex_);
    controllers.reserve(connection_status_.size());
    for (const auto& [id, status] : connection_status_) {
      if (status != MiniRtcConnectionStatus::MiniRtcConnectionConnected) continue;
      const auto host = connection_host_names_.find(id);
      const std::string name =
          host != connection_host_names_.end() && !host->second.empty()
              ? host->second : id;
      controllers.push_back({UiText(id), UiText(name)});
    }
  }
  // Stable ordering and model identity preserve radio selection, hover and
  // scrolling while the 16 ms UI timer reconciles asynchronous peer events.
  std::sort(controllers.begin(), controllers.end(), [](const auto& a, const auto& b) {
    return std::string(a.remote_id) < std::string(b.remote_id);
  });
  std::vector<std::string> ids;
  ids.reserve(controllers.size());
  for (const auto& controller : controllers) ids.emplace_back(controller.remote_id);
  if (ids != ui_->controller_ids) ui_->main->set_confirming_disconnect(false);
  if (ids.empty()) ui_->main->set_controller_panel_open(false);
  ui_->controller_ids = std::move(ids);
  ui::UpdateModelRows(*ui_->controller_model, std::move(controllers));
  const int selected = server_window_state::ReconcileSelectedController(
      ui_->controller_ids, &selected_server_remote_id_);
  ui_->main->set_selected_controller(selected);
}

void GuiApplication::CancelFileTransfer(uint32_t file_id) {
  using Status = RemoteSession::FileTransferStatus;
  const auto found = ui_->transfer_notifications.find(file_id);
  if (found == ui_->transfer_notifications.end()) return;
  auto& info = found->second.info;
  if (info.receiving || (info.status != Status::Queued && info.status != Status::Sending))
    return;
  // DeskHub tasks waiting for a concurrency slot have no sender yet.
  for (auto& operation : ui_->batch_operations) {
    if (operation.file_id == file_id &&
        operation.state == SlintUi::BatchOperation::State::Queued) {
      operation.state = SlintUi::BatchOperation::State::Finished;
      info.status = Status::Cancelled;
      info.rate_bps = 0;
      return;
    }
  }
  if (transfers_.CancelSend(file_id)) {
    info.status = Status::Cancelling;
    info.rate_bps = 0;
  }
}

std::string GuiApplication::SessionDisplayName(const std::string& remote) {
  Thumbnail::RecentConnection device{};
  device.remote_id = remote;
  if (const auto props = FindRemoteSession(remote)) {
    if (const auto direct = props->lan_session_) {
      device.remote_id = Thumbnail::LanRecordId(direct->target.address, direct->target.port);
    }
    std::lock_guard lock(props->remote_version_mutex_);
    device.remote_host_name = props->remote_host_name_;
  } else {
    std::shared_lock lock(connection_status_mutex_);
    if (const auto host = connection_host_names_.find(remote);
        host != connection_host_names_.end())
      device.remote_host_name = host->second;
  }
  if (device.remote_host_name.empty() || device.remote_host_name == "unknown") {
    for (const auto& [_, recent] : recent_connections_) {
      if (recent.remote_id == device.remote_id) {
        device.remote_host_name = recent.remote_host_name;
        break;
      }
    }
  }
  const auto name = settings_.RecentConnectionDisplayName(device);
  // Keep transport IDs out of the header while peer information is arriving.
  return name == device.remote_id ? std::string{} : name;
}

void GuiApplication::OpenFileManager(const std::string& remote) {
  if (!transfers_.OpenBrowser(remote)) return;
  if (!transfers_.BrowserSupported(remote)) {
    // Host information from older clients omits the capability. Use their
    // existing file protocol and picker directly, without a directory RPC.
    const auto generation = transfers_.browser().Generation(remote);
    OpenFileDialog(std::string(localization::select_file[localization_language_index_]),
        [this, remote, generation](std::string path) {
          if (path.empty() || !transfers_.BrowserConnected(remote) ||
              transfers_.browser().Generation(remote) != generation) return;
          const auto props = FindRemoteSession(remote);
          transfers_.ProcessSelectedFile(path, props,
              props ? props->file_label_ : file_label_, remote);
        });
    return;
  }
  auto& browser = ui_->file_browsers[remote];
  if (browser && !browser->Tick()) browser.reset();
  if (!browser) {
    const bool controller = static_cast<bool>(FindRemoteSession(remote));
    browser = std::make_unique<FileBrowserWindow>(transfers_, remote, controller,
        localization_language_index_);
  }
  browser->SetPeerName(SessionDisplayName(remote));
  browser->Show();
  SyncFileTransferNotifications();
}

void GuiApplication::SyncClipboardFileNotification() {
  using Status = file_transfer::ReceiveStatus;
  const auto snapshot = clipboard_.FileReceiveSnapshot();
  const auto hide = [this] {
    if (ui_->clipboard_notification) (*ui_->clipboard_notification)->hide();
    ui_->clipboard_notification.reset();
    ui_->clipboard_notification_id = 0;
    ui_->clipboard_notification_display = 0;
  };
  if (!snapshot) { hide(); return; }
  const auto& progress = snapshot->transfer;
  const bool active = progress.status == Status::Receiving;
  const bool completed = progress.status == Status::Completed;
  const bool failed = progress.status == Status::Failed;
  using OfferError = Clipboard::FileOfferError;
  const bool unavailable = snapshot->offer_error != OfferError::NoError;
  const auto now = std::chrono::steady_clock::now();
  if (!active && now - snapshot->updated > (failed ? 6s : 3s)) {
    clipboard_.DismissFileReceive(snapshot->id); hide(); return;
  }
  if (ui_->clipboard_notification_id != snapshot->id) {
    hide();
    // Avoid flashing a progress window for tiny selections; they go straight
    // to the short completion confirmation on the receiving computer.
    if (active && now - snapshot->started < 250ms) return;
    ui_->clipboard_notification.emplace(ui::ClipboardFileNotification::create());
    ui_->clipboard_notification_id = snapshot->id;
    auto& component = *ui_->clipboard_notification;
    RegisterFontAwesome(component->window());
    const auto action = [this, id = snapshot->id] {
      const auto current = clipboard_.FileReceiveSnapshot();
      if (!current || current->id != id) return;
      if (current->transfer.status == Status::Receiving) clipboard_.CancelFileReceive(id);
      else clipboard_.DismissFileReceive(id);
    };
    component->on_action_requested(action);
    component->window().on_close_requested([action] {
      action(); return slint::CloseRequestResponse::KeepWindowShown;
    });
    if (CanUseGlobalPointerPosition()) {
      float x = 0, y = 0;
      SDL_GetGlobalMouseState(&x, &y);
      const SDL_Point point{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
      ui_->clipboard_notification_display = SDL_GetDisplayForPoint(&point);
    }
  }
  auto& component = *ui_->clipboard_notification;
  const auto text = [this](const char* key) {
    return UiText(localization::Literal(key, localization_language_index_));
  };
  component->set_unavailable(unavailable);
  component->set_heading(text(unavailable ? "文件粘贴暂不可用"
                                     : active ? "正在接收剪贴板文件" : completed ? "剪贴板文件接收完成"
                                     : failed ? "剪贴板文件接收失败" : "已取消文件接收"));
  component->set_detail(UiText(progress.name));
  auto count = localization::Literal("{0} / {1} 个文件", localization_language_index_);
  for (const auto& [key, value] : {std::pair{"{0}", std::to_string(progress.completed_files)},
                                  std::pair{"{1}", std::to_string(progress.files)}})
    if (const auto pos = count.find(key); pos != std::string::npos) count.replace(pos, 3, value);
  component->set_summary(UiText(count + " · " + FormatFileSize(progress.received) +
                                " / " + FormatFileSize(progress.total)));
  if (unavailable) {
    const bool fuse_error = snapshot->offer_error == OfferError::UnavailableRuntime ||
                           snapshot->offer_error == OfferError::MountFailed;
    component->set_detail(fuse_error ? text("请检查 FUSE 安装和挂载权限。")
                                    : text("剪贴板文件接收失败"));
    component->set_summary(text("可使用文件传输窗口发送文件。"));
  }
  const float fraction = completed ? 1.0f : progress.total
      ? static_cast<float>(static_cast<double>(progress.received) / progress.total)
      : progress.files ? static_cast<float>(progress.completed_files) / progress.files : 0.0f;
  component->set_progress(std::clamp(fraction, 0.0f, 1.0f));
  component->set_active(active); component->set_completed(completed); component->set_failed(failed);
  component->set_action_label(active ? UiText(localization::cancel[localization_language_index_])
                                     : text("关闭"));
  SDL_Rect bounds{};
  if (!SDL_GetDisplayUsableBounds(ui_->clipboard_notification_display, &bounds)) {
    ui_->clipboard_notification_display = SDL_GetPrimaryDisplay();
    if (!SDL_GetDisplayUsableBounds(ui_->clipboard_notification_display, &bounds)) return;
  }
#if defined(__APPLE__)
  const float scale = 1;
#else
  const float scale = std::max(0.01f, SDL_GetDisplayContentScale(ui_->clipboard_notification_display));
#endif
  auto& window = component->window();
  const float width = std::min(344.0f, std::max(1.0f, bounds.w / scale - 32));
  const float window_scale = std::max(0.01f, window.scale_factor());
  const auto size = window.size();
  if (std::abs(size.width / window_scale - width) > 0.5f ||
      std::abs(size.height / window_scale - 112) > 0.5f)
    window.set_size(slint::LogicalSize({width, 112}));
  if (CanUseGlobalPointerPosition()) {
    const float x = bounds.x / scale + std::max(0.0f, bounds.w / scale - width - 16);
    const float y = bounds.y / scale + std::max(0.0f, bounds.h / scale - 128);
#if defined(__APPLE__)
    const auto position = window.position();
    if (std::abs(position.x / window_scale - x) > 0.5f ||
        std::abs(position.y / window_scale - y) > 0.5f)
      window.set_position(slint::LogicalPosition({x, y}));
#else
    const slint::PhysicalPosition target({static_cast<int32_t>(std::lround(x * scale)),
                                         static_cast<int32_t>(std::lround(y * scale))});
    const auto position = window.position();
    if (position.x != target.x || position.y != target.y) window.set_position(target);
#endif
  }
  if (window.is_visible() || !NativeWindowReady(window)) return;
#if _WIN32
  const auto configure = [&] {
    if (const HWND hwnd = window.win32_hwnd()) {
      SetWindowLongPtr(hwnd, GWL_EXSTYLE,
          (GetWindowLongPtr(hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) & ~WS_EX_APPWINDOW);
      SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
          SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
  };
  configure();
#endif
#if defined(__APPLE__)
  ShowFileTransferNotification(window.appkit_view(), [&window] { window.show(); });
#else
  component->show();
#endif
#if _WIN32
  configure();
#endif
}

void GuiApplication::SyncFileTransferNotifications() {
  using Status = RemoteSession::FileTransferStatus;
  using namespace transfer_notification;
  std::map<uint32_t, RemoteSession::FileTransferInfo> files;
  const auto collect = [&](FileTransferState& state) {
    std::lock_guard lock(state.file_transfer_list_mutex_);
    for (const auto& info : state.file_transfer_list_)
      if (!info.receiving) files.emplace(info.file_id, info);
  };
  collect(transfers_.global_state());
  {
    std::shared_lock lock(remote_sessions_mutex_);
    for (const auto& [_, props] : remote_sessions_) {
      if (props) collect(props->file_transfer_);
    }
  }
  // DeskHub reserves each recipient's notification before a transfer slot is
  // available. Keep its queued window (and dismissal) alive until dispatched.
  std::set<uint32_t> pending_files;
  for (const auto& operation : ui_->batch_operations)
    if (operation.file_id &&
        operation.state != SlintUi::BatchOperation::State::Finished)
      pending_files.insert(operation.file_id);
  // Retain the last progress independently of session lifetime. Results stay
  // visible until dismissed or presented in the browser, even after disconnect.
  for (const auto& [id, info] : files)
    ui_->transfer_notifications[id].info = info;
  std::erase_if(ui_->transfer_notifications, [&](auto& item) {
    // Progress belongs to the sender. Remove any cached receive popup as well,
    // regardless of window visibility, peer version or final transfer status.
    if (item.second.info.receiving) {
      if (item.second.window) (*item.second.window)->hide();
      return true;
    }
    if ((!item.second.presentation.dismissed && !item.second.presentation.result_seen) || files.count(item.first) ||
        pending_files.count(item.first)) return false;
    if (item.second.window) (*item.second.window)->hide();
    return true;
  });
  if (ui_->transfer_notifications.empty()) {
    ui_->transfer_display = 0;
    return;
  }
  if (!ui_->transfer_display) {
    const auto* view = FindStreamView(ui_->transfer_notifications.begin()->second.info.remote_id);
    const auto& anchor = view && view->Visible()
        ? view->window->window()
        : ui_->gallery_shown ? ui_->wall->window() : ui_->main->window();
    ui_->transfer_display = DisplayForSlintWindow(anchor);
  }
  SDL_Rect bounds{};
  if (!SDL_GetDisplayUsableBounds(ui_->transfer_display, &bounds)) {
    ui_->transfer_display = SDL_GetPrimaryDisplay();
    if (!SDL_GetDisplayUsableBounds(ui_->transfer_display, &bounds)) return;
  }
#if defined(__APPLE__)
  const float desktop_scale = 1;
#else
  const float desktop_scale = std::max(0.01f, SDL_GetDisplayContentScale(ui_->transfer_display));
#endif
  const float screen_x = bounds.x / desktop_scale;
  const float screen_y = bounds.y / desktop_scale;
  const float screen_width = bounds.w / desktop_scale;
  const bool clipboard_visible = ui_->clipboard_notification &&
      (*ui_->clipboard_notification)->window().is_visible() &&
      ui_->clipboard_notification_display == ui_->transfer_display;
  const float screen_height = bounds.h / desktop_scale - (clipboard_visible ? 122.0f : 0.0f);
  const auto layout = Fit(screen_width, screen_height);
  size_t slot = 0;
  for (auto& transfer : ui_->transfer_notifications) {
    const auto& id = transfer.first;
    auto& notification = transfer.second;
    auto& info = notification.info;
    if (!files.count(id) && !pending_files.count(id) && info.status != Status::Completed &&
        info.status != Status::Failed && info.status != Status::Cancelled) {
      info.status = Status::Failed;
      info.rate_bps = 0;
    }
    const bool completed = info.status == Status::Completed;
    const bool failed = info.status == Status::Failed;
    const auto browser = ui_->file_browsers.find(info.remote_id);
    const bool in_browser = browser != ui_->file_browsers.end() && browser->second->PresentsTransfers();
    const bool result_presented = in_browser && browser->second->PresentsResult(id, info.status);
    if (!notification.presentation.ShowNotification(in_browser, result_presented) ||
        slot >= layout.capacity()) {
      if (notification.window) {
        (*notification.window)->hide();
        notification.window.reset();
      }
      continue;
    }
    const bool created = !notification.window;
    if (created) {
      notification.window.emplace(ui::FileTransferNotification::create());
      RegisterFontAwesome((*notification.window)->window());
      const auto dismiss = [this, id] {
        const auto found = ui_->transfer_notifications.find(id);
        if (found == ui_->transfer_notifications.end()) return;
        found->second.presentation.dismissed = true;
        if (found->second.window) (*found->second.window)->hide();
      };
      (*notification.window)->on_dismissed(dismiss);
      (*notification.window)->on_cancel_requested([this, id] { CancelFileTransfer(id); });
      (*notification.window)->window().on_close_requested([dismiss] {
        dismiss();
        return slint::CloseRequestResponse::KeepWindowShown;
      });
    }
    auto& component = *notification.window;
    ui::FileTransferEntry entry;
    entry.name = UiText(info.file_name);
    switch (info.status) {
      case Status::Queued: entry.status = UiText(localization::queued[localization_language_index_]); break;
      case Status::Sending: entry.status = UiText(localization::sending[localization_language_index_]); break;
      case Status::Receiving: entry.status = UiText(localization::receiving[localization_language_index_]); break;
      case Status::Completed: entry.status = UiText(localization::completed[localization_language_index_]); break;
      case Status::Failed: entry.status = UiText(localization::failed[localization_language_index_]); break;
      case Status::Cancelling: entry.status = UiText(localization::cancelling[localization_language_index_]); break;
      case Status::Cancelled: entry.status = UiText(localization::cancelled[localization_language_index_]); break;
    }
    entry.progress = completed ? 1.0f : info.file_size == 0 ? 0.0f
        : std::clamp(static_cast<float>(info.sent_bytes) / info.file_size, 0.0f, 1.0f);
    entry.speed = UiText(FormatTransferRate(info.rate_bps));
    entry.size = UiText(FormatFileSize(info.sent_bytes) + " / " + FormatFileSize(info.file_size));
    component->set_entry(entry);
    auto display_id = info.remote_id;
    std::erase_if(display_id, [](unsigned char c) { return c < '0' || c > '9'; });
    component->set_remote_id(UiText(display_id));
    component->set_heading(UiText(localization::file_sending_progress[localization_language_index_]));
    component->set_transferring(info.status == Status::Sending);
    component->set_completed(completed);
    component->set_failed(failed);
    component->set_cancelled(info.status == Status::Cancelled || info.status == Status::Cancelling);
    component->set_can_cancel(info.status == Status::Queued || info.status == Status::Sending);
    component->set_cancel_text(UiText(localization::cancel[localization_language_index_]));
    auto& window = component->window();
    const auto size = window.size();
    const float scale = std::max(window.scale_factor(), 0.01f);
    if (created || std::abs(size.width / scale - layout.width) > 0.5f ||
        std::abs(size.height / scale - layout.height) > 0.5f) {
      window.set_size(slint::LogicalSize({layout.width, layout.height}));
    }
    const auto position = Place(layout, screen_x, screen_y, screen_width, screen_height, slot++);
    if (CanUseGlobalPointerPosition()) {
      const auto current = window.position();
#if defined(__APPLE__)
      if (created || std::abs(current.x / scale - position.x) > 0.5f ||
          std::abs(current.y / scale - position.y) > 0.5f)
        window.set_position(slint::LogicalPosition({position.x, position.y}));
#else
      const auto x = static_cast<int32_t>(std::lround(position.x * desktop_scale));
      const auto y = static_cast<int32_t>(std::lround(position.y * desktop_scale));
      if (created || current.x != x || current.y != y)
        window.set_position(slint::PhysicalPosition({x, y}));
#endif
    }
    if (!window.is_visible()) {
      // Wait until native placement and focus restoration can be synchronous.
      if (!NativeWindowReady(window)) continue;
#if _WIN32
      // Tool windows stay out of Alt-Tab and must not interrupt remote input.
      const auto configure = [&] {
        const HWND hwnd = window.win32_hwnd();
        if (!hwnd) return;
        SetWindowLongPtr(hwnd, GWL_EXSTYLE,
            (GetWindowLongPtr(hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) & ~WS_EX_APPWINDOW);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
      };
      configure();
#endif
#if defined(__APPLE__)
      ShowFileTransferNotification(window.appkit_view(), [&window] { window.show(); });
#else
      component->show();
#endif
#if _WIN32
      configure();
#endif
    }
  }
  if (slot == 0) ui_->transfer_display = 0;
}

bool GuiApplication::ValidateSelfHostedSettingsFromUi(bool gallery_window) {
  auto& main = ui_->Window(gallery_window);
  const auto input = std::string(main->get_server_host());
  MiniRtcSignalServerAddress address{};
  if (MiniRtcParseSignalServerAddress(input.data(), input.size(), &address) != 0) {
    main->set_self_host_settings_error(UiText(
        localization::self_hosted_address_invalid[localization_language_index_]));
    return false;
  }
  const auto port = address.port ? std::optional<int>(address.port) : ParsePort(main->get_server_port());
  if (!port) {
    main->set_self_host_settings_error(UiText(
        localization::self_hosted_port_invalid[localization_language_index_]));
    return false;
  }
  main->set_server_host(UiText(std::string(address.host) + address.target));
  main->set_server_port(UiText(std::to_string(*port)));
  main->set_self_host_settings_error("");
  return true;
}

bool GuiApplication::SaveSettingsFromUi(bool gallery_window) {
  const auto previous_server = GetSignalServerSettings(*config_center_);
  const bool saved = PersistSettingsFromUi(gallery_window);
  const bool server_changed = previous_server != GetSignalServerSettings(*config_center_);
  // Individual settings are committed separately. Even if a later setter
  // fails, the saved endpoint and preview preference already apply.
  if (saved || server_changed) ApplySettingsFromConfig(server_changed);
  else settings_.ApplyPreviewPreference();
  if (saved) {
    ResetSettingsUi(gallery_window);
    ui_->gallery_synced = {};
  }
  return saved;
}

bool GuiApplication::PersistSettingsFromUi(bool gallery_window) {
#ifdef _WIN32
  // A later setter can fail after earlier values were persisted. Reconcile
  // with the host on the next status even when no config request was sent.
  if (managed_host_frontend_)
    local_host_status_.config_dirty = local_host_status_.identity_dirty = true;
#endif
  const bool previous_ip_enabled = config_center_->IsIpDirectEnabled();
  const auto previous_connection_mode = config_center_->GetConnectionMode();
  auto& main = ui_->Window(gallery_window);
  main->set_settings_save_error("");
  if (main->get_trusted_devices_only() && !trusted_devices_) {
    main->set_settings_save_error(UiText(localization::Literal(
        "无法读取设备身份或可信列表，请修复后再启用。", localization_language_index_)));
    return false;
  }
  const bool has_endpoint = main->get_self_hosted_enabled() ||
      !std::string(main->get_server_host()).empty() ||
      !std::string(main->get_server_port()).empty();
  if (has_endpoint && !ValidateSelfHostedSettingsFromUi(gallery_window)) {
    main->set_settings_tab(0);
    main->set_self_host_settings_open(true);
    return false;
  }
  const auto wall_settings = gallery::StreamSettings::FromIndices(
      main->get_gallery_resolution_draft(), main->get_gallery_frame_rate_draft(),
      main->get_gallery_bandwidth_draft());
  const bool wall_changed = wall_settings &&
      (wall_settings->resolution != ui_->gallery.stream_settings.resolution ||
       wall_settings->frame_rate != ui_->gallery.stream_settings.frame_rate ||
       wall_settings->bandwidth_mbps != ui_->gallery.stream_settings.bandwidth_mbps);
  const auto shared_wall_path = std::filesystem::path(cache_path_) / "deskhub.json";
  gallery::Workspace previous_wall;
  const bool wall_readable = !wall_changed || previous_wall.Load(shared_wall_path);
  auto updated_wall = previous_wall;
  if (wall_settings) updated_wall.stream_settings = *wall_settings;
  // Persist the draft first, but leave live sessions unchanged until the
  // common save succeeds. Restore the previous wall file if it fails.
  if (!wall_settings || (wall_changed && (!wall_readable || !updated_wall.Save(shared_wall_path)))) {
    main->set_gallery_settings_error(UiText(
        localization::Literal("保存失败，请重试", localization_language_index_)));
    main->set_settings_tab(1);
    return false;
  }
  if ((has_endpoint && config_center_->SetServerEndpoint(
          std::string(main->get_server_host()), *ParsePort(main->get_server_port())) != 0) ||
      config_center_->SetSelfHosted(main->get_self_hosted_enabled()) != 0) {
    if (wall_changed && !previous_wall.Save(shared_wall_path))
      LOG_ERROR("Failed to restore DeskHub settings after common save failure");
    main->set_self_host_settings_error(UiText(
        localization::self_hosted_save_failed[localization_language_index_]));
    main->set_settings_tab(0);
    main->set_self_host_settings_open(true);
    return false;
  }
  if (config_center_->SetIpDirectEnabled(main->get_ip_direct_enabled_draft()) != 0) {
    if (wall_changed && !previous_wall.Save(shared_wall_path))
      LOG_ERROR("Failed to restore DeskHub settings after IP direct save failure");
    main->set_settings_save_error(UiText(
        localization::Literal("无法保存 IP 连接设置，请重试。", localization_language_index_)));
    return false;
  }
#if defined(_WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(__OHOS__))
  if (config_center_->SetLockOnDisconnect(main->get_lock_on_disconnect_enabled()) != 0) {
    if (wall_changed && !previous_wall.Save(shared_wall_path))
      LOG_ERROR("Failed to restore DeskHub settings after lock preference save failure");
    main->set_settings_save_error(UiText(
        localization::Literal("无法保存断开后锁屏设置，请重试。", localization_language_index_)));
    return false;
  }
#endif
  if (config_center_->SetPrivacyLocalUnlock(main->get_privacy_local_unlock_allowed()) != 0) {
    if (wall_changed) previous_wall.Save(shared_wall_path);
    main->set_settings_save_error(UiText(
        localization::Literal("无法保存隐私屏解锁设置，请重试。", localization_language_index_)));
    return false;
  }
  if (main->get_privacy_black_screen_available() &&
      config_center_->SetPrivacyBlackScreen(main->get_privacy_black_screen()) != 0) {
    if (wall_changed) previous_wall.Save(shared_wall_path);
    main->set_settings_save_error(UiText(
        localization::Literal("保存失败，请重试", localization_language_index_)));
    return false;
  }
  if (config_center_->SetDesktopRealtimeRateControl(main->get_desktop_realtime_rate_control()) != 0) {
    if (wall_changed) previous_wall.Save(shared_wall_path);
    main->set_settings_save_error(UiText(localization::Literal(
        "保存失败，请重试", localization_language_index_)));
    return false;
  }
  if (config_center_->SetPauseVideoWhenMinimized(main->get_pause_video_when_minimized()) != 0) {
    if (wall_changed && !previous_wall.Save(shared_wall_path))
      LOG_ERROR("Failed to restore DeskHub settings after video pause preference save failure");
    main->set_settings_save_error(UiText(
        localization::Literal("保存失败，请重试", localization_language_index_)));
    return false;
  }
  try {
    const bool listen_all = main->get_lan_listen_all_draft();
    if (LanAvailable() && main->get_ip_direct_enabled_draft()) {
      lan_->SetListenAll(listen_all);
    } else if (lan_) {
      // Persist the preference without rebinding a disabled/stopping listener.
      if (listen_all != lan_->identity().listen_all())
        lan_->identity().SaveListenerMode(listen_all, {});
    } else if (listen_all != main->get_lan_listen_all()) {
      // Do not open/create an inactive identity when this preference is unchanged.
      lan::Identity identity(path_manager_->GetConfigPath() / "lan");
      identity.Load();
      if (listen_all != identity.listen_all())
        identity.SaveListenerMode(listen_all, {});
    }
  } catch (const std::exception& error) {
    if (config_center_->SetIpDirectEnabled(previous_ip_enabled) != 0 ||
        config_center_->SetConnectionMode(previous_connection_mode) != 0)
      LOG_ERROR("Failed to restore IP availability after listener settings failure");
    if (wall_changed && !previous_wall.Save(shared_wall_path))
      LOG_ERROR("Failed to restore DeskHub settings after listener settings failure");
    main->set_settings_tab(0);
    main->set_settings_save_error(
        main->global<ui::LanStrings>().invoke_error_message(UiText(error.what())));
    return false;
  }
  language_button_value_ =
      std::clamp(main->get_language_index(), 0, localization::kSystemLanguage);
  video_encode_format_button_value_ = std::clamp(main->get_codec_index(), 0, static_cast<int>(MiniRtcVideoCodec::H265));
  enable_self_hosted_ = main->get_self_hosted_enabled();
#if !defined(__OHOS__)
  enable_autostart_ = main->get_autostart_enabled();
#endif
  enable_daemon_ = main->get_daemon_enabled();

  localization_language_ =
      static_cast<ConfigCenter::LANGUAGE>(language_button_value_);
  localization_language_index_ = localization::detail::ClampLanguageIndex(language_button_value_);
  config_center_->SetLanguage(localization_language_);
  SyncPrivacyScreenSettings();
  config_center_->SetVideoEncodeFormat(
      static_cast<ConfigCenter::VIDEO_ENCODE_FORMAT>(
          video_encode_format_button_value_));
  config_center_->SetForceRelay(main->get_force_relay_enabled());
  main->set_force_relay_enabled(config_center_->IsForceRelay());
#if !defined(__OHOS__)
  bool identity_ready = true;
#ifdef _WIN32
  identity_ready = !enable_autostart_ || PrepareSharedGuiIdentity(false);
#endif
  if (!identity_ready || config_center_->SetAutostart(enable_autostart_) != 0) {
    enable_autostart_ = config_center_->IsEnableAutostart();
    main->set_autostart_enabled(enable_autostart_);
    main->set_settings_save_error(UiText(
#ifdef _WIN32
        localization::Literal("无法配置无人值守服务，请检查管理员权限和安装文件。", localization_language_index_)));
#else
        localization::Literal("保存失败，请重试", localization_language_index_)));
#endif
    return false;
  }
#endif
  if (config_center_->SetDaemon(enable_daemon_) != 0) {
    enable_daemon_ = config_center_->IsEnableDaemon();
    main->set_daemon_enabled(enable_daemon_);
    main->set_settings_save_error(UiText(
        localization::Literal("保存失败，请重试", localization_language_index_)));
    return false;
  }
  if (config_center_->SetSaveRemotePreviews(main->get_save_remote_previews()) != 0) {
    LOG_ERROR("Failed to save remote preview preference");
  }
  reload_recent_connections_ = true;
#if defined(_WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(__OHOS__))
  if (config_center_->SetPrivacyScreen(main->get_privacy_on_connect_enabled()) !=
      0) {
    main->set_privacy_on_connect_enabled(
        config_center_->IsEnablePrivacyScreen());
  }
#endif

  const std::string path(main->get_file_save_path());
  std::memset(file_transfer_save_path_buf_, 0,
              sizeof(file_transfer_save_path_buf_));
  std::strncpy(file_transfer_save_path_buf_, path.c_str(),
               sizeof(file_transfer_save_path_buf_) - 1);
  config_center_->SetFileTransferSavePath(path);

  if (wall_changed) {
    ui_->gallery.stream_settings = *wall_settings;
    std::shared_lock lock(remote_sessions_mutex_);
    for (const auto& [_, props] : remote_sessions_)
      if (props && !props->closing_ && props->wall_managed_)
        props->preview_needs_sync_ = true;
    ui_->gallery_synced = {};
  }
  if (config_center_->SetTrustedDevicesOnly(main->get_trusted_devices_only()) != 0) {
    main->set_settings_save_error(UiText(localization::Literal(
        "无法保存可信设备设置，请重试。", localization_language_index_)));
    return false;
  }
  TickControllerTrust();
  if (!PublishSharedIdentity(true)) {
    main->set_settings_save_error(UiText(localization::Literal(
        "保存失败，请重试", localization_language_index_)));
    return false;
  }
  return true;
}

void GuiApplication::LoadHistoryWorkspace() {
  ui_->gallery = {};
  ui_->gallery_path = settings_.HistoryDirectory() / "deskhub.json";
  if (!ui_->gallery.Load(ui_->gallery_path))
    LOG_WARN("Could not load DeskHub workspace; original file retained");
  gallery::Workspace shared;
  if (shared.Load(std::filesystem::path(cache_path_) / "deskhub.json"))
    ui_->gallery.stream_settings = shared.stream_settings;
}

void GuiApplication::ApplySettingsFromConfig(bool reconnect) {
  const bool changed_history = settings_.SelectHistoryServer();
  settings_.ApplyPreviewPreference();
  if (changed_history) {
    if (ui_) ReleaseControl();
    CloseAllRemoteSessions();
    ReloadRecentConnections();
    device_presence_cache_.SetSignalConnected(false);
    gallery_presence_ids_.clear();
    {
      std::lock_guard lock(pending_presence_probe_mutex_);
      pending_presence_probe_ = false;
      pending_presence_result_ready_ = false;
      pending_presence_remote_id_.clear();
      pending_presence_password_.clear();
      pending_presence_remember_password_ = false;
    }
    connection_dialog_target_.clear();
    show_connection_status_window_ = false;
    if (ui_) {
      ui_->recent_group_filter.reset();
      ui_->recent_preview_images.clear();
      ui_->recent_model_signature.clear();
      ui_->recent_model->set_vector({});
      ui_->gallery_previews.clear();
      ui_->gallery_pending_previews.clear();
      ui_->gallery_ids.clear();
      ui_->gallery_focus.clear();
      ui_->gallery_auth_id.clear();
      ui_->gallery_requested_auth.clear();
      ui_->batch_operations.clear();
      ui_->gallery_hidden_active_ids.clear();
      ui_->gallery_recent_model->set_vector({});
      ui_->gallery_network_model->set_vector({});
      ui_->gallery_model->set_vector({});
      ui_->gallery_selected_model->set_vector({});
      ui_->wall->set_gallery_group_index(0);
      ui_->wall->invoke_reset_gallery_transient();
      for (const auto& window : {ui_->main, ui_->wall})
        window->invoke_reset_history_view();
      LoadHistoryWorkspace();
      ui_->gallery_synced = {};
      SyncMainWindow();
    }
  }
  language_button_value_ = static_cast<int>(config_center_->GetLanguage());
  localization_language_index_ = localization::detail::ClampLanguageIndex(language_button_value_);
  localization_language_ = config_center_->GetLanguage();
  video_encode_format_button_value_ = static_cast<int>(config_center_->GetVideoEncodeFormat());
  enable_self_hosted_ = config_center_->IsSelfHosted();
  enable_autostart_ = config_center_->IsEnableAutostart();
  enable_daemon_ = config_center_->IsEnableDaemon();
  SyncPrivacyScreenSettings();
  const auto path = config_center_->GetFileTransferSavePath();
  std::snprintf(file_transfer_save_path_buf_, sizeof(file_transfer_save_path_buf_), "%s", path.c_str());
  const auto host = config_center_->GetSignalServerHost();
  std::snprintf(signal_server_ip_self_, sizeof(signal_server_ip_self_), "%s", host.c_str());
  const int port = config_center_->GetSignalServerPort();
  const auto port_text = port > 0 ? std::to_string(port) : std::string{};
  std::snprintf(signal_server_port_self_, sizeof(signal_server_port_self_), "%s", port_text.c_str());
  language_button_value_last_ = language_button_value_;
  video_encode_format_button_value_last_ = video_encode_format_button_value_;
  enable_self_hosted_last_ = enable_self_hosted_;
  enable_autostart_last_ = enable_autostart_;
  enable_daemon_last_ = enable_daemon_;
  file_transfer_save_path_last_ = path;
#if defined(__linux__) && !defined(__OHOS__)
  HeadlessConsole::Instance().SetLanguage(localization_language_index_);
#endif
  if (ui_) {
    ui_->localized_language = -1;
    for (const auto& window : {ui_->main, ui_->wall})
      window->set_ip_direct_enabled(config_center_->IsIpDirectEnabled());
    if (!ui_->main->get_settings_open()) ResetSettingsUi();
    if (!ui_->wall->get_settings_open()) ResetSettingsUi(true);
  }
  ApplyLanSettings();
  if (changed_history) return;
  if (reconnect && !HasActiveSession()) {
    // Tick recreates the peer after asynchronous cleanup, using the committed
    // configuration. Keep the application and its console running throughout.
    CloseAllRemoteSessions();
  } else {
    MiniRtcConnectionParams params{};
    params.video_codec = config_center_->GetVideoEncodeFormat();
    params.hardware_acceleration = true;
    params.turn_mode = static_cast<MiniRtcTurnMode>(config_center_->GetTurnMode());
    params.desktop_encoding_mode = config_center_->GetDesktopEncodingMode();
    if (params.video_codec != preferred_video_codec_ || params.turn_mode != params_.turn_mode ||
        params.desktop_encoding_mode != params_.desktop_encoding_mode) {
      // Keep the outgoing peer template in sync with the listener.
      if (!peer_ || PeerUpdateConnectionParams(peer_, params) == 0) {
        preferred_video_codec_ = params.video_codec;
        params_.turn_mode = params.turn_mode;
        params_.desktop_encoding_mode = params.desktop_encoding_mode;
      } else {
        LOG_ERROR("Failed to apply connection settings without reconnecting");
      }
    }
  }
}

void GuiApplication::ConnectFromUi(const std::string& input) {
  ui_->gallery_auth_id.clear();
  if (input.rfind("lan-", 0) == 0) {
    if (!LanAvailable()) return;
    if (auto existing = FindRemoteSession(input);
        existing && existing->connection_established_) {
      ShowStreamWindow(existing);
      return;
    }
    for (const auto& [_, recent] : recent_connections_)
      if (recent.remote_id == input && !recent.lan.address.empty()) {
        ui_->main->invoke_request_lan_connection(
            UiText(lan::FormatEndpoint(recent.lan.address, recent.lan.port)));
        return;
      }
    return;
  }
  const std::string remote_id = CompactPeerId(input);
  if (!gallery::Workspace::ValidId(remote_id)) {
    ui_->main->set_connection_input_error(
        UiText(localization::Literal("请输入有效的 9 位设备 ID", localization_language_index_)));
    return;
  }
  ui_->main->set_connection_input_error("");
  if (auto existing = FindRemoteSession(remote_id);
      existing && !existing->closing_ && existing->connection_established_) {
    ShowStreamWindow(existing);
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

void GuiApplication::ShowStreamWindow(const std::shared_ptr<RemoteSession>& session) {
  session->stream_window_open_ = true;
  if (auto* view = FindStreamView(session->remote_id_)) {
    auto& window = view->window->window();
    window.set_minimized(false);
    view->window->show();
#if defined(__APPLE__)
    MacActivateWindow(window.appkit_view());
#elif defined(_WIN32)
    SetForegroundWindow(window.win32_hwnd());
#elif defined(__linux__) && !defined(__OHOS__)
    if (window.wayland_surface()) {
      platform::ActivateWaylandWindow(window.wayland_display(), window.wayland_surface(), {});
    } else {
      if (!ui_->x11_focus_display) ui_->x11_focus_display = XOpenDisplay(nullptr);
      X11ActivateWindow(ui_->x11_focus_display, std::string(view->window->get_session_title()));
    }
#endif
  }
}

void GuiApplication::CloseDisconnectedSessions() {
  std::vector<std::string> closed;
  {
    std::shared_lock lock(remote_sessions_mutex_);
    for (const auto& [id, props] : remote_sessions_) {
      if (props && props->remote_close_pending_.exchange(false)) {
        closed.push_back(id);
      }
    }
  }
  // CloseRemoteSession drains transport callbacks and takes the session map
  // lock. Do this on the UI thread, outside both the callback and map lock.
  for (const auto& id : closed) DisconnectSession(id);
}

void GuiApplication::DisconnectSession(const std::string& remote_id) {
  // A user disconnect also cancels work that could reconnect this device.
  // Mark in place: this can run while SyncGallery is iterating the queue.
  for (auto& operation : ui_->batch_operations) {
    if (operation.id == remote_id) {
      operation.state = SlintUi::BatchOperation::State::Finished;
      operation.password.reset();
    }
  }
  CancelPendingConnection(remote_id);
  if (ui_->gallery_requested_auth == remote_id)
    ui_->gallery_requested_auth.clear();
  if (ui_->gallery_auth_id == remote_id) {
    ui_->gallery_auth_id.clear();
    show_connection_status_window_ = false;
  }
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
  if (controlled_remote_id_ == remote_id)
    ReleaseControl();
  else if (!gallery_group_control_id_.empty())
    ReleasePressedInput();
  keyboard_.ReleaseRemotePressedKeys(remote_id, "stream_window_closed");
  CloseRemoteSession(props);
  ResetRemoteSessionResources(props);
  {
    std::unique_lock lock(remote_sessions_mutex_);
    remote_sessions_.erase(remote_id);
  }
  if (connection_dialog_target_ == remote_id) connection_dialog_target_.clear();
}

void GuiApplication::ResetPointerMotion() {
  ui_->pointer_motion = {};
  ui_->wall->global<ui::RemoteCursor>().invoke_reset();
  if (auto* view = FindStreamView(controlled_remote_id_); view && ui_->gallery_control_id.empty())
    view->window->global<ui::RemoteCursor>().invoke_reset();
}

std::optional<VideoPoint> GuiApplication::MapRemotePointer(
    RemoteSession& session, float x, float y) {
  const bool gallery = !ui_->gallery_control_id.empty();
  const auto* view = FindStreamView(session.remote_id_);
  if (!gallery && !view) return std::nullopt;
  const auto width = gallery ? ui_->wall->get_gallery_video_width()
                             : view->window->get_video_area_width();
  const auto height = gallery ? ui_->wall->get_gallery_video_height()
                              : view->window->get_video_area_height();
  int video_width, video_height;
  {
    std::lock_guard lock(session.video_frame_mutex_);
    video_width = session.video_width_;
    video_height = session.video_height_;
  }
  const auto& cursor = gallery ? ui_->wall->global<ui::RemoteCursor>()
                               : view->window->global<ui::RemoteCursor>();
  if (!std::isfinite(width) || !std::isfinite(height) ||
      !(width > 0 && height > 0 && x >= 0 && x <= width && y >= 0 && y <= height) ||
      video_width <= 0 || video_height <= 0) {
    cursor.set_adjusted_x(-1);
    cursor.set_outside_video(true);
    return std::nullopt;
  }
  const int percent = config_center_->GetMouseSensitivityPercent();
  if (percent != 100) {
    if (!cursor.get_adjusted()) ui_->pointer_motion = {};
    const auto adjusted = ui_->pointer_motion.Update({x, y}, {width, height}, percent);
    x = adjusted.x;
    y = adjusted.y;
    cursor.set_adjusted_x(x);
    cursor.set_adjusted_y(y);
  }
  const auto point = MapVideoPoint(x, y, width, height, video_width, video_height);
  cursor.set_outside_video(!point);
  // The stream takes over local hover after this callback. Do not send the
  // covered move, press or scroll to the peer.
  if (percent != 100 && !gallery && ui_->pressed_mouse_buttons.empty() &&
      view->window->invoke_is_local_control_area(x, y)) {
    return std::nullopt;
  }
  return point;
}

void GuiApplication::SendPointerInput(int button, int kind, float x, float y) {
  auto props = FindRemoteSession(controlled_remote_id_);
  if (!props || !props->peer_ || !props->control_mouse_) {
    return;
  }
  if (kind == 0) {
    ReleaseControl();
    return;
  }
  if (kind == 2 && !ui_->pressed_mouse_buttons.count(button)) return;
  auto point = MapRemotePointer(*props, x, y);
  if (!point) {
    if (kind != 2) return;
    point = VideoPoint{ui_->pointer_x, ui_->pointer_y};
  }

  RemoteAction action{};
  action.type = ControlType::mouse;
  action.m.x = point->x;
  action.m.y = point->y;
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

  if (kind == 1) ui_->pressed_mouse_buttons.insert(button);
  if (kind == 2) ui_->pressed_mouse_buttons.erase(button);
  ui_->pointer_x = point->x;
  ui_->pointer_y = point->y;
  SendRemoteInput(action);
}

void GuiApplication::SendScrollInput(float delta_x, float delta_y, float x,
                                     float y) {
  auto props = FindRemoteSession(controlled_remote_id_);
  if (!props || !props->peer_ || !props->control_mouse_) {
    return;
  }
  const auto point = MapRemotePointer(*props, x, y);
  if (!point) {
    return;
  }
  RemoteAction action{};
  action.type = ControlType::mouse;
  action.m.x = point->x;
  action.m.y = point->y;
  const bool vertical = std::abs(delta_y) >= std::abs(delta_x);
  const float delta = vertical ? delta_y : delta_x;
  action.m.flag =
      vertical ? MouseFlag::wheel_vertical : MouseFlag::wheel_horizontal;
  ui_->pointer_x = point->x;
  ui_->pointer_y = point->y;
  SendRemoteInput(action, delta);
}

void GuiApplication::SendKeyInput(const std::string& text, bool pressed,
                                  bool control, bool alt, bool shift,
                                  bool meta) {
  (void)control;
  (void)alt;
  (void)meta;
  auto props = FindRemoteSession(controlled_remote_id_);
  if (!props || !props->peer_ || !props->control_mouse_ ||
      props->connection_status_.load() != MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
    return;
  }


  // Native capture forwards the event independently. Slint keyboard events
  // are used only by platforms whose native capture backend is unavailable.
  if (keyboard_capturer_is_started_ && !keyboard_capturer_uses_window_events_) {
    return;
  }

  const int virtual_key = SlintKeyToWindowsVk(text);
  if (virtual_key >= 0) {
    keyboard_.SendWindowKey(virtual_key, pressed, shift);
  }
}

void GuiApplication::OpenFileDialog(const std::string& title,
                                   std::function<void(std::string)> completion) {
  if (!ui_ || exit_) return;
  if (ui_->file_dialog_open) {
    completion({});
    return;
  }
  ui_->file_dialog_open = true;
  std::weak_ptr<void> lifetime = ui_->file_dialog_lifetime;
  auto finished = [this, lifetime, completion = std::move(completion)](std::string path) {
    // Native callbacks can arrive after cancellation during application exit.
    if (lifetime.expired()) return;
    ui_->file_dialog_open = false;
    if (!exit_) completion(std::move(path));
  };
#if defined(__APPLE__)
  OpenNativeFileDialog(title, std::move(finished));
#else
  const char* path =
      tinyfd_openFileDialog(title.c_str(), "", 0, nullptr, nullptr, 0);
  finished(path ? path : "");
#endif
}

void GuiApplication::ExportDiagnosticLogs() {
  if (!ui_ || exit_ || ui_->file_dialog_open || ui_->diagnostic_export.valid()) return;
  ui_->file_dialog_open = true;
  for (const auto& window : {ui_->main, ui_->wall}) {
    window->set_feedback_exporting(true);
    window->set_feedback_status("");
    window->set_feedback_saved_path("");
    window->set_feedback_succeeded(false);
  }
  const auto filename = feedback::DiagnosticFileName();
  const auto title = localization::Literal("导出诊断日志", localization_language_index_);
  std::weak_ptr<void> lifetime = ui_->file_dialog_lifetime;
  auto finished = [this, lifetime](std::string path) {
    if (lifetime.expired()) return;
    ui_->file_dialog_open = false;
    if (exit_) return;
    if (path.empty()) {
      for (const auto& window : {ui_->main, ui_->wall}) window->set_feedback_exporting(false);
      return;
    }
    try {
      auto destination = PathFromUtf8(path);
      auto extension = destination.extension().string();
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (extension != ".zip") {
        destination += ".zip";
        path += ".zip";
        // The save dialog only confirms replacement of the original path.
        if (std::filesystem::exists(destination)) throw std::runtime_error("Destination exists");
      }
#ifdef __OHOS__
      // The folder picker cannot confirm replacement of an existing archive.
      if (std::filesystem::exists(destination)) throw std::runtime_error("Destination exists");
#endif
      ui_->diagnostic_saved_path = path;
      get_logger()->flush();
      ui_->diagnostic_export = std::async(std::launch::async,
          [logs = path_manager_->GetLogPath(), destination] {
            return feedback::ExportDiagnostics(logs, logs, destination, CROSSDESK_VERSION);
          });
    } catch (...) {
      for (const auto& window : {ui_->main, ui_->wall}) {
        window->set_feedback_exporting(false);
        window->set_feedback_status(UiText(localization::Literal(
            "无法保存诊断包，请选择其他位置重试。", localization_language_index_)));
      }
    }
  };
#if defined(__APPLE__)
  SaveNativeFileDialog(title, filename, std::move(finished));
#elif defined(__OHOS__)
  const char* folder = tinyfd_selectFolderDialog(title.c_str(), "");
  finished(folder ? std::string(folder) + "/" + filename : "");
#else
  const char* filters[] = {"*.zip"};
  const char* path = tinyfd_saveFileDialog(title.c_str(), filename.c_str(), 1,
                                         filters, "ZIP archive");
  finished(path ? path : "");
#endif
}

void GuiApplication::PollDiagnosticExport() {
  if (!ui_->diagnostic_export.valid() ||
      ui_->diagnostic_export.wait_for(0ms) != std::future_status::ready) return;
  feedback::ExportResult result{feedback::ExportError::write_failed};
  try { result = ui_->diagnostic_export.get(); } catch (...) {}
  const char* message = "无法保存诊断包，请选择其他位置重试。";
  switch (result.error) {
    case feedback::ExportError::none:
      message = "诊断日志已导出，请将 ZIP 附到 GitHub Issue。"; break;
    case feedback::ExportError::no_logs:
      message = "没有找到可导出的日志。"; break;
    case feedback::ExportError::read_failed:
      message = "无法读取日志，请稍后重试。"; break;
    case feedback::ExportError::too_large:
      message = "日志文件过大，无法导出。"; break;
    case feedback::ExportError::write_failed: break;
  }
  const bool succeeded = result.error == feedback::ExportError::none;
  for (const auto& window : {ui_->main, ui_->wall}) {
    window->set_feedback_exporting(false);
    window->set_feedback_succeeded(succeeded);
    window->set_feedback_status(UiText(localization::Literal(message, localization_language_index_)));
    window->set_feedback_saved_path(UiText(succeeded ? ui_->diagnostic_saved_path : ""));
  }
}

bool GuiApplication::OpenUrl(const std::string& url) {
  return SDL_OpenURL(url.c_str());
}

void GuiApplication::SyncAudioPlayback() {
  const bool wall_visible = ui_ && ui_->wall->window().is_visible() &&
      !ui_->wall->window().is_minimized();

  std::lock_guard delivery_lock(audio_delivery_mutex_);
  std::shared_lock lock(remote_sessions_mutex_);
  for (const auto& [id, props] : remote_sessions_) {
    const bool allowed = !props->closing_ &&
        props->connection_status_ == MiniRtcConnectionStatus::MiniRtcConnectionConnected &&
        props->audio_capture_button_pressed_ &&
        ((wall_visible && props->wall_managed_) ||
         (focus_on_stream_window_ && id == controlled_remote_id_ && props->stream_window_open_));
    if (props->audio_allowed_.exchange(allowed) != allowed)
      props->preview_needs_sync_ = true;
    if (!allowed) devices_.ClearAudio(id);
  }
}

void GuiApplication::ReleasePressedInput(bool preserve_local_ui) {
  {
    std::shared_lock lock(remote_sessions_mutex_);
    for (const auto& [_, session] : remote_sessions_) session->scroll_ = {};
  }
  ResetPointerMotion();
  keyboard_.ForceReleasePressedKeys();
  for (int button : ui_->pressed_mouse_buttons) {
    RemoteAction action{};
    action.type = ControlType::mouse;
    action.m.x = ui_->pointer_x;
    action.m.y = ui_->pointer_y;
    action.m.flag = button == 1   ? MouseFlag::left_up
                    : button == 2 ? MouseFlag::right_up
                                  : MouseFlag::middle_up;
    SendRemoteInput(action);
  }
  ui_->pressed_mouse_buttons.clear();
  if (auto* view = FindStreamView(controlled_remote_id_); view && ui_->gallery_control_id.empty()) {
    if (preserve_local_ui) view->window->invoke_reset_pressed_input();
    else view->window->invoke_reset_input_state();
  }
}

void GuiApplication::ReleaseControl(bool preserve_local_ui) {
  if (ui_->releasing_input) return;
  ui_->releasing_input = true;
  focus_on_stream_window_ = false;
  const auto gallery_target = ui_->gallery_control_id;
  devices_.StopKeyboardCapturer();
  ReleasePressedInput(preserve_local_ui);
  gallery_group_control_id_.clear();
  ui_->wall->set_gallery_group_controlling(false);
  ui_->wall->set_gallery_remote_cursor_shape(
      static_cast<int>(RemoteCursorShape::default_cursor));
  if (!ui_->gallery_control_id.empty()) {
    ui_->gallery_control_id.clear();
    ui_->wall->set_gallery_controlling(false);
  }
  SyncAudioPlayback();
  if (auto props = FindRemoteSession(controlled_remote_id_)) {
    ++props->input_generation_;
    props->clipboard_allowed_ = false;
    if (props->remote_id_ == gallery_target) {
      props->control_mouse_ = ui_->gallery_previous_mouse_control;
      props->enable_mouse_control_ = props->control_mouse_;
    }
  }
  controlled_remote_id_.clear();
  start_keyboard_capturer_ = false;
  ui_->releasing_input = false;
}

void GuiApplication::ShowGalleryWindow() {
  if (!ui_) return;
  auto& window = ui_->wall->window();
  if (!window.is_visible()) {
    ReleaseControl();
    std::vector<std::shared_ptr<RemoteSession>> sessions;
    {
      std::shared_lock lock(remote_sessions_mutex_);
      for (const auto& [_, props] : remote_sessions_)
        if (props && !props->closing_) sessions.push_back(props);
    }
    for (const auto& props : sessions) MuteRemoteAudio(props);
    if (!ui_->gallery_shown) {
      ui_->gallery_window.BeginRestore();
      RestoreWindowGeometry(true, true);
      ui_->gallery_shown = true;
    }
    need_to_send_recent_connections_ = true;
    ui_->wall->show();
  }
  window.set_minimized(false);
#if defined(__APPLE__)
  ConfigureMainWindowChrome(window.appkit_view(), true);
  MacActivateWindow(window.appkit_view());
  ui_->gallery_native_titlebar_attempts = 30;
#elif defined(_WIN32)
  ConfigureWindowsWindowIcons(window.win32_hwnd());
  SetForegroundWindow(window.win32_hwnd());
#elif defined(__linux__) && !defined(__OHOS__)
  if (!ui_->x11_focus_display) ui_->x11_focus_display = XOpenDisplay(nullptr);
  X11ActivateWindow(ui_->x11_focus_display, std::string(ui_->wall->get_window_title()));
#endif
  ui_->gallery_synced = {};
  SyncGallery();
}

void GuiApplication::CloseGalleryWindow() {
  if (!ui_) return;
  ReleaseControl();
  // Snapshot all receivers, not just the current wall page/group. Include
  // requests without a live session so no queued work can reconnect on close.
  std::set<std::string> disconnect_ids;
  {
    std::shared_lock lock(remote_sessions_mutex_);
    for (const auto& [id, _] : remote_sessions_) disconnect_ids.insert(id);
  }
  for (const auto& [id, _] : pending_reconnects_) disconnect_ids.insert(id);
  for (const auto& operation : ui_->batch_operations)
    disconnect_ids.insert(operation.id);
  {
    std::lock_guard lock(pending_presence_probe_mutex_);
    if (!pending_presence_remote_id_.empty())
      disconnect_ids.insert(pending_presence_remote_id_);
  }
  // Reuse explicit disconnect: cancel retries, release input/media resources,
  // and queue peer teardown without waiting for the transport on the UI thread.
  for (const auto& id : disconnect_ids) DisconnectSession(id);
  need_to_rejoin_ = false;
  ui_->wall->invoke_reset_gallery_transient();
  ui_->wall->set_alias_open(false);
  ui_->wall->set_delete_open(false);
  ui_->wall->set_device_group_open(false);
  ui_->wall->set_self_host_settings_open(false);
  ui_->wall->set_settings_open(false);
  ui_->wall->set_about_open(false);
  ui_->wall->set_messages_open(false);
  ResetSettingsUi(true);
  ui_->gallery_requested_auth.clear();
  ui_->gallery_auth_id.clear();
  show_connection_status_window_ = false;
  SyncConnectionDialog();
  SyncWindowGeometry(true);
  ui_->gallery_transition.Cancel();
  ui_->wall->set_gallery_transitioning(false);
  ui_->window_saved = {};
  SaveGalleryWindowState();
#if defined(__APPLE__)
  // Restore application presentation options while the wall is hidden. Its
  // remembered fullscreen preference is applied again on the next open.
  if (IsGalleryWindowFullscreen()) {
    SetGalleryWindowFullscreen(false);
    ui_->gallery_window.BeginRestore();
  }
#endif
  ui_->wall->hide();
  ui_->gallery_synced = {};
  SyncGallery();
  SyncStreamWindows();
}

void GuiApplication::ToggleGalleryFullscreen() {
  if (!ui_ || !ui_->wall->window().is_visible() ||
      ui_->gallery_transition.Active()) return;
#if defined(__APPLE__)
  const bool fullscreen = IsGalleryWindowFullscreen();
#else
  const bool fullscreen = ui_->wall->window().is_fullscreen();
#endif
  const bool target = !(ui_->gallery_window.Restoring()
                            ? ui_->gallery_window.wall_fullscreen : fullscreen);
  ui_->gallery_transition.Begin(target, std::chrono::steady_clock::now());
  // Fullscreen only changes presentation. End held gestures before blocking
  // input, but retain the controlled session and its keyboard focus.
  if (!ui_->gallery_control_id.empty()) ReleasePressedInput();
  ui_->wall->set_gallery_transitioning(true);
  ui_->gallery_synced = {};
}

void GuiApplication::SaveGalleryWindowState() {
  if (ui_->wall->get_gallery_resizing() || ui_->connection_window.Restoring() ||
      (ui_->wall->window().is_visible() && ui_->gallery_window.Restoring()) ||
      ui_->gallery_transition.Active()) return;
  const auto now = std::chrono::steady_clock::now();
  if (now - ui_->window_saved > 1s || exit_) {
    const float scale = ui_->loaded_window_scale[1] > 0
        ? ui_->loaded_window_scale[1]
        : std::max(ui_->wall->window().scale_factor(), 1.0f);
    const float connection_scale = std::max(ui_->main->window().scale_factor(), 1.0f);
    nlohmann::json state{{"version", 2},
                         {"tile_width", ui_->wall->get_gallery_tile_width()},
                         {"focus_rail_height", ui_->wall->get_gallery_focus_rail_height()},
                         {"fullscreen", ui_->gallery_window.wall_fullscreen},
                         {"maximized", ui_->gallery_window.wall_maximized}};
    if (ui_->gallery_window.wall_size) {
      state["width"] = int(std::lround(ui_->gallery_window.wall_size->width));
      state["height"] = int(std::lround(ui_->gallery_window.wall_size->height));
    }
    if (ui_->gallery_window.wall_position) {
      state["wall_x"] = ui_->gallery_window.wall_position->x / scale;
      state["wall_y"] = ui_->gallery_window.wall_position->y / scale;
    }
    if (ui_->connection_window.connection_position) {
      state["connection_x"] =
          ui_->connection_window.connection_position->x / connection_scale;
      state["connection_y"] =
          ui_->connection_window.connection_position->y / connection_scale;
    }
    const auto text = state.dump();
    if (text != ui_->saved_window_signature &&
        gallery::WriteAtomically(std::filesystem::path(cache_path_) / "deskhub.json.window", text))
      ui_->saved_window_signature = text;
    ui_->window_saved = now;
  }
}

bool GuiApplication::AddGalleryDevice(const std::string& input,
                                      const std::string& password,
                                      const std::string& group,
                                      bool remember_password) {
  const auto error = [this](const char* zh) {
    ui_->wall->set_gallery_notice(UiText(
        localization::Literal(zh, localization_language_index_).c_str()));
    return false;
  };
  const auto id = gallery::Workspace::NormalizeId(input);
  if (!gallery::Workspace::ValidId(id))
    return error("请输入有效的 9 位设备 ID");
  if (password.empty() || password.size() > 6 ||
      password.find_first_not_of(
          "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") !=
          std::string::npos)
    return error("请输入 1–6 位英文字母或数字密码");
  if (!signal_connected_)
    return error("尚未连接服务器，请稍后重试");
  using State = SlintUi::BatchOperation::State;
  for (const auto& operation : ui_->batch_operations)
    if (operation.id == id && operation.action == "connect" &&
        (operation.state == State::Queued || operation.state == State::Running))
      return error("该设备正在连接，请稍候");

  auto updated = ui_->gallery;
  updated.Add(id, Trim(group));
  if (!updated.Save(ui_->gallery_path))
    return error("保存失败，请重试");
  ui_->gallery = std::move(updated);
  ui_->gallery_hidden_active_ids.erase(id);
  if (auto props = FindRemoteSession(id)) {
    props->wall_managed_ = true;
    MuteRemoteAudio(props);
  }
  for (auto& operation : ui_->batch_operations)
    if (operation.id == id && operation.action == "connect" &&
        operation.state == State::AwaitingPassword)
      operation.state = State::Finished;
  SlintUi::BatchOperation operation;
  operation.id = id;
  operation.action = "connect";
  operation.password = password;
  operation.remember_password = remember_password;
  ui_->batch_operations.push_back(std::move(operation));
  ui_->gallery_requested_auth = id;
  ui_->gallery_focus.clear();
  ui_->wall->set_gallery_group_index(0);
  const auto ids = ui_->gallery.Ids();
  ui_->gallery.page = (std::find(ids.begin(), ids.end(), id) - ids.begin()) /
                      ui_->gallery.capacity;
  ui_->gallery_synced = {};
  ui_->wall->set_gallery_notice("");
  return true;
}

bool GuiApplication::ImportGalleryRecent(const std::string& input_group) {
  std::vector<std::string> ids;
  for (size_t i = 0; i < ui_->gallery_recent_model->row_count(); ++i) {
    const auto device = ui_->gallery_recent_model->row_data(i);
    if (device && device->selected && !device->added &&
        !ui_->gallery.Contains(std::string(device->id)))
      ids.emplace_back(device->id);
  }
  if (ids.empty()) return false;
  const auto group = Trim(input_group);
  auto updated = ui_->gallery;
  updated.AddMany(ids, group);
  if (!updated.Save(ui_->gallery_path)) {
    ui_->wall->set_gallery_notice(UiText(
        localization::Literal("保存失败，请重试", localization_language_index_)));
    return false;
  }
  ui_->gallery = std::move(updated);
  // Import only membership. Existing recent credentials remain in their own
  // store and are looked up by the normal connect action when requested.
  for (const auto& id : ids) {
    ui_->gallery_hidden_active_ids.erase(id);
    if (auto props = FindRemoteSession(id)) {
      props->wall_managed_ = true;
      MuteRemoteAudio(props);
    }
  }
  ui_->gallery_focus.clear();
  ui_->gallery.selected.clear();
  ui_->wall->set_gallery_batch(false);
  const int group_index = group.empty() ? 0 :
      int(std::find(ui_->gallery.groups.begin(), ui_->gallery.groups.end(), group) -
          ui_->gallery.groups.begin()) + 1;
  ui_->wall->set_gallery_group_index(group_index);
  const auto visible = ui_->gallery.Ids(group);
  ui_->gallery.page = (std::find(visible.begin(), visible.end(), ids.front()) -
                      visible.begin()) / ui_->gallery.capacity;
  ui_->gallery_synced = {};
  ui_->wall->set_gallery_notice("");
  return true;
}

void GuiApplication::DeviceGroupAction(const std::string& action,
                                      const std::string& value,
                                      bool gallery_window) {
  auto& editor = ui_->group_editors[gallery_window];
  auto& main = ui_->Window(gallery_window);
  main->set_device_group_error("");
  if (action == "close") {
    editor.id.clear();
    editor.options->set_vector({});
  } else if (action == "open") {
    editor.id = value;
    std::vector<ui::DeviceGroupOption> options;
    options.push_back({"", true});
    for (const auto& name : ui_->gallery.groups) {
      const bool selected = std::any_of(
          ui_->gallery.members.begin(), ui_->gallery.members.end(),
          [&](const auto& m) { return m.id == value && m.group == name; });
      options.push_back({UiText(name), selected});
      if (selected) options.front().selected = false;
    }
    editor.options->set_vector(options);
    main->set_device_group_options(editor.options);
  } else if (action == "toggle" && !editor.id.empty()) {
    bool grouped = false;
    for (size_t i = 1; i < editor.options->row_count(); ++i) {
      auto option = *editor.options->row_data(i);
      if (value.empty()) option.selected = false;
      else if (std::string(option.name) == value) option.selected = !option.selected;
      grouped |= option.selected;
      editor.options->set_row_data(i, option);
    }
    editor.options->set_row_data(0, {"", !grouped});
    if (value.empty()) main->set_device_group_new_name("");
  }
}

bool GuiApplication::SaveDeviceGroups(const std::string& new_group,
                                      bool gallery_window) {
  auto& editor = ui_->group_editors[gallery_window];
  auto& main = ui_->Window(gallery_window);
  const auto& id = editor.id;
  if (!gallery::Workspace::ValidId(id)) return false;
  std::vector<std::string> names;
  for (size_t i = 0; i < editor.options->row_count(); ++i) {
    const auto option = *editor.options->row_data(i);
    if (option.selected && !option.name.empty()) names.emplace_back(option.name);
  }
  if (const auto name = Trim(new_group); !name.empty()) names.push_back(name);

  auto updated = ui_->gallery;
  if (!updated.SetGroups(id, names) || !updated.Save(ui_->gallery_path)) {
    main->set_device_group_error(UiText(
        localization::Literal("保存失败，请重试", localization_language_index_)));
    return false;
  }
  // Removing an empty group shifts indices. Retain the filter by name instead
  // of accidentally switching to a different group at the old index.
  const int index = ui_->wall->get_gallery_group_index();
  const auto current = index > 0 && index <= int(ui_->gallery.groups.size())
                           ? ui_->gallery.groups[index - 1] : std::string{};
  ui_->gallery = std::move(updated);
  const auto found = std::find(ui_->gallery.groups.begin(),
                               ui_->gallery.groups.end(), current);
  ui_->wall->set_gallery_group_index(
      found == ui_->gallery.groups.end() ? 0
      : int(found - ui_->gallery.groups.begin()) + 1);
  // Group edits never enroll or unhide a device, or take ownership of an
  // existing session. Only the explicit wall-add actions do that.
  ui_->gallery_synced = {};
  main->set_device_group_error("");
  SyncGallery();
  return true;
}

bool GuiApplication::DeleteGroup(const std::string& name) {
  auto updated = ui_->gallery;
  if (!updated.DeleteGroup(name)) return true;
  if (!updated.Save(ui_->gallery_path)) return false;

  const auto removed = std::find(ui_->gallery.groups.begin(), ui_->gallery.groups.end(), name);
  const int removed_index = int(removed - ui_->gallery.groups.begin()) + 1;
  const int selected = ui_->wall->get_gallery_group_index();
  ui_->gallery = std::move(updated);
  if (selected == removed_index) ui_->wall->set_gallery_group_index(0);
  else if (selected > removed_index) ui_->wall->set_gallery_group_index(selected - 1);
  if (ui_->recent_group_filter == name) ui_->recent_group_filter.reset();

  // A device editor can still be open in the other window.
  for (auto& editor : ui_->group_editors) {
    if (editor.options->row_count() == 0) continue;
    std::vector<ui::DeviceGroupOption> options;
    for (size_t row = 0; row < editor.options->row_count(); ++row) {
      auto option = *editor.options->row_data(row);
      if (std::string(option.name) != name) options.push_back(option);
    }
    options.front().selected = std::none_of(options.begin() + 1, options.end(),
        [](const auto& option) { return option.selected; });
    editor.options->set_vector(options);
  }
  ui_->gallery_synced = {};
  SyncMainWindow();
  SyncGallery();
  return true;
}

void GuiApplication::GalleryAction(const std::string& action,
                                   const std::string& value,
                                   bool gallery_window) {
  using OperationState = SlintUi::BatchOperation::State;
  auto& wall = ui_->gallery;
  ui_->gallery_synced = {};
  if (action == "settings-open") {
    ReleaseControl();
    ResetSettingsUi(gallery_window);
    return;
  }
  if (action == "network-open") {
    ReleaseControl();
    ui_->gallery_network_synced = {};
    return;
  }
  const auto tr = [this](const char* zh) {
    return UiText(localization::Literal(zh, localization_language_index_).c_str());
  };
  if (action == "recent-open") {
    ReleaseControl();
    need_to_send_recent_connections_ = true;
    ui_->wall->set_gallery_notice("");
    const int group_index = ui_->wall->get_gallery_group_index();
    ui_->wall->set_gallery_import_group(UiText(
        group_index > 0 && group_index <= int(wall.groups.size())
            ? wall.groups[group_index - 1] : ""));
    std::vector<ui::GalleryRecentDevice> devices;
    std::set<std::string> seen;
    int available = 0;
    for (const auto& [_, recent] : recent_connections_) {
      if (!recent.lan.address.empty()) continue;
      if (!gallery::Workspace::ValidId(recent.remote_id) ||
          !seen.insert(recent.remote_id).second)
        continue;
      ui::GalleryRecentDevice device{};
      device.id = UiText(recent.remote_id);
      device.name = UiText(settings_.RecentConnectionDisplayName(recent));
      device.online = device_presence_cache_.IsOnline(recent.remote_id);
      device.added = wall.Contains(recent.remote_id);
      device.selected = device.added;
      if (!device.added) ++available;
      devices.push_back(std::move(device));
    }
    ui_->gallery_recent_model->set_vector(std::move(devices));
    ui_->wall->set_gallery_recent_devices(ui_->gallery_recent_model);
    ui_->wall->set_gallery_recent_selected(0);
    ui_->wall->set_gallery_recent_available(available);
    return;
  }
  if (action == "recent-close") {
    ui_->gallery_recent_model->set_vector({});
    ui_->wall->set_gallery_recent_selected(0);
    ui_->wall->set_gallery_recent_available(0);
    ui_->wall->set_gallery_notice("");
    return;
  }
  if (action == "recent-select" || action == "recent-all") {
    const bool select_all = ui_->wall->get_gallery_recent_selected() <
                           ui_->wall->get_gallery_recent_available();
    int count = 0;
    for (size_t i = 0; i < ui_->gallery_recent_model->row_count(); ++i) {
      auto device = *ui_->gallery_recent_model->row_data(i);
      if (device.added) continue;
      if (action == "recent-all" || std::string(device.id) == value) {
        device.selected = action == "recent-all" ? select_all : !device.selected;
        ui_->gallery_recent_model->set_row_data(i, device);
      }
      if (device.selected) ++count;
    }
    ui_->wall->set_gallery_recent_selected(count);
    return;
  }
  if (action == "add-open") {
    ui_->wall->set_gallery_notice("");
    return;
  } else if (action == "group") {
    ReleaseControl();
    wall.page = 0;
    wall.selected.clear();
    ui_->gallery_focus.clear();
  } else if (action == "selection" || action == "view-mode") {
    ReleaseControl();
    wall.selected.clear();
    ui_->gallery_focus.clear();
  } else if (action == "select") {
    if (!wall.selected.erase(value)) wall.selected.insert(value);
  } else if (action == "all-select") {
    wall.ToggleSelectAll(ui_->gallery_ids);
  } else if (action == "page") {
    ReleaseControl();
    wall.page += value == "next" ? 1 : -1;
    ui_->gallery_focus.clear();
  } else if (action == "focus") {
    ReleaseControl();
    ui_->gallery_focus = value;
  } else if (action == "group-release") {
    // Flush releases to the old group before returning to single-device input.
    ReleasePressedInput();
    gallery_group_control_id_.clear();
    ui_->wall->set_gallery_group_controlling(false);
  } else if (action == "control" || action == "group-control") {
    ReleaseControl();
    auto props = FindRemoteSession(value);
    if (value != ui_->gallery_focus || !props || !props->peer_ ||
        props->closing_ ||
        props->connection_status_ != MiniRtcConnectionStatus::MiniRtcConnectionConnected)
      return;
    {
      std::lock_guard lock(props->gallery_frame_mutex_);
      if (!props->gallery_rgba_ ||
          std::chrono::steady_clock::now() - props->gallery_frame_at_ > 5s)
        return;
    }
    ui_->gallery_previous_mouse_control = props->control_mouse_;
    ui_->gallery_control_id = value;
    controlled_remote_id_ = value;
    if (action == "group-control") gallery_group_control_id_ = value;
    ui_->wall->set_gallery_group_controlling(!gallery_group_control_id_.empty());
    props->control_mouse_ = true;
    props->enable_mouse_control_ = true;
    props->clipboard_allowed_ = true;
    // The gallery window FocusScope owns keyboard input. Native capture stays
    // off so local toolbar/menu events never leak into this remote session.
    start_keyboard_capturer_ = false;
    ui_->wall->set_gallery_controlling(true);
  } else if (action == "remove") {
    ReleaseControl();
    auto updated = wall;
    const int group_index = ui_->wall->get_gallery_group_index();
    const std::string group =
        group_index > 0 && group_index <= int(wall.groups.size())
            ? wall.groups[group_index - 1]
            : "";
    updated.Remove(value, group);
    if (updated.Save(ui_->gallery_path)) {
      wall = std::move(updated);
      const auto retained =
          std::find(wall.groups.begin(), wall.groups.end(), group);
      ui_->wall->set_gallery_group_index(
          retained == wall.groups.end() ? 0
          : int(retained - wall.groups.begin()) + 1);
      // Only removing from All devices hides an active session. Removing a
      // group assignment still leaves the device enrolled in the wall.
      if (group.empty())
        ui_->gallery_hidden_active_ids.insert(value);
      ui_->gallery_focus.clear();
    }
  } else if (action == "release") {
    ReleaseControl();
  } else if (action == "connect" || action == "disconnect" || action == "mute" ||
             action == "unmute" || action == "file") {
    std::vector<std::string> ids;
    if (!value.empty())
      ids.push_back(value);
    else
      for (const auto& id : ui_->gallery_ids)
        if (wall.selected.count(id)) ids.push_back(id);
    if (ids.empty()) return;
    if (action == "mute" || action == "unmute") {
      for (const auto& id : ids) {
        if (auto props = FindRemoteSession(id);
            props && !props->closing_ &&
            props->connection_status_ == MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
          if (action == "mute") {
            MuteRemoteAudio(props);
          } else {
            UnmuteRemoteAudio(props);
          }
        }
      }
      SyncGallery();
      return;
    }
    if (action == "connect" && !value.empty())
      ui_->gallery_requested_auth = value;
    ReleaseControl();
    if (action == "disconnect") {
      // Disconnect must not wait for long-running file transfers to finish.
      for (const auto& id : ids) DisconnectSession(id);
      SyncGallery();
      return;
    }
    // Capture the selection before the asynchronous file picker opens.
    const auto enqueue = [this, ids, action, value](const std::string& file) {
      RemoteSession::FileTransferInfo queued_file;
      if (action == "file") {
        queued_file.file_path = PathFromUtf8(file);
        const auto name = queued_file.file_path.filename().u8string();
        queued_file.file_name.assign(name.begin(), name.end());
        std::error_code ec;
        if (std::filesystem::is_regular_file(queued_file.file_path, ec)) {
          const auto size = std::filesystem::file_size(queued_file.file_path, ec);
          if (!ec) queued_file.file_size = size;
        } else {
          queued_file.status = RemoteSession::FileTransferStatus::Failed;
        }
        if (ec) queued_file.status = RemoteSession::FileTransferStatus::Failed;
      }
      for (const auto& id : ids) {
        if (action == "connect" && !value.empty()) {
          if (auto props = FindRemoteSession(id);
              props && props->connection_auth_.Get().password_required) {
            ui_->gallery_auth_id = id;
            show_connection_status_window_ = true;
          }
        }
        const bool pending =
            std::any_of(ui_->batch_operations.begin(),
                        ui_->batch_operations.end(), [&](const auto& t) {
                          return t.id == id && t.action == action &&
                                 (t.state == OperationState::Queued ||
                                  t.state == OperationState::Running ||
                                  t.state == OperationState::AwaitingPassword);
                        });
        // Repeated file selections are distinct transfers, even to a busy peer.
        if (pending && action != "file") continue;
        SlintUi::BatchOperation operation;
        operation.id = id;
        operation.action = action;
        operation.file = file;
        if (action == "file") {
          operation.file_id = file_transfer::Session::NextFileId();
          auto& notification = ui_->transfer_notifications[operation.file_id];
          notification.info = queued_file;
          notification.info.file_id = operation.file_id;
          notification.info.remote_id = id;
          if (queued_file.status == RemoteSession::FileTransferStatus::Failed)
            operation.state = OperationState::Finished;
        }
        ui_->batch_operations.push_back(std::move(operation));
      }
      SyncGallery();
    };
    if (action == "file") {
      OpenFileDialog(std::string(tr("选择分发文件")),
          [enqueue](std::string file) {
            if (!file.empty()) enqueue(file);
          });
    } else {
      enqueue({});
    }
    return;
  }
  SyncGallery();
}

void GuiApplication::RestoreWindowGeometry(bool before_show,
                                           bool gallery_window) {
  if (!ui_) return;
  auto& placement = gallery_window ? ui_->gallery_window : ui_->connection_window;
  if (!placement.RestoringNormal()) return;
  auto& component = ui_->Window(gallery_window);
  auto& window = component->window();
  const auto current_size = window.size();
  const bool wall_mode = gallery_window;
  const float compact_width = component->get_minimum_window_width();
  const float compact_height = component->get_minimum_window_height();
  if (!before_show && ui_->loaded_window_scale[gallery_window] > 0) {
    const float ratio = window.scale_factor() / ui_->loaded_window_scale[gallery_window];
    if (placement.wall_position) {
      placement.wall_position->x *= ratio;
      placement.wall_position->y *= ratio;
    }
    if (placement.connection_position) {
      placement.connection_position->x *= ratio;
      placement.connection_position->y *= ratio;
    }
    ui_->loaded_window_scale[gallery_window] = 0;
  }
  auto target_size = placement.RestoreSize(wall_mode, window.scale_factor(),
                                         compact_height, compact_width);
  const auto& position = gallery_window
                             ? placement.wall_position
                             : placement.connection_position;
  auto restore_position = position;
  if (position) {
    auto safe_position = *position;
    // Clamp the requested geometry, never a stale size read immediately
    // after set_size while the compositor is still applying constraints.
    auto safe_size = target_size;
    SDL_Rect available{};
    int display_count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&display_count);
    bool found = false;
    for (int i = 0; displays && i < display_count; ++i) {
      SDL_Rect bounds{};
      if (!SDL_GetDisplayUsableBounds(displays[i], &bounds)) continue;
#if defined(__APPLE__)
      const float scale = window.scale_factor();
      bounds.x *= scale;
      bounds.y *= scale;
      bounds.w *= scale;
      bounds.h *= scale;
#endif
      if (i == 0) available = bounds;
      if (position->x < bounds.x + bounds.w - 60 &&
          position->x + int(safe_size.width) > bounds.x + 60 &&
          position->y >= bounds.y && position->y < bounds.y + bounds.h - 60) {
        available = bounds;
        found = true;
        break;
      }
    }
    SDL_free(displays);
    // Preserve a reachable window's anchor, including intentional edge
    // placement. Only recover coordinates when its display is unavailable.
    if (!found && available.w > 0 && available.h > 0) {
      const auto margin = uint32_t(32 * window.scale_factor());
      const auto minimum = placement.RestoreSize(false, window.scale_factor(),
                                               compact_height, compact_width);
      safe_size.width = std::max(minimum.width,
          std::min(safe_size.width, uint32_t(available.w)));
      safe_size.height = std::max(minimum.height, std::min(
          safe_size.height, uint32_t(std::max(1, available.h - int(margin)))));
      safe_position.x = std::clamp(
          safe_position.x, available.x,
          available.x + std::max(0, available.w - int(safe_size.width)));
      safe_position.y = std::clamp(
          safe_position.y, available.y,
          available.y +
              std::max(0, available.h - int(safe_size.height) - int(margin)));
      if (wall_mode) target_size = safe_size;
    }
    restore_position = safe_position;
  }
  if (before_show) {
    float initial_y_offset = 0;
#if defined(__APPLE__)
    initial_y_offset = MainWindowInitialPositionYOffset();
#endif
    placement.PrepareForShow(window, wall_mode, target_size, restore_position,
                            initial_y_offset);
    return;
  }
  const bool normal_restored = placement.NormalSizeRestored(current_size, target_size);
  if (!normal_restored) window.set_size(target_size);
  if (restore_position) window.set_position(*restore_position);
  if (normal_restored && wall_mode) {
    if (placement.wall_maximized &&
        !placement.wall_fullscreen) {
#if defined(__APPLE__)
      MaximizeGalleryWindowWithoutAnimation();
#else
      window.set_maximized(true);
#endif
    }
    if (placement.wall_fullscreen) {
#if defined(__APPLE__)
      ConfigureMainWindowChrome(window.appkit_view(), true);
      SetGalleryWindowFullscreen(true);
#else
      window.set_fullscreen(true);
#endif
    }
  }
}

void GuiApplication::SyncWindowGeometry(bool gallery_window) {
  auto& component = ui_->Window(gallery_window);
  auto& window = component->window();
  if (!window.is_visible() || window.is_minimized()) return;
  const auto current_size = window.size();
  auto& placement = gallery_window ? ui_->gallery_window : ui_->connection_window;
  const bool wall_mode = gallery_window;
  const bool transitioning = gallery_window && ui_->gallery_transition.Active();
  const float compact_width = component->get_minimum_window_width();
  const float compact_height = component->get_minimum_window_height();
#if defined(__APPLE__)
  const bool fullscreen = gallery_window && IsGalleryWindowFullscreen();
  const bool maximized = gallery_window && IsGalleryWindowMaximized();
#else
  const bool fullscreen = window.is_fullscreen();
  const bool maximized = window.is_maximized();
#endif
  if (!transitioning && placement.RecoverSizeMismatch(wall_mode, window.position(), current_size,
                                 window.scale_factor(), window.is_visible(),
                                 window.is_minimized(), fullscreen, maximized,
                                 compact_height, compact_width)) {
    ui_->restore_ticks[gallery_window] = 0;
    const auto target = placement.RestoreSize(wall_mode, window.scale_factor(),
                                               compact_height, compact_width);
    LOG_WARN("DeskHub window size mismatch: wall={} client={}x{} scale={}; restoring={}x{}",
             wall_mode, current_size.width, current_size.height,
             window.scale_factor(), target.width, target.height);
  }
  if (placement.Restoring() && ++ui_->restore_ticks[gallery_window] == 120) {
    const auto target = placement.RestoreSize(wall_mode, window.scale_factor(),
                                            compact_height, compact_width);
    LOG_WARN("DeskHub window restore delayed: wall={} client={}x{} target={}x{} scale={} fullscreen={} maximized={}",
             wall_mode, current_size.width, current_size.height, target.width,
             target.height, window.scale_factor(), fullscreen, maximized);
  }
  if (placement.RestoringNormal()) {
    RestoreWindowGeometry(false, gallery_window);
  } else if (placement.Restoring()) {
    if (placement.FinishRestore(wall_mode, current_size, fullscreen, maximized))
      LOG_INFO("DeskHub window restored: {}; client={}x{} scale={} fullscreen={} maximized={}",
               wall_mode ? "wall" : "connection", current_size.width,
               current_size.height, window.scale_factor(), fullscreen, maximized);
  } else if (window.is_visible() && !window.is_minimized() && !transitioning) {
    placement.Observe(wall_mode, window.position(), current_size, window.scale_factor(),
                      fullscreen, maximized, compact_height, compact_width);
  }
}

void GuiApplication::SyncGallery() {
  using OperationState = SlintUi::BatchOperation::State;
  if (!ui_) return;
  SyncAudioPlayback();
  SyncWindowGeometry(false);
  SyncWindowGeometry(true);
  auto& window = ui_->wall->window();
  const auto current_size = window.size();
  if (ui_->observed_gallery_size &&
      (current_size.width != ui_->observed_gallery_size->width ||
       current_size.height != ui_->observed_gallery_size->height)) {
    if (!ui_->gallery_control_id.empty() && !ui_->gallery_transition.Active())
      ReleaseControl();
    // Native resize can redraw before the usual 200ms preview refresh.
    ui_->gallery_synced = {};
  }
  ui_->observed_gallery_size = current_size;
  const bool wall_mode = window.is_visible();
  const bool visible = wall_mode && !window.is_minimized();
  auto& placement = ui_->gallery_window;
  auto& transition = ui_->gallery_transition;
  if (!visible) transition.Cancel();
#if defined(__APPLE__)
  const bool fullscreen = IsGalleryWindowFullscreen();
#else
  const bool fullscreen = window.is_fullscreen();
#endif
  if (wall_mode && !placement.Restoring()) {
    if (fullscreen != ui_->wall->get_gallery_fullscreen() && !transition.Active())
      ReleaseControl();
    ui_->wall->set_gallery_fullscreen(fullscreen);
  }
  auto cursor_shape = RemoteCursorShape::default_cursor;
  if (!ui_->gallery_control_id.empty()) {
    // Only the selected control source owns the wall cursor, including group
    // control. Followers can report different shapes without replacing it.
    auto controlled = FindRemoteSession(ui_->gallery_control_id);
    if (!visible || !controlled || controlled->closing_ ||
        controlled->connection_status_ != MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
      ReleaseControl();
    } else {
      HostPlatform platform;
      {
        std::lock_guard lock(controlled->remote_version_mutex_);
        platform = controlled->remote_platform_;
      }
      std::lock_guard lock(controlled->remote_cursor_state_mutex_);
      if (controlled->remote_cursor_state_received_)
        cursor_shape = controlled->cursor_presentation_.Resolve(
            SDL_GetTicks(), platform);
    }
  }
  ui_->wall->set_gallery_remote_cursor_shape(static_cast<int>(cursor_shape));
  const auto now = std::chrono::steady_clock::now();
  const auto action = transition.Advance(now, current_size.width, current_size.height,
                                         window.scale_factor(), fullscreen, placement.Restoring());
  if (action == gallery::FullscreenTransition::Action::Apply) {
    // Keep previews visible while native resizing and the gallery model catch
    // up. Only input and geometry persistence wait for the window to settle.
    ui_->wall->set_gallery_fullscreen(transition.Target());
#if defined(__APPLE__)
    if (!SetGalleryWindowFullscreen(transition.Target())) {
      ui_->wall->set_gallery_fullscreen(fullscreen);
      transition.Cancel();
    } else
#else
    window.set_fullscreen(transition.Target());
#endif
    {
      placement.wall_fullscreen = transition.Target();
    }
    ui_->gallery_synced = {};
  }
  ui_->wall->set_gallery_transitioning(transition.Active());
  if (!transition.Active() && now - ui_->gallery_synced <
      (visible ? 16ms : 200ms))
    return;
  ui_->gallery_synced = now;
  // Track every group, including devices that have never connected, without
  // replacing the recent-device subscription or starting an interactive probe.
  const auto presence_ids = ui_->gallery.Ids();
  if (gallery_presence_ids_ != presence_ids) {
    gallery_presence_ids_ = presence_ids;
    need_to_send_recent_connections_ = true;
  }
  if (ui_->wall->get_gallery_importing()) {
    // Keep the picker selection and ordering stable when presence changes.
    int available = 0, selected = 0;
    for (size_t i = 0; i < ui_->gallery_recent_model->row_count(); ++i) {
      auto device = *ui_->gallery_recent_model->row_data(i);
      const bool online = device_presence_cache_.IsOnline(std::string(device.id));
      const bool added = ui_->gallery.Contains(std::string(device.id));
      if (device.online != online || device.added != added) {
        device.online = online;
        if (device.added != added) device.selected = added;
        device.added = added;
        ui_->gallery_recent_model->set_row_data(i, device);
      }
      if (!device.added) {
        ++available;
        if (device.selected) ++selected;
      }
    }
    ui_->wall->set_gallery_recent_available(available);
    ui_->wall->set_gallery_recent_selected(selected);
  }
  const auto tr = [this](const char* zh) -> std::string {
    return localization::Literal(zh, localization_language_index_).c_str();
  };
  const auto name = [this](const std::string& id) {
    for (const auto& [_, recent] : recent_connections_)
      if (recent.lan.address.empty())
        if (recent.remote_id == id)
          return settings_.RecentConnectionDisplayName(recent);
    Thumbnail::RecentConnection device{};
    device.remote_id = id;
    if (auto props = FindRemoteSession(id)) {
      std::lock_guard lock(props->remote_version_mutex_);
      device.remote_host_name = props->remote_host_name_;
    }
    return settings_.RecentConnectionDisplayName(device);
  };
  if (visible && ui_->wall->get_gallery_network_open() &&
      now - ui_->gallery_network_synced >= 1s) {
    ui_->gallery_network_synced = now;
    // This list follows live connections, independently of the wall's current
    // group and page. Never show saved, queued or still-connecting devices.
    std::vector<std::string> connected_ids;
    {
      std::shared_lock lock(remote_sessions_mutex_);
      for (const auto& [id, props] : remote_sessions_)
        if (props && !props->lan_session_ && !props->closing_ &&
            props->connection_status_ == MiniRtcConnectionStatus::MiniRtcConnectionConnected)
          connected_ids.push_back(id);
    }
    std::sort(connected_ids.begin(), connected_ids.end());
    std::vector<ui::GalleryNetworkDevice> devices;
    for (const auto& id : connected_ids) {
      const auto props = FindRemoteSession(id);
      if (!props || props->closing_ ||
          props->connection_status_ != MiniRtcConnectionStatus::MiniRtcConnectionConnected)
        continue;
      ui::GalleryNetworkDevice device{};
      device.name = UiText(name(id));
      device.id = UiText(FormatPeerId(id.c_str()));
      const auto mode = props->traversal_mode_.load();
      device.mode = UiText(mode == MiniRtcTraversalMode::MiniRtcTraversalP2P
          ? tr("直连")
          : mode == MiniRtcTraversalMode::MiniRtcTraversalUnknown
              ? tr("等待统计")
              : tr("中继"));
      device.inbound = device.outbound = device.loss = "—";
      device.video_latency = "—";
      // Prefer the wall's own frame path. An independently displayed stream
      // may still supply a measurement for a device outside the current page.
      auto video_latency = props->gallery_video_latency_->Get(now);
      if (!video_latency) video_latency = props->video_latency_->Get(now);
      if (video_latency) {
        const auto ms = video_latency->average_ms;
        device.video_latency = UiText(ms < 1 ? "<1 ms"
            : std::to_string(static_cast<int>(std::lround(ms))) + " ms");
      }
      const auto video = props->received_video_stats_.Get(now);
      device.resolution = video.width > 0 && video.height > 0
          ? UiText(FormatResolution(video.width, video.height))
          : UiText("—");
      device.fps = video.fps ? UiText(std::to_string(*video.fps) + " FPS") : UiText("—");
      const auto codecs = props->net_traffic_stats_.LoadVideoCodecs();
      device.encoder = UiText(codecs.remote_encoder.empty() ? "—" : codecs.remote_encoder);
      device.decoder = UiText(codecs.local_decoder.empty() ? "—" : codecs.local_decoder);
      const bool has_report = props->net_traffic_stats_.HasRecentReport(now);
      const auto stats = props->net_traffic_stats_.Load();
      if (mode == MiniRtcTraversalP2P && has_report) {
        device.local_endpoint = UiText(stats.local_endpoint);
        device.remote_endpoint = UiText(stats.remote_endpoint);
      }
      // Use negotiated transport state, never the local encryption preference.
      // Missing/stale reports cannot confirm either encrypted or plaintext.
      device.encryption_known = has_report;
      device.encryption_enabled = has_report && stats.srtp_active;
      if (mode != MiniRtcTraversalMode::MiniRtcTraversalUnknown && has_report) {
        device.inbound = UiText(FormatBitrate(stats.total_inbound_stats.bitrate));
        device.outbound = UiText(FormatBitrate(stats.total_outbound_stats.bitrate));
        // MiniRTC's total loss is a sum of media fractions, not an aggregate.
        // Report video loss explicitly, and only after receiving video packets.
        if (stats.video_inbound_stats.rtp_packet_count > 0 &&
            std::isfinite(stats.video_inbound_stats.loss_rate)) {
          char loss[24];
          std::snprintf(loss, sizeof(loss), "%.1f%%",
              std::clamp(stats.video_inbound_stats.loss_rate, 0.0f, 1.0f) * 100.0f);
          device.loss = UiText(loss);
        }
      }
      devices.push_back(std::move(device));
    }
    if (ui_->gallery_network_model->row_count() != devices.size())
      ui_->gallery_network_model->set_vector(std::move(devices));
    else
      for (size_t i = 0; i < devices.size(); ++i)
        ui_->gallery_network_model->set_row_data(i, devices[i]);
  }
  // Limit simultaneous operations without keeping a user-facing operation list
  // or operation history. Targets stay fixed when the selection changes.
  int running = std::count_if(
      ui_->batch_operations.begin(), ui_->batch_operations.end(),
      [](const auto& t) { return t.state == OperationState::Running; });
  for (auto& operation : ui_->batch_operations) {
    if (operation.state == OperationState::Queued && running < 4) {
      if (operation.action == "connect" &&
          session_cleanup_tasks_.count(operation.id))
        continue;
      operation.started = now;
      operation.state = OperationState::Running;
      ++running;
      auto props = FindRemoteSession(operation.id);
      if (operation.action == "connect") {
        if (props && !props->closing_) {
          // A wall connect can reuse an already connected/connecting peer.
          props->wall_managed_ = true;
          MuteRemoteAudio(props);
        }
        const auto status = props ? props->connection_status_.load()
                                  : MiniRtcConnectionStatus::MiniRtcConnectionClosed;
        const bool usable = props && props->peer_ && !props->closing_;
        const bool connecting = usable &&
            (status == MiniRtcConnectionStatus::MiniRtcConnectionConnecting ||
             status == MiniRtcConnectionStatus::MiniRtcConnectionGathering);
        if (!signal_connected_)
          operation.state = OperationState::Finished;
        else if (!(usable && status == MiniRtcConnectionStatus::MiniRtcConnectionConnected) &&
                 (!connecting || operation.password)) {
          std::string password = operation.password.value_or("");
          bool remember = operation.remember_password;
          if (!operation.password)
            for (const auto& [_, recent] : recent_connections_)
              if (recent.lan.address.empty())
                if (recent.remote_id == operation.id) {
                  password = recent.password;
                  remember = recent.remember_password;
                  break;
                }
          // Join itself determines availability. Background probes must never
          // overwrite the interactive page's single pending presence probe.
          ConnectTo(operation.id, password.c_str(), remember, true, true);
          props = FindRemoteSession(operation.id);
          if (props && props->closing_) {
            // ConnectTo retained these credentials in pending_reconnects_.
            operation.password.reset();
            operation.state = OperationState::Queued;
            --running;
            continue;
          }
          operation.password.reset();
        }
      } else if (!props || !props->peer_ ||
                 props->connection_status_ != MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
        operation.state = OperationState::Finished;
      } else if (operation.action == "file") {
        if (!transfers_.ProcessSelectedFile(operation.file, props,
                                            props->file_label_, {}, operation.file_id))
          operation.state = OperationState::Finished;
      }
      operation.session = props;
    }
    auto props = operation.session.lock();
    if (props && ui_->gallery_requested_auth == operation.id &&
        props->connection_auth_.Get().password_required) {
      ui_->gallery_auth_id = operation.id;
      ui_->gallery_requested_auth.clear();
      show_connection_status_window_ = true;
    }
    if (operation.state == OperationState::Running ||
        operation.state == OperationState::AwaitingPassword) {
      if (!props || props->closing_)
        operation.state = OperationState::Finished;
      else if (operation.action == "connect") {
        const auto status = props->connection_status_.load();
        const auto auth = props->connection_auth_.Get();
        if (status == MiniRtcConnectionStatus::MiniRtcConnectionConnected)
          operation.state = OperationState::Finished;
        else if (auth.password_required && !auth.validating)
          operation.state = OperationState::AwaitingPassword;
        else if (status != MiniRtcConnectionStatus::MiniRtcConnectionConnecting &&
                 status != MiniRtcConnectionStatus::MiniRtcConnectionGathering && !auth.validating)
          operation.state = OperationState::Finished;
        else if (operation.state == OperationState::AwaitingPassword) {
          operation.state = OperationState::Running;
          operation.started = now;
        } else if (now - operation.started > 30s) {
          operation.state = OperationState::Finished;
          DisconnectSession(operation.id);
        }
      } else if (props->connection_status_ != MiniRtcConnectionStatus::MiniRtcConnectionConnected)
        operation.state = OperationState::Finished;
      else if (operation.action == "file") {
        std::lock_guard lock(props->file_transfer_.file_transfer_list_mutex_);
        const auto& files = props->file_transfer_.file_transfer_list_;
        const auto file = std::find_if(files.begin(), files.end(), [&](const auto& info) {
          return info.file_id == operation.file_id;
        });
        if (file == files.end() ||
            file->status == RemoteSession::FileTransferStatus::Completed ||
            file->status == RemoteSession::FileTransferStatus::Failed ||
            file->status == RemoteSession::FileTransferStatus::Cancelled)
          operation.state = OperationState::Finished;
      }
    }
  }
  std::erase_if(ui_->batch_operations, [](const auto& operation) {
    return operation.state == OperationState::Finished;
  });
  SaveGalleryWindowState();

  auto& wall = ui_->gallery;
  std::unordered_map<std::string, std::string> group_labels;
  for (const auto& member : wall.members) {
    if (member.group.empty()) continue;
    auto& label = group_labels[member.id];
    if (!label.empty()) label += " · ";
    label += member.group;
  }
  std::vector<slint::SharedString> groups{
      UiText(tr("全部设备"))};
  for (const auto& group : wall.groups) groups.push_back(UiText(group));
  const auto old_groups = ui_->wall->get_gallery_groups();
  bool changed_groups = !old_groups || old_groups->row_count() != groups.size();
  if (!changed_groups)
    for (size_t i = 0; i < groups.size(); ++i)
      changed_groups |= old_groups->row_data(i) != groups[i];
  if (changed_groups)
    ui_->wall->set_gallery_groups(
        std::make_shared<slint::VectorModel<slint::SharedString>>(groups));
  const int group_index = std::clamp(ui_->wall->get_gallery_group_index(), 0,
                                     int(wall.groups.size()));
  auto ids = wall.Ids(group_index == 0 ? "" : wall.groups[group_index - 1]);
  if (group_index == 0) {
    std::shared_lock lock(remote_sessions_mutex_);
    std::vector<std::string> active;
    for (const auto& [id, props] : remote_sessions_)
      if (props && !props->closing_ && !ui_->gallery_hidden_active_ids.count(id) &&
          std::find(ids.begin(), ids.end(), id) == ids.end())
        active.push_back(id);
    std::sort(active.begin(), active.end());
    ids.insert(ids.end(), active.begin(), active.end());
  }
  ids = wall.OrderIds(ids);
  ui_->gallery_ids = ids;
  std::erase_if(wall.selected, [&](const auto& id) {
    return std::find(ids.begin(), ids.end(), id) == ids.end();
  });
  const int columns = std::max(1, ui_->wall->get_gallery_columns());
  const int rows = std::max(1, ui_->wall->get_gallery_rows());
  wall.ResizePage(columns * rows, ids.size());
  // Resizing/fullscreen changes page capacity. Keep the focused device in
  // the page model so the rail shows the other devices on the same page.
  if (!ui_->gallery_focus.empty()) {
    const auto focused = std::find(ids.begin(), ids.end(), ui_->gallery_focus);
    if (focused != ids.end())
      wall.page = std::distance(ids.begin(), focused) / wall.capacity;
    else {
      ReleaseControl();
      ui_->gallery_focus.clear();
    }
  }
  ui_->wall->set_gallery_page(wall.page);
  ui_->wall->set_gallery_pages(
      std::max(1, (int(ids.size()) + wall.capacity - 1) / wall.capacity));
  ui_->wall->set_gallery_count(ids.size());
  int connected = 0, selected_on_page = 0;
  const auto gallery_viewport = visible && !ui_->gallery_focus.empty()
      ? gallery::PreviewResolution::Viewport(
            ui_->wall->get_gallery_video_width(),
            ui_->wall->get_gallery_video_height(), window.scale_factor())
      : gallery::PreviewResolution{};
  const auto gallery_preview_viewport = visible
      ? gallery::PreviewResolution::Viewport(
            ui_->wall->get_gallery_preview_width(),
            ui_->wall->get_gallery_preview_height(), window.scale_factor())
      : gallery::PreviewResolution{};
  std::set<std::string> preview_ids;
  std::vector<ui::GalleryTile> tiles;
  std::vector<VideoLatencyFrame> submitted_previews;
  const auto make_tile = [&](const std::string& id, bool preview) {
    ui::GalleryTile tile{};
    tile.id = UiText(id);
    tile.name = UiText(name(id));
    tile.group_names = UiText(group_labels[id]);
    tile.selected = wall.selected.count(id);
    auto props = FindRemoteSession(id);
    for (const auto& [_, recent] : recent_connections_) {
      if (!recent.lan.address.empty()) continue;
      if (recent.remote_id != id) continue;
      tile.host_name = UiText(recent.remote_host_name);
      break;
    }
    if (props && tile.host_name.empty()) {
      std::lock_guard lock(props->remote_version_mutex_);
      tile.host_name = UiText(props->remote_host_name_);
    }
    tile.connected = props &&
                     props->connection_status_ == MiniRtcConnectionStatus::MiniRtcConnectionConnected &&
                     !props->closing_;
    const auto online = device_presence_cache_.GetOnline(id);
    tile.online = tile.connected || online.value_or(false);
    tile.presence_known = tile.connected || online.has_value();
    tile.status = UiText(
        tile.connected ? tr("等待画面")
                       : tr("未连接"));
    const bool queued = !tile.connected && std::any_of(
        ui_->batch_operations.begin(), ui_->batch_operations.end(),
        [&](const auto& operation) {
          return operation.id == id && operation.action == "connect" &&
                 operation.state == OperationState::Queued;
        });
    // A queued retry takes precedence over errors from the previous session.
    if (queued)
      tile.status = UiText(tr("队列中"));
    else if (props && props->connection_auth_.Get().password_required)
      tile.status = UiText(tr("需要密码"));
    else if (props && !tile.connected)
      tile.status = UiText(ConnectionStatusText(
          props->connection_status_.load(), localization_language_index_));
    if (preview && visible && tile.connected) {
      preview_ids.insert(id);
      const auto viewport = ui_->gallery_focus == id
          ? gallery_viewport : gallery_preview_viewport;
      props->gallery_preview_width_ =
          std::max({320, viewport.width, viewport.height});
      std::lock_guard lock(props->gallery_frame_mutex_);
      if (props->gallery_rgba_) {
        auto& cached = ui_->gallery_previews[id];
        if (cached.sequence != props->gallery_sequence_ ||
            cached.session.lock() != props) {
          slint::SharedPixelBuffer<slint::Rgba8Pixel> pixels(
              props->gallery_width_, props->gallery_height_);
          std::memcpy(pixels.begin(), props->gallery_rgba_->data(),
                      props->gallery_rgba_->size());
          cached.image = slint::Image(std::move(pixels));
          cached.sequence = props->gallery_sequence_;
          cached.session = props;
          submitted_previews.push_back(props->gallery_frame_timing_);
        }
        tile.frame = cached.image;
        tile.has_frame = true;
        tile.stale = now - props->gallery_frame_at_ > 5s;
        tile.status =
            UiText(tile.stale ? tr("画面已停留")
                              : tr("只读预览"));
      }
    }
    return tile;
  };
  for (size_t i = 0; i < ids.size(); ++i) {
    const auto props = FindRemoteSession(ids[i]);
    if (props && props->connection_status_ == MiniRtcConnectionStatus::MiniRtcConnectionConnected &&
        !props->closing_) {
      ++connected;
    }
    if (i >= size_t(wall.page * wall.capacity) &&
        i < size_t((wall.page + 1) * wall.capacity)) {
      if (wall.selected.count(ids[i])) ++selected_on_page;
      tiles.push_back(make_tile(ids[i], true));
    }
  }
  if (ui_->gallery_model->row_count() != tiles.size())
    ui_->gallery_model->set_vector(std::move(tiles));
  else
    for (size_t i = 0; i < tiles.size(); ++i)
      ui_->gallery_model->set_row_data(i, tiles[i]);
  ui_->wall->set_gallery_focused(ui_->gallery_focus.empty()
                                     ? ui::GalleryTile{}
                                     : make_tile(ui_->gallery_focus, true));
  // Count each newly submitted image once, including conversion and UI queue
  // time. Slint image submission does not confirm physical screen presentation.
  for (const auto& timing : submitted_previews) timing.MarkSubmitted();
  ui_->wall->set_gallery_connected(connected);
  if (!ui_->wall->get_gallery_adding() && !ui_->wall->get_gallery_importing())
    ui_->wall->set_gallery_notice("");
  ui_->wall->set_gallery_selected(wall.selected.size());
  ui_->wall->set_gallery_hidden_selected(wall.selected.size() -
                                         selected_on_page);
  std::vector<ui::GalleryActionDevice> selected_devices;
  bool selected_audio_available = false, selected_audio_muted = true;
  for (const auto& id : ids)
    if (wall.selected.count(id)) {
      ui::GalleryActionDevice device{};
      device.id = UiText(id);
      device.name = UiText(name(id));
      const auto props = FindRemoteSession(id);
      device.connected = props && !props->closing_ &&
                         props->connection_status_ == MiniRtcConnectionStatus::MiniRtcConnectionConnected;
      if (device.connected) {
        selected_audio_available = true;
        selected_audio_muted &= !props->audio_capture_button_pressed_;
      }
      selected_devices.push_back(std::move(device));
    }
  ui_->wall->set_gallery_selected_audio_available(selected_audio_available);
  ui_->wall->set_gallery_selected_audio_muted(selected_audio_available &&
                                             selected_audio_muted);
  if (ui_->gallery_selected_model->row_count() != selected_devices.size())
    ui_->gallery_selected_model->set_vector(std::move(selected_devices));
  else
    for (size_t i = 0; i < selected_devices.size(); ++i) {
      const auto old = *ui_->gallery_selected_model->row_data(i);
      const auto& device = selected_devices[i];
      if (old.id != device.id || old.name != device.name ||
          old.connected != device.connected)
        ui_->gallery_selected_model->set_row_data(i, device);
    }
  auto& pending_previews = ui_->gallery_pending_previews;
  auto& bandwidth_demands = ui_->gallery_bandwidth_demands;
  pending_previews.clear();
  bandwidth_demands.clear();
  {
    std::shared_lock lock(remote_sessions_mutex_);
    pending_previews.reserve(remote_sessions_.size());
    bandwidth_demands.reserve(remote_sessions_.size());
    for (const auto& [id, props] : remote_sessions_) {
      if (!props || props->closing_) continue;
      if (!preview_ids.count(id)) {
        props->gallery_preview_width_ = 0;
        std::unique_lock conversion_lock(props->gallery_conversion_mutex_,
                                         std::try_to_lock);
        if (conversion_lock.owns_lock()) {
          std::vector<unsigned char>().swap(props->gallery_nv12_);
          std::vector<unsigned char>().swap(props->gallery_scaled_);
          std::vector<unsigned char>().swap(props->gallery_scratch_);
        }
        std::lock_guard frame_lock(props->gallery_frame_mutex_);
        props->gallery_rgba_.reset();
      }
      if (props->preview_subscription_supported_ &&
          props->connection_status_ == MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
        if (wall_mode && !ui_->gallery_hidden_active_ids.count(id))
          props->wall_managed_ = true;
        // Keep session quality and audio; wall bandwidth, resolution and
        // cadence requests never overwrite session preferences.
        PreviewSubscription demand;
        {
          std::lock_guard settings_lock(props->video_settings_mutex_);
          demand = PreviewSubscription::Interactive(
              props->video_settings_.quality, props->video_settings_.frame_rate,
              props->video_settings_.preference, props->audio_allowed_.load());
        }
        if (props->wall_managed_)
          wall.stream_settings.ApplyFrameRate(demand, props->preview_fps15_supported_);
        const bool needs_sync = props->preview_needs_sync_.exchange(false);
        const auto* view = FindStreamView(id);
        const bool stream_visible = view && view->Visible() && props->stream_window_open_;
        const bool stream_needs_video = view && props->stream_window_open_ &&
            (stream_visible || !config_center_->IsPauseVideoWhenMinimized());
        if (props->preview_resolution_supported_) {
          const auto wall_viewport = !preview_ids.count(id)
              ? gallery::PreviewResolution{}
              : ui_->gallery_focus == id ? gallery_viewport
                                        : gallery_preview_viewport;
          const auto resolution = props->preview_resolution_.Update(
              wall.stream_settings.Resolution(
                  props->wall_managed_, wall_viewport,
                  stream_visible ? gallery::PreviewResolution::Viewport(
                      view->window->get_video_area_width(), view->window->get_video_area_height(),
                      view->window->window().scale_factor()) : gallery::PreviewResolution{}),
              now, needs_sync);
          demand.version = 2;
          demand.width = resolution.width;
          demand.height = resolution.height;
        }
        if (!preview_ids.count(id) && !stream_needs_video)
          demand.fps = 0;
        gallery::PreviewBandwidthDemand bandwidth;
        if (props->preview_bandwidth_supported_) {
          demand.max_bitrate_bps = 0;
          if (props->wall_managed_ && demand.fps > 0) {
            bandwidth = {
                gallery::PreviewBandwidthDemand::Estimate(
                    demand.width, demand.height,
                    demand.fps == 60 ? demand.frame_rate : demand.fps),
                stream_visible || (visible && ui_->gallery_focus == id)};
          }
        }
        bandwidth_demands.push_back(bandwidth);
        pending_previews.push_back({props, demand, needs_sync});
      }
    }
  }
  const auto& allocations = ui_->gallery_bandwidth_allocator.Allocate(
      bandwidth_demands, wall.stream_settings.bandwidth_mbps * 1'000'000);
  for (size_t i = 0; i < pending_previews.size(); ++i) {
    if (bandwidth_demands[i].desired_bps == 0) continue;
    auto& demand = pending_previews[i].demand;
    // Quantize down to avoid resending subscriptions for sub-kbit rounding.
    const int bitrate = allocations[i] / 1000 * 1000;
    demand.max_bitrate_bps = bitrate;
    if (!bitrate) demand.fps = 0;
  }
  for (auto& pending : pending_previews) {
    auto& props = pending.props;
    auto& demand = pending.demand;
    const bool acknowledged =
        props->preview_ack_.load() == props->preview_request_ &&
        props->preview_accepted_.load();
    if (pending.needs_sync || !demand.SameSettings(props->preview_requested_) ||
        (!acknowledged && now - props->preview_request_at_ > 5s)) {
      demand.request_id = ++props->preview_request_;
      if (!demand.request_id) demand.request_id = ++props->preview_request_;
      const auto message = demand.ToJson().dump();
      if (MiniRtcSendReliableDataFrame(props->peer_, message.data(), message.size(),
                                props->control_data_label_.c_str()) == 0)
        props->preview_requested_ = demand;
      props->preview_request_at_ = now;
    }
  }
  // Reuse capacity without retaining disconnected sessions between refreshes.
  pending_previews.clear();
  std::erase_if(ui_->gallery_previews, [&](const auto& item) {
    return !preview_ids.count(item.first);
  });
}

void GuiApplication::Cleanup() {
  // Deactivate queued presentation work before sessions/renderers are released.
  // This covers every platform: the wakeup may be invoked from a decoder thread
  // that outlives the window.
  std::atomic_store_explicit(&video_frame_wakeup_,
      std::shared_ptr<const std::function<void()>>{}, std::memory_order_release);
#if defined(__APPLE__)
  if (ui_) ui_->video_scheduler.Stop();
#endif
  BeginShutdown();
  while (!ShutdownDrained())
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  remote_input_.Stop();
#if _WIN32
  windows_rdp_handoff_allowed_->store(false, std::memory_order_relaxed);
  windows_rdp_handoff_connected_->store(false, std::memory_order_relaxed);
  if (windows_service_worker_) windows_service_worker_->Reset();
#endif
#if defined(__linux__) && !defined(__OHOS__)
  HeadlessConsole::Instance().BindSettings(nullptr, {}, {});
#endif
  if (!ui_) {
    SDL_Quit();
    return;
  }
  ui_->file_dialog_lifetime.reset();
#if defined(__APPLE__)
  CancelNativeFileDialog();
#endif
  SaveGalleryWindowState();
#if defined(__APPLE__)
  SetGalleryWindowFullscreen(false);
#endif
  ui_->wall->hide();
  for (auto& [_, notification] : ui_->transfer_notifications)
    if (notification.window) (*notification.window)->hide();
  ui_->transfer_notifications.clear();
  if (ui_->clipboard_notification) (*ui_->clipboard_notification)->hide();
  ui_->clipboard_notification.reset();
  ui_->timer.stop();
  ui_->video_timer.stop();
#if _WIN32 && CROSSDESK_PORTABLE
  JoinPortableWindowsServiceInstallThread();
#endif
  privacy_.Disable();
  clipboard_.Shutdown();
  ReleaseControl();
  keyboard_.ReleaseAllRemotePressedKeys("application_exit");
  devices_.DestroyDevices();
  privacy_.Shutdown();
  HandleScreenLock();
  devices_.DestroyFactories();
  CloseAllRemoteSessions();
  WaitForSessionCleanup();
  devices_.DestroyAudioOutput();
  for (auto& [_, view] : ui_->streams) view.SaveSize(*config_center_);
  ui_->streams.clear();
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
  if (ui_->tray) {
    ui_->tray->RemoveTrayIcon();
    ui_->tray.reset();
  }
#endif
  ui_.reset();
  if (update_checker_) {
    update_checker_->Stop();
  }
#if CROSSDESK_HAS_UPDATER
  updater_.reset();
#endif
  SDL_Quit();
}

}  // namespace crossdesk
