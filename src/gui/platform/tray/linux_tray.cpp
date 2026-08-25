#include "linux_tray.h"

#if defined(__linux__) && !defined(__APPLE__)

#include <SDL3/SDL.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xrender.h>
#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "localization.h"
#include "rd_log.h"
#include "stb_image.h"

namespace crossdesk {

namespace {

constexpr int kTrayIconSize = 24;
constexpr int kMenuItemHeight = 28;
constexpr int kMenuItemCount = 3;
constexpr int kMenuVerticalPadding = 4;
constexpr int kMenuHeight =
    kMenuItemHeight * kMenuItemCount + kMenuVerticalPadding * 2;
constexpr int kMenuHorizontalPadding = 12;
constexpr int kMenuCornerRadius = 8;
constexpr int kMenuBorderWidth = 1;
constexpr int kMenuItemHorizontalInset = 4;
constexpr int kMenuItemVerticalInset = 2;
constexpr int kMenuItemCornerRadius = 5;
constexpr int kMenuSeparatorHorizontalInset = 8;
constexpr int kDockTimeoutMs = 800;
constexpr int kMaxTrayEventsPerTick = 32;
constexpr int kMaxTrayEventsDuringEmbed = 16;
constexpr long kSystemTrayRequestDock = 0;
constexpr long kXEmbedMapped = 1;
constexpr long kXEmbedEmbeddedNotify = 0;
constexpr int kAppIndicatorCategoryApplicationStatus = 0;
constexpr int kAppIndicatorStatusPassive = 0;
constexpr int kAppIndicatorStatusActive = 1;

// AppIndicator exports the menu to the desktop shell, which lets GNOME, KDE,
// and compatible panels render it with their own theme. Load either maintained
// implementation dynamically so the legacy XEmbed path remains available.
struct DesktopIndicatorApi {
  using SignalCallback = void (*)(void*, void*);
  using SignalDestroyNotify = void (*)(void*, void*);

  void* library = nullptr;
  void* (*indicator_new_with_path)(const char*, const char*, int,
                                   const char*) = nullptr;
  void (*indicator_set_status)(void*, int) = nullptr;
  void (*indicator_set_menu)(void*, void*) = nullptr;
  void (*indicator_set_title)(void*, const char*) = nullptr;
  int (*gtk_init_check)(int*, char***) = nullptr;
  void* (*gtk_menu_new)() = nullptr;
  void* (*gtk_menu_item_new_with_label)(const char*) = nullptr;
  void* (*gtk_separator_menu_item_new)() = nullptr;
  void (*gtk_menu_shell_append)(void*, void*) = nullptr;
  void (*gtk_widget_show_all)(void*) = nullptr;
  void (*gtk_widget_destroy)(void*) = nullptr;
  int (*gtk_events_pending)() = nullptr;
  int (*gtk_main_iteration_do)(int) = nullptr;
  unsigned long (*signal_connect_data)(void*, const char*, SignalCallback,
                                       void*, SignalDestroyNotify,
                                       int) = nullptr;
  void (*object_unref)(void*) = nullptr;
};

template <typename T>
bool LoadDesktopIndicatorSymbol(void* library, T* function,
                                const char* symbol_name) {
  *function = reinterpret_cast<T>(dlsym(library, symbol_name));
  return *function != nullptr;
}

void UnloadDesktopIndicatorApi(DesktopIndicatorApi* api) {
  if (api->library) {
    dlclose(api->library);
  }
  *api = DesktopIndicatorApi{};
}

bool LoadDesktopIndicatorApi(DesktopIndicatorApi* api) {
  for (const char* library_name : {"libayatana-appindicator3.so.1",
                                   "libappindicator3.so.1"}) {
    api->library = dlopen(library_name, RTLD_NOW | RTLD_LOCAL);
    if (api->library) {
      break;
    }
  }
  if (!api->library) {
    return false;
  }

  const bool loaded =
      LoadDesktopIndicatorSymbol(api->library,
                                 &api->indicator_new_with_path,
                                 "app_indicator_new_with_path") &&
      LoadDesktopIndicatorSymbol(api->library, &api->indicator_set_status,
                                 "app_indicator_set_status") &&
      LoadDesktopIndicatorSymbol(api->library, &api->indicator_set_menu,
                                 "app_indicator_set_menu") &&
      LoadDesktopIndicatorSymbol(api->library, &api->indicator_set_title,
                                 "app_indicator_set_title") &&
      LoadDesktopIndicatorSymbol(api->library, &api->gtk_init_check,
                                 "gtk_init_check") &&
      LoadDesktopIndicatorSymbol(api->library, &api->gtk_menu_new,
                                 "gtk_menu_new") &&
      LoadDesktopIndicatorSymbol(api->library,
                                 &api->gtk_menu_item_new_with_label,
                                 "gtk_menu_item_new_with_label") &&
      LoadDesktopIndicatorSymbol(api->library,
                                 &api->gtk_separator_menu_item_new,
                                 "gtk_separator_menu_item_new") &&
      LoadDesktopIndicatorSymbol(api->library, &api->gtk_menu_shell_append,
                                 "gtk_menu_shell_append") &&
      LoadDesktopIndicatorSymbol(api->library, &api->gtk_widget_show_all,
                                 "gtk_widget_show_all") &&
      LoadDesktopIndicatorSymbol(api->library, &api->gtk_widget_destroy,
                                 "gtk_widget_destroy") &&
      LoadDesktopIndicatorSymbol(api->library, &api->gtk_events_pending,
                                 "gtk_events_pending") &&
      LoadDesktopIndicatorSymbol(api->library, &api->gtk_main_iteration_do,
                                 "gtk_main_iteration_do") &&
      LoadDesktopIndicatorSymbol(api->library, &api->signal_connect_data,
                                 "g_signal_connect_data") &&
      LoadDesktopIndicatorSymbol(api->library, &api->object_unref,
                                 "g_object_unref");
  if (!loaded) {
    UnloadDesktopIndicatorApi(api);
  }
  return loaded;
}

bool IsAsciiPrintable(const std::string& text) {
  for (unsigned char ch : text) {
    if (ch < 0x20 || ch > 0x7e) {
      return false;
    }
  }
  return true;
}

std::string GetShowMainWindowMenuLabel(int language_index) {
  const int normalized_index = localization::detail::ClampLanguageIndex(
      language_index);
  const std::string& label = localization::show_main_window[normalized_index];
  return label.empty() ? "Show Main Window" : label;
}

std::string GetSettingsMenuLabel(int language_index) {
  const int normalized_index = localization::detail::ClampLanguageIndex(
      language_index);
  const std::string& label = localization::settings[normalized_index];
  return label.empty() ? "Settings" : label;
}

std::string GetExitMenuLabel(int language_index) {
  const int normalized_index = localization::detail::ClampLanguageIndex(
      language_index);
  const std::string& label = localization::exit_program[normalized_index];
  return label.empty() ? "Exit" : label;
}

unsigned long AllocateColor(Display* display, int screen, const char* name,
                            unsigned long fallback) {
  if (!display || !name) {
    return fallback;
  }

  XColor parsed{};
  XColor exact{};
  Colormap colormap = DefaultColormap(display, screen);
  if (XAllocNamedColor(display, colormap, name, &parsed, &exact)) {
    return parsed.pixel;
  }

  return fallback;
}

void FillRoundedRectangle(Display* display, Drawable drawable, GC gc, int x,
                          int y, int width, int height, int radius) {
  if (!display || !drawable || !gc || width <= 0 || height <= 0) {
    return;
  }

  const int clamped_radius =
      std::clamp(radius, 0, std::min(width, height) / 2);
  if (clamped_radius == 0) {
    XFillRectangle(display, drawable, gc, x, y, width, height);
    return;
  }

  const int diameter = clamped_radius * 2;
  XFillRectangle(display, drawable, gc, x + clamped_radius, y,
                 width - diameter, height);
  XFillRectangle(display, drawable, gc, x, y + clamped_radius, width,
                 height - diameter);
  XFillArc(display, drawable, gc, x, y, diameter, diameter, 0, 360 * 64);
  XFillArc(display, drawable, gc, x + width - diameter, y, diameter, diameter,
           0, 360 * 64);
  XFillArc(display, drawable, gc, x, y + height - diameter, diameter, diameter,
           0, 360 * 64);
  XFillArc(display, drawable, gc, x + width - diameter,
           y + height - diameter, diameter, diameter, 0, 360 * 64);
}

bool IsPointInsideRoundedRectangle(int point_x, int point_y, int x, int y,
                                   int width, int height, int radius) {
  if (point_x < x || point_y < y || point_x >= x + width ||
      point_y >= y + height) {
    return false;
  }

  const int clamped_radius =
      std::clamp(radius, 0, std::min(width, height) / 2);
  if (clamped_radius == 0 ||
      (point_x >= x + clamped_radius &&
       point_x < x + width - clamped_radius) ||
      (point_y >= y + clamped_radius &&
       point_y < y + height - clamped_radius)) {
    return true;
  }

  const int center_x = point_x < x + clamped_radius
                           ? x + clamped_radius
                           : x + width - clamped_radius - 1;
  const int center_y = point_y < y + clamped_radius
                           ? y + clamped_radius
                           : y + height - clamped_radius - 1;
  const int delta_x = point_x - center_x;
  const int delta_y = point_y - center_y;
  return delta_x * delta_x + delta_y * delta_y <=
         clamped_radius * clamped_radius;
}

std::vector<std::filesystem::path> BuildIconCandidatePaths() {
  std::vector<std::filesystem::path> paths = {
      "icons/linux/crossdesk_32x32.png",
      "icons/linux/crossdesk_24x24.png",
      "icons/linux/crossdesk_48x48.png",
      "icons/linux/crossdesk_64x64.png",
      "icons/linux/crossdesk_96x96.png",
      "icons/linux/crossdesk_128x128.png",
      "icons/linux/crossdesk_256x256.png",
      "icons/linux/crossdesk_512x512.png",
      "/usr/share/icons/hicolor/32x32/apps/crossdesk.png",
      "/usr/share/icons/hicolor/24x24/apps/crossdesk.png",
      "/usr/share/icons/hicolor/48x48/apps/crossdesk.png",
      "/usr/share/icons/hicolor/64x64/apps/crossdesk.png",
      "/usr/share/icons/hicolor/96x96/apps/crossdesk.png",
      "/usr/share/icons/hicolor/128x128/apps/crossdesk.png",
      "/usr/share/icons/hicolor/512x512/apps/crossdesk.png",
      "/usr/share/icons/hicolor/256x256/apps/crossdesk.png",
      "/usr/local/share/icons/hicolor/32x32/apps/crossdesk.png",
      "/usr/local/share/icons/hicolor/24x24/apps/crossdesk.png",
      "/usr/local/share/icons/hicolor/48x48/apps/crossdesk.png",
      "/usr/local/share/icons/hicolor/64x64/apps/crossdesk.png",
      "/usr/local/share/icons/hicolor/96x96/apps/crossdesk.png",
      "/usr/local/share/icons/hicolor/128x128/apps/crossdesk.png",
      "/usr/local/share/icons/hicolor/512x512/apps/crossdesk.png",
      "/usr/local/share/icons/hicolor/256x256/apps/crossdesk.png"};

  const char* base_path = SDL_GetBasePath();
  if (base_path && base_path[0] != '\0') {
    const std::filesystem::path base_dir(base_path);
    for (const char* size :
         {"32", "24", "48", "64", "96", "128", "256", "512", "16"}) {
      paths.push_back(base_dir /
                      ("icons/linux/crossdesk_" + std::string(size) + "x" +
                       size + ".png"));
      paths.push_back(base_dir /
                      ("../share/icons/hicolor/" + std::string(size) + "x" +
                       size + "/apps/crossdesk.png"));
    }
  }

  return paths;
}

std::pair<std::string, std::string> ResolveDesktopIndicatorIcon() {
  for (const auto& candidate : BuildIconCandidatePaths()) {
    std::error_code error;
    if (!std::filesystem::exists(candidate, error) || error) {
      continue;
    }

    const auto absolute_path = std::filesystem::absolute(candidate, error);
    if (!error) {
      return {absolute_path.stem().string(),
              absolute_path.parent_path().string()};
    }
  }
  return {"crossdesk", {}};
}

int CountMaskBits(unsigned long mask) {
  int count = 0;
  while (mask) {
    count += static_cast<int>(mask & 1UL);
    mask >>= 1;
  }
  return count;
}

int CountMaskShift(unsigned long mask) {
  int shift = 0;
  while (mask && (mask & 1UL) == 0) {
    ++shift;
    mask >>= 1;
  }
  return shift;
}

unsigned long ScaleColorToMask(unsigned char value, unsigned long mask) {
  const int bits = CountMaskBits(mask);
  if (bits <= 0) {
    return 0;
  }

  const unsigned long max_value = (1UL << bits) - 1;
  const unsigned long scaled =
      (static_cast<unsigned long>(value) * max_value + 127UL) / 255UL;
  return (scaled << CountMaskShift(mask)) & mask;
}

unsigned char SampleBilinearChannel(const std::vector<unsigned char>& rgba,
                                    int width, int height, double src_x,
                                    double src_y, int channel) {
  src_x = std::clamp(src_x, 0.0, static_cast<double>(width - 1));
  src_y = std::clamp(src_y, 0.0, static_cast<double>(height - 1));

  const int x0 = static_cast<int>(std::floor(src_x));
  const int y0 = static_cast<int>(std::floor(src_y));
  const int x1 = std::min(x0 + 1, width - 1);
  const int y1 = std::min(y0 + 1, height - 1);
  const double fx = src_x - x0;
  const double fy = src_y - y0;

  auto channel_at = [&](int x, int y) -> double {
    const size_t offset =
        (static_cast<size_t>(y) * static_cast<size_t>(width) +
         static_cast<size_t>(x)) *
            4 +
        static_cast<size_t>(channel);
    return rgba[offset];
  };

  const double top =
      channel_at(x0, y0) * (1.0 - fx) + channel_at(x1, y0) * fx;
  const double bottom =
      channel_at(x0, y1) * (1.0 - fx) + channel_at(x1, y1) * fx;
  const double value = top * (1.0 - fy) + bottom * fy;
  return static_cast<unsigned char>(std::clamp(value, 0.0, 255.0) + 0.5);
}

}  // namespace

struct LinuxTrayImpl {
  explicit LinuxTrayImpl(::SDL_Window* window, std::string tray_tooltip,
                         int language_index_value,
                         uint32_t tray_exit_event_type)
      : app_window(window),
        tooltip(std::move(tray_tooltip)),
        language_index(language_index_value),
        exit_event_type(tray_exit_event_type),
        show_menu_label(GetShowMainWindowMenuLabel(language_index_value)),
        show_menu_ascii_label(IsAsciiPrintable(show_menu_label)
                                  ? show_menu_label
                                  : "Show Main Window"),
        settings_menu_label(GetSettingsMenuLabel(language_index_value)),
        settings_menu_ascii_label(IsAsciiPrintable(settings_menu_label)
                                      ? settings_menu_label
                                      : "Settings"),
        exit_menu_label(GetExitMenuLabel(language_index_value)),
        exit_menu_ascii_label(IsAsciiPrintable(exit_menu_label)
                                  ? exit_menu_label
                                  : "Exit") {}

  explicit LinuxTrayImpl(std::function<void()> show_window_callback,
                         std::function<void()> hide_window_callback,
                         std::function<void()> open_settings_callback,
                         std::function<void()> exit_callback,
                         std::string tray_tooltip, int language_index_value)
      : show_window(std::move(show_window_callback)),
        hide_window(std::move(hide_window_callback)),
        open_settings(std::move(open_settings_callback)),
        exit_app(std::move(exit_callback)),
        tooltip(std::move(tray_tooltip)),
        language_index(language_index_value),
        show_menu_label(GetShowMainWindowMenuLabel(language_index_value)),
        show_menu_ascii_label(IsAsciiPrintable(show_menu_label)
                                  ? show_menu_label
                                  : "Show Main Window"),
        settings_menu_label(GetSettingsMenuLabel(language_index_value)),
        settings_menu_ascii_label(IsAsciiPrintable(settings_menu_label)
                                      ? settings_menu_label
                                      : "Settings"),
        exit_menu_label(GetExitMenuLabel(language_index_value)),
        exit_menu_ascii_label(IsAsciiPrintable(exit_menu_label)
                                  ? exit_menu_label
                                  : "Exit") {}

  ~LinuxTrayImpl() { RemoveTrayIcon(); }

  bool MinimizeToTray() {
    if (!EnsureTrayIcon()) {
      return false;
    }

    if (hide_window) {
      hide_window();
    } else if (app_window) {
      SDL_HideWindow(app_window);
    }
    return true;
  }

  void RemoveTrayIcon() {
    RemoveDesktopIndicator();
    HideMenu();

    if (display && icon_window) {
      XUnmapWindow(display, icon_window);
      XDestroyWindow(display, icon_window);
      icon_window = 0;
    }

    if (icon_colormap_owned && display && icon_colormap) {
      XFreeColormap(display, icon_colormap);
      icon_colormap = 0;
      icon_colormap_owned = false;
    }

    if (font_set) {
      XFreeFontSet(display, font_set);
      font_set = nullptr;
    }

    if (fallback_font) {
      XFreeFont(display, fallback_font);
      fallback_font = nullptr;
    }

    if (menu_font) {
      XftFontClose(display, menu_font);
      menu_font = nullptr;
    }

    if (menu_text_color_allocated) {
      XftColorFree(display, DefaultVisual(display, screen),
                   DefaultColormap(display, screen), &menu_text_color);
      menu_text_color_allocated = false;
    }

    if (display) {
      XCloseDisplay(display);
      display = nullptr;
    }

    docked = false;
    embedded = false;
    icon_needs_redraw = false;
    tray_manager_window = 0;
  }

  void ProcessEvents() {
    ProcessDesktopIndicatorEvents();
    ProcessPendingEvents(kMaxTrayEventsPerTick);
  }

 private:
  static void OnDesktopShowWindow(void*, void* user_data) {
    auto* self = static_cast<LinuxTrayImpl*>(user_data);
    if (self) {
      self->ShowWindow();
    }
  }

  static void OnDesktopOpenSettings(void*, void* user_data) {
    auto* self = static_cast<LinuxTrayImpl*>(user_data);
    if (self) {
      self->OpenSettings();
    }
  }

  static void OnDesktopExit(void*, void* user_data) {
    auto* self = static_cast<LinuxTrayImpl*>(user_data);
    if (self) {
      self->RequestExit();
    }
  }

  bool EnsureDesktopIndicator() {
    if (desktop_indicator) {
      return true;
    }
    if (desktop_indicator_attempted) {
      return false;
    }
    desktop_indicator_attempted = true;

    if (!LoadDesktopIndicatorApi(&desktop_indicator_api)) {
      LOG_INFO(
          "Desktop AppIndicator runtime unavailable; using XEmbed tray "
          "fallback");
      return false;
    }
    if (!desktop_indicator_api.gtk_init_check(nullptr, nullptr)) {
      LOG_INFO(
          "GTK could not connect to the desktop; using XEmbed tray fallback");
      return false;
    }

    desktop_menu = desktop_indicator_api.gtk_menu_new();
    void* show_item = desktop_indicator_api.gtk_menu_item_new_with_label(
        show_menu_label.c_str());
    void* settings_item = desktop_indicator_api.gtk_menu_item_new_with_label(
        settings_menu_label.c_str());
    void* separator = desktop_indicator_api.gtk_separator_menu_item_new();
    void* exit_item = desktop_indicator_api.gtk_menu_item_new_with_label(
        exit_menu_label.c_str());
    if (!desktop_menu || !show_item || !settings_item || !separator ||
        !exit_item) {
      if (desktop_menu) {
        desktop_indicator_api.gtk_widget_destroy(desktop_menu);
        desktop_menu = nullptr;
      }
      return false;
    }

    desktop_indicator_api.signal_connect_data(
        show_item, "activate", &LinuxTrayImpl::OnDesktopShowWindow, this,
        nullptr, 0);
    desktop_indicator_api.signal_connect_data(
        settings_item, "activate", &LinuxTrayImpl::OnDesktopOpenSettings, this,
        nullptr, 0);
    desktop_indicator_api.signal_connect_data(
        exit_item, "activate", &LinuxTrayImpl::OnDesktopExit, this, nullptr, 0);
    desktop_indicator_api.gtk_menu_shell_append(desktop_menu, show_item);
    desktop_indicator_api.gtk_menu_shell_append(desktop_menu, settings_item);
    desktop_indicator_api.gtk_menu_shell_append(desktop_menu, separator);
    desktop_indicator_api.gtk_menu_shell_append(desktop_menu, exit_item);
    desktop_indicator_api.gtk_widget_show_all(desktop_menu);

    const auto [icon_name, icon_path] = ResolveDesktopIndicatorIcon();
    desktop_indicator = desktop_indicator_api.indicator_new_with_path(
        "crossdesk", icon_name.c_str(),
        kAppIndicatorCategoryApplicationStatus,
        icon_path.empty() ? nullptr : icon_path.c_str());
    if (!desktop_indicator) {
      desktop_indicator_api.gtk_widget_destroy(desktop_menu);
      desktop_menu = nullptr;
      return false;
    }

    desktop_indicator_api.indicator_set_title(desktop_indicator,
                                               tooltip.c_str());
    desktop_indicator_api.indicator_set_menu(desktop_indicator, desktop_menu);
    desktop_indicator_api.indicator_set_status(desktop_indicator,
                                               kAppIndicatorStatusActive);
    ProcessDesktopIndicatorEvents();
    LOG_INFO("Linux tray registered through the desktop AppIndicator service");
    return true;
  }

  void RemoveDesktopIndicator() {
    if (desktop_indicator) {
      desktop_indicator_api.indicator_set_status(
          desktop_indicator, kAppIndicatorStatusPassive);
    }
    if (desktop_menu) {
      desktop_indicator_api.gtk_widget_destroy(desktop_menu);
      desktop_menu = nullptr;
    }
    if (desktop_indicator) {
      desktop_indicator_api.object_unref(desktop_indicator);
      desktop_indicator = nullptr;
    }
    // GTK registers process-wide types and callbacks. Keep the dynamically
    // loaded module resident after initialization and let process teardown
    // release it, instead of invalidating those registrations with dlclose().
  }

  void ProcessDesktopIndicatorEvents() {
    if (!desktop_indicator) {
      return;
    }

    int processed = 0;
    while (processed < kMaxTrayEventsPerTick &&
           desktop_indicator_api.gtk_events_pending()) {
      desktop_indicator_api.gtk_main_iteration_do(0);
      ++processed;
    }
  }

  int ProcessPendingEvents(int max_events) {
    if (!display) {
      return 0;
    }

    const int event_limit = std::max(1, max_events);
    int processed = 0;
    while (processed < event_limit && XPending(display) > 0) {
      XEvent event{};
      XNextEvent(display, &event);
      HandleEvent(event);
      ++processed;
    }

    if (icon_needs_redraw && display && icon_window) {
      icon_needs_redraw = false;
      DrawIcon();
    }

    return processed;
  }

  void MarkIconDirty() { icon_needs_redraw = true; }

  bool EnsureDisplay() {
    if (display) {
      return true;
    }

    display = XOpenDisplay(nullptr);
    if (!display) {
      LOG_WARN("Linux tray unavailable: failed to open X11 display");
      return false;
    }

    std::setlocale(LC_CTYPE, "");

    screen = DefaultScreen(display);
    root_window = RootWindow(display, screen);
    black_pixel = BlackPixel(display, screen);
    white_pixel = WhitePixel(display, screen);
    icon_visual = DefaultVisual(display, screen);
    icon_depth = DefaultDepth(display, screen);
    icon_colormap = DefaultColormap(display, screen);
    SelectArgbVisual();
    brand_pixel = AllocateColor(display, screen, "#2563eb", black_pixel);
    hover_pixel = AllocateColor(display, screen, "#e5e7eb", white_pixel);
    menu_border_pixel =
        AllocateColor(display, screen, "#d1d5db", black_pixel);
    int shape_event_base = 0;
    int shape_error_base = 0;
    shape_available =
        XShapeQueryExtension(display, &shape_event_base, &shape_error_base);

    char selection_name[64] = {};
    std::snprintf(selection_name, sizeof(selection_name),
                  "_NET_SYSTEM_TRAY_S%d", screen);
    selection_atom = XInternAtom(display, selection_name, False);
    tray_opcode_atom = XInternAtom(display, "_NET_SYSTEM_TRAY_OPCODE", False);
    xembed_atom = XInternAtom(display, "_XEMBED", False);
    xembed_info_atom = XInternAtom(display, "_XEMBED_INFO", False);
    utf8_string_atom = XInternAtom(display, "UTF8_STRING", False);
    net_wm_name_atom = XInternAtom(display, "_NET_WM_NAME", False);
    net_wm_window_type_atom =
        XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    net_wm_window_type_popup_menu_atom =
        XInternAtom(display, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);

    char** missing_charset_list = nullptr;
    int missing_charset_count = 0;
    char* default_string = nullptr;
    font_set = XCreateFontSet(display,
                              "-*-*-medium-r-normal--14-*-*-*-*-*-*-*",
                              &missing_charset_list, &missing_charset_count,
                              &default_string);
    if (missing_charset_list) {
      XFreeStringList(missing_charset_list);
    }
    fallback_font = XLoadQueryFont(display, "fixed");
    menu_font = XftFontOpenName(display, screen, "Sans-10");
    menu_text_color_allocated =
        XftColorAllocName(display, DefaultVisual(display, screen),
                          DefaultColormap(display, screen), "#111827",
                          &menu_text_color);
    return true;
  }

  void SelectArgbVisual() {
    XVisualInfo visual_template{};
    visual_template.screen = screen;
    int visual_count = 0;
    XVisualInfo* visual_info =
        XGetVisualInfo(display, VisualScreenMask, &visual_template,
                       &visual_count);
    if (!visual_info) {
      return;
    }

    for (int i = 0; i < visual_count; ++i) {
      XRenderPictFormat* format =
          XRenderFindVisualFormat(display, visual_info[i].visual);
      if (!format || format->type != PictTypeDirect ||
          format->direct.alphaMask == 0) {
        continue;
      }

      Colormap colormap =
          XCreateColormap(display, root_window, visual_info[i].visual,
                          AllocNone);
      if (!colormap) {
        continue;
      }

      icon_visual = visual_info[i].visual;
      icon_depth = visual_info[i].depth;
      icon_format = format;
      icon_colormap = colormap;
      icon_colormap_owned = true;
      break;
    }

    XFree(visual_info);
  }

  bool EnsureTrayIcon() {
    if (EnsureDesktopIndicator()) {
      return true;
    }
    if (docked && icon_window) {
      return true;
    }

    if (!EnsureDisplay() || !EnsureIconWindow()) {
      return false;
    }

    tray_manager_window = XGetSelectionOwner(display, selection_atom);
    if (!tray_manager_window) {
      LOG_WARN("Linux tray unavailable: no X11 system tray manager");
      return false;
    }

    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = tray_manager_window;
    event.xclient.message_type = tray_opcode_atom;
    event.xclient.format = 32;
    event.xclient.data.l[0] = CurrentTime;
    event.xclient.data.l[1] = kSystemTrayRequestDock;
    event.xclient.data.l[2] = icon_window;
    event.xclient.data.l[3] = 0;
    event.xclient.data.l[4] = 0;

    embedded = false;
    XSendEvent(display, tray_manager_window, False, NoEventMask, &event);
    XFlush(display);

    if (!WaitForEmbed()) {
      LOG_WARN("Linux tray unavailable: tray manager did not embed icon");
      XUnmapWindow(display, icon_window);
      XDestroyWindow(display, icon_window);
      icon_window = 0;
      tray_manager_window = 0;
      return false;
    }

    docked = true;
    LOG_INFO("Linux tray icon embedded");
    XMapRaised(display, icon_window);
    icon_needs_redraw = false;
    DrawIcon();
    return true;
  }

  bool EnsureIconWindow() {
    if (icon_window) {
      return true;
    }

    XSetWindowAttributes attrs{};
    attrs.background_pixel = 0;
    attrs.border_pixel = 0;
    attrs.colormap = icon_colormap;
    attrs.override_redirect = True;
    attrs.event_mask =
        ExposureMask | ButtonPressMask | ButtonReleaseMask | StructureNotifyMask;
    icon_window = XCreateWindow(
        display, root_window, 0, 0, kTrayIconSize, kTrayIconSize, 0,
        icon_depth, InputOutput, icon_visual,
        CWBackPixel | CWBorderPixel | CWColormap | CWOverrideRedirect |
            CWEventMask,
        &attrs);
    if (!icon_window) {
      LOG_WARN("Linux tray unavailable: failed to create icon window");
      return false;
    }

    XSizeHints size_hints{};
    size_hints.flags = PMinSize | PMaxSize | PBaseSize;
    size_hints.min_width = kTrayIconSize;
    size_hints.min_height = kTrayIconSize;
    size_hints.max_width = kTrayIconSize;
    size_hints.max_height = kTrayIconSize;
    size_hints.base_width = kTrayIconSize;
    size_hints.base_height = kTrayIconSize;
    XSetWMNormalHints(display, icon_window, &size_hints);

    XClassHint class_hint{};
    class_hint.res_name = const_cast<char*>("crossdesk");
    class_hint.res_class = const_cast<char*>("CrossDesk");
    XSetClassHint(display, icon_window, &class_hint);

    XStoreName(display, icon_window, tooltip.c_str());
    if (utf8_string_atom && net_wm_name_atom) {
      XChangeProperty(display, icon_window, net_wm_name_atom, utf8_string_atom,
                      8, PropModeReplace,
                      reinterpret_cast<const unsigned char*>(tooltip.c_str()),
                      static_cast<int>(tooltip.size()));
    }

    long xembed_info[2] = {0, kXEmbedMapped};
    XChangeProperty(display, icon_window, xembed_info_atom, xembed_info_atom, 32,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char*>(xembed_info), 2);

    XWMHints wm_hints{};
    wm_hints.flags = InputHint;
    wm_hints.input = True;
    XSetWMHints(display, icon_window, &wm_hints);
    return true;
  }

  void HandleEvent(const XEvent& event) {
    switch (event.type) {
      case Expose:
        if (event.xexpose.window == icon_window) {
          MarkIconDirty();
        } else if (event.xexpose.window == menu_window) {
          DrawMenu();
        }
        break;

      case ConfigureNotify:
        if (event.xconfigure.window == icon_window) {
          MarkIconDirty();
        }
        break;

      case DestroyNotify:
        if (event.xdestroywindow.window == icon_window) {
          icon_window = 0;
          docked = false;
          embedded = false;
          icon_needs_redraw = false;
        } else if (event.xdestroywindow.window == menu_window) {
          menu_window = 0;
          menu_visible = false;
        }
        break;

      case ReparentNotify:
        if (event.xreparent.window == icon_window &&
            event.xreparent.parent == tray_manager_window) {
          embedded = true;
          MarkIconDirty();
        }
        break;

      case ClientMessage:
        if (event.xclient.window == icon_window &&
            event.xclient.format == 32 &&
            event.xclient.message_type == xembed_atom &&
            event.xclient.data.l[1] == kXEmbedEmbeddedNotify) {
          embedded = true;
          MarkIconDirty();
        }
        break;

      case ButtonRelease:
        HandleButtonRelease(event.xbutton);
        break;

      case MotionNotify:
        if (event.xmotion.window == menu_window) {
          const int hovered_item =
              MenuItemAt(event.xmotion.x, event.xmotion.y);
          if (hovered_item != hovered_menu_item) {
            hovered_menu_item = hovered_item;
            DrawMenu();
          }
        }
        break;

      case LeaveNotify:
        if (event.xcrossing.window == menu_window) {
          HideMenu();
        }
        break;

      default:
        break;
    }
  }

  void HandleButtonRelease(const XButtonEvent& event) {
    if (menu_window && event.window == menu_window) {
      const int selected_item =
          event.button == Button1 ? MenuItemAt(event.x, event.y) : -1;
      HideMenu();
      if (selected_item == 0) {
        ShowWindow();
      } else if (selected_item == 1) {
        OpenSettings();
      } else if (selected_item == 2) {
        RequestExit();
      }
      return;
    }

    if (event.window != icon_window) {
      if (menu_visible) {
        HideMenu();
      }
      return;
    }

    if (event.button == Button1 || event.button == Button3) {
      ShowMenu(event.x_root, event.y_root);
    }
  }

  bool WaitForEmbed() {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kDockTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
      ProcessPendingEvents(kMaxTrayEventsDuringEmbed);
      if (embedded) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return embedded;
  }

  void DrawIcon() {
    if (!display || !icon_window) {
      return;
    }

    XWindowAttributes attributes{};
    if (!XGetWindowAttributes(display, icon_window, &attributes)) {
      return;
    }

    const int width = std::max(1, attributes.width);
    const int height = std::max(1, attributes.height);

    if (DrawProgramIcon(width, height)) {
      return;
    }

    const int side = std::min(width, height);
    const int shape_x = (width - side) / 2;
    const int shape_y = (height - side) / 2;
    ApplyIconShape(shape_x, shape_y, side);
    const int pad = std::max(2, side / 8);
    const int box = std::max(1, side - pad * 2);

    GC gc = XCreateGC(display, icon_window, 0, nullptr);
    XSetForeground(display, gc, white_pixel);
    XFillRectangle(display, icon_window, gc, 0, 0, width, height);
    XSetForeground(display, gc, brand_pixel);
    XFillArc(display, icon_window, gc, (width - box) / 2, (height - box) / 2,
             box, box, 0, 360 * 64);

    const char* mark = "C";
    XSetForeground(display, gc, white_pixel);
    if (fallback_font) {
      XSetFont(display, gc, fallback_font->fid);
      const int text_width = XTextWidth(fallback_font, mark, 1);
      const int x = (width - text_width) / 2;
      const int y = (height + fallback_font->ascent - fallback_font->descent) /
                    2;
      XDrawString(display, icon_window, gc, x, y, mark, 1);
    } else {
      XDrawString(display, icon_window, gc, width / 2 - 3, height / 2 + 4,
                  mark, 1);
    }

    XFreeGC(display, gc);
    XFlush(display);
  }

  bool DrawProgramIcon(int width, int height) {
    if (!LoadProgramIcon()) {
      return false;
    }

    const int image_size = std::min(width, height);
    if (image_size <= 0) {
      return false;
    }

    const int image_x = (width - image_size) / 2;
    const int image_y = (height - image_size) / 2;
    ApplyIconShape(image_x, image_y, image_size);
    XImage* image = XCreateImage(display, icon_visual, icon_depth, ZPixmap, 0,
                                 nullptr, width, height, 32, 0);
    if (!image) {
      return false;
    }

    if (image->red_mask == 0 || image->green_mask == 0 ||
        image->blue_mask == 0) {
      XDestroyImage(image);
      return false;
    }

    image->data = static_cast<char*>(
        std::calloc(static_cast<size_t>(image->bytes_per_line),
                    static_cast<size_t>(height)));
    if (!image->data) {
      XDestroyImage(image);
      return false;
    }

    const unsigned long background_pixel = RgbaToPixel(image, 0, 0, 0, 0);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        XPutPixel(image, x, y, background_pixel);
      }
    }

    Pixmap shape_mask = 0;
    GC shape_gc = nullptr;
    if (shape_available) {
      shape_mask = XCreatePixmap(display, icon_window, width, height, 1);
      if (shape_mask) {
        shape_gc = XCreateGC(display, shape_mask, 0, nullptr);
        XSetForeground(display, shape_gc, 0);
        XFillRectangle(display, shape_mask, shape_gc, 0, 0, width, height);
        XSetForeground(display, shape_gc, 1);
      }
    }

    for (int y = 0; y < height; ++y) {
      if (y < image_y || y >= image_y + image_size) {
        continue;
      }
      const double src_y =
          ((static_cast<double>(y - image_y) + 0.5) * icon_height /
           image_size) -
          0.5;
      for (int x = 0; x < width; ++x) {
        if (x < image_x || x >= image_x + image_size) {
          continue;
        }
        const double src_x =
            ((static_cast<double>(x - image_x) + 0.5) * icon_width /
             image_size) -
            0.5;
        const unsigned char src_r =
            SampleBilinearChannel(icon_rgba, icon_width, icon_height, src_x,
                                  src_y, 0);
        const unsigned char src_g =
            SampleBilinearChannel(icon_rgba, icon_width, icon_height, src_x,
                                  src_y, 1);
        const unsigned char src_b =
            SampleBilinearChannel(icon_rgba, icon_width, icon_height, src_x,
                                  src_y, 2);
        const unsigned char src_a =
            SampleBilinearChannel(icon_rgba, icon_width, icon_height, src_x,
                                  src_y, 3);
        const unsigned long pixel =
            RgbaToPixel(image, src_r, src_g, src_b, src_a);
        XPutPixel(image, x, y, pixel);
        if (shape_gc && src_a > 24) {
          XDrawPoint(display, shape_mask, shape_gc, x, y);
        }
      }
    }

    if (shape_mask) {
      XShapeCombineMask(display, icon_window, ShapeBounding, 0, 0, shape_mask,
                        ShapeSet);
      XShapeCombineMask(display, icon_window, ShapeInput, 0, 0, shape_mask,
                        ShapeSet);
      if (shape_gc) {
        XFreeGC(display, shape_gc);
      }
      XFreePixmap(display, shape_mask);
    }

    GC gc = XCreateGC(display, icon_window, 0, nullptr);
    XPutImage(display, icon_window, gc, image, 0, 0, 0, 0, width, height);
    XFreeGC(display, gc);
    XDestroyImage(image);
    XFlush(display);
    return true;
  }

  void ApplyIconShape(int x, int y, int size) {
    if (!shape_available || !display || !icon_window || size <= 0) {
      return;
    }

    XRectangle rect{};
    rect.x = static_cast<short>(x);
    rect.y = static_cast<short>(y);
    rect.width = static_cast<unsigned short>(size);
    rect.height = static_cast<unsigned short>(size);
    XShapeCombineRectangles(display, icon_window, ShapeBounding, 0, 0, &rect, 1,
                            ShapeSet, Unsorted);
    XShapeCombineRectangles(display, icon_window, ShapeInput, 0, 0, &rect, 1,
                            ShapeSet, Unsorted);
  }

  unsigned long RgbaToPixel(XImage* image, unsigned char r, unsigned char g,
                            unsigned char b, unsigned char a) const {
    unsigned char out_r = r;
    unsigned char out_g = g;
    unsigned char out_b = b;

    unsigned long alpha_mask = 0;
    if (icon_format && icon_format->type == PictTypeDirect &&
        icon_format->direct.alphaMask > 0) {
      alpha_mask = ((1UL << icon_format->direct.alphaMask) - 1UL)
                   << icon_format->direct.alpha;
      out_r = static_cast<unsigned char>(
          (static_cast<unsigned int>(r) * a + 127U) / 255U);
      out_g = static_cast<unsigned char>(
          (static_cast<unsigned int>(g) * a + 127U) / 255U);
      out_b = static_cast<unsigned char>(
          (static_cast<unsigned int>(b) * a + 127U) / 255U);
    }

    return ScaleColorToMask(out_r, image->red_mask) |
           ScaleColorToMask(out_g, image->green_mask) |
           ScaleColorToMask(out_b, image->blue_mask) |
           ScaleColorToMask(a, alpha_mask);
  }

  bool LoadProgramIcon() {
    if (!icon_rgba.empty()) {
      return true;
    }

    for (const auto& candidate : BuildIconCandidatePaths()) {
      if (!std::filesystem::exists(candidate)) {
        continue;
      }

      int width = 0;
      int height = 0;
      int channels = 0;
      unsigned char* pixels =
          stbi_load(candidate.string().c_str(), &width, &height, &channels, 4);
      if (!pixels || width <= 0 || height <= 0) {
        if (pixels) {
          stbi_image_free(pixels);
        }
        continue;
      }

      icon_width = width;
      icon_height = height;
      icon_rgba.assign(pixels, pixels + static_cast<size_t>(width) *
                                           static_cast<size_t>(height) * 4);
      stbi_image_free(pixels);
      LOG_INFO("Loaded Linux tray icon: {}", candidate.string());
      return true;
    }

    LOG_WARN("Linux tray icon not found, using fallback icon");
    return false;
  }

  int MenuTextWidth(const std::string& label,
                    const std::string& ascii_label) const {
    if (menu_font) {
      XGlyphInfo extents{};
      XftTextExtentsUtf8(display, menu_font,
                         reinterpret_cast<const FcChar8*>(label.c_str()),
                         static_cast<int>(label.size()), &extents);
      return extents.xOff;
    }

    if (font_set) {
      XRectangle ink{};
      XRectangle logical{};
      Xutf8TextExtents(font_set, label.c_str(), static_cast<int>(label.size()),
                       &ink, &logical);
      return logical.width;
    }

    if (fallback_font) {
      return XTextWidth(fallback_font, ascii_label.c_str(),
                        static_cast<int>(ascii_label.size()));
    }

    return static_cast<int>(ascii_label.size()) * 8;
  }

  int MenuItemAt(int x, int y) const {
    if (!IsPointInsideRoundedRectangle(x, y, 0, 0, menu_width, kMenuHeight,
                                       kMenuCornerRadius)) {
      return -1;
    }

    const int content_y = y - kMenuVerticalPadding;
    if (content_y < 0 ||
        content_y >= kMenuItemHeight * kMenuItemCount) {
      return -1;
    }
    return content_y / kMenuItemHeight;
  }

  void ApplyMenuShape() {
    if (!shape_available || !display || !menu_window) {
      return;
    }

    Pixmap shape_mask =
        XCreatePixmap(display, menu_window, menu_width, kMenuHeight, 1);
    if (!shape_mask) {
      return;
    }

    GC shape_gc = XCreateGC(display, shape_mask, 0, nullptr);
    if (!shape_gc) {
      XFreePixmap(display, shape_mask);
      return;
    }

    XSetForeground(display, shape_gc, 0);
    XFillRectangle(display, shape_mask, shape_gc, 0, 0, menu_width,
                   kMenuHeight);
    XSetForeground(display, shape_gc, 1);
    FillRoundedRectangle(display, shape_mask, shape_gc, 0, 0, menu_width,
                         kMenuHeight, kMenuCornerRadius);
    XShapeCombineMask(display, menu_window, ShapeBounding, 0, 0, shape_mask,
                      ShapeSet);
    XShapeCombineMask(display, menu_window, ShapeInput, 0, 0, shape_mask,
                      ShapeSet);

    XFreeGC(display, shape_gc);
    XFreePixmap(display, shape_mask);
  }

  void ShowMenu(int root_x, int root_y) {
    if (!display) {
      return;
    }

    HideMenu();

    menu_width =
        std::max({72,
                  MenuTextWidth(show_menu_label, show_menu_ascii_label) +
                      kMenuHorizontalPadding * 2,
                  MenuTextWidth(settings_menu_label,
                                settings_menu_ascii_label) +
                      kMenuHorizontalPadding * 2,
                  MenuTextWidth(exit_menu_label, exit_menu_ascii_label) +
                      kMenuHorizontalPadding * 2});
    const int display_width = DisplayWidth(display, screen);
    const int display_height = DisplayHeight(display, screen);
    const int x = std::clamp(root_x, 0, std::max(0, display_width - menu_width));
    const int y = std::clamp(root_y, 0,
                             std::max(0, display_height - kMenuHeight));

    XSetWindowAttributes attrs{};
    attrs.override_redirect = True;
    attrs.background_pixel = menu_border_pixel;
    attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask | LeaveWindowMask;
    menu_window = XCreateWindow(
        display, root_window, x, y, menu_width, kMenuHeight, 0, CopyFromParent,
        InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWEventMask, &attrs);
    if (!menu_window) {
      return;
    }

    if (net_wm_window_type_atom && net_wm_window_type_popup_menu_atom) {
      const Atom window_type = net_wm_window_type_popup_menu_atom;
      XChangeProperty(display, menu_window, net_wm_window_type_atom, XA_ATOM,
                      32, PropModeReplace,
                      reinterpret_cast<const unsigned char*>(&window_type), 1);
    }
    ApplyMenuShape();

    menu_visible = true;
    hovered_menu_item = -1;
    XMapRaised(display, menu_window);
    XGrabPointer(display, menu_window, False,
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    DrawMenu();
    XFlush(display);
  }

  void HideMenu() {
    if (!display || !menu_window) {
      menu_visible = false;
      hovered_menu_item = -1;
      return;
    }

    XUngrabPointer(display, CurrentTime);
    XDestroyWindow(display, menu_window);
    menu_window = 0;
    menu_visible = false;
    hovered_menu_item = -1;
    XFlush(display);
  }

  void DrawMenu() {
    if (!display || !menu_window) {
      return;
    }

    GC gc = XCreateGC(display, menu_window, 0, nullptr);
    XSetForeground(display, gc, menu_border_pixel);
    FillRoundedRectangle(display, menu_window, gc, 0, 0, menu_width,
                         kMenuHeight, kMenuCornerRadius);
    XSetForeground(display, gc, white_pixel);
    FillRoundedRectangle(
        display, menu_window, gc, kMenuBorderWidth, kMenuBorderWidth,
        menu_width - kMenuBorderWidth * 2,
        kMenuHeight - kMenuBorderWidth * 2,
        kMenuCornerRadius - kMenuBorderWidth);
    if (hovered_menu_item >= 0 && hovered_menu_item < kMenuItemCount) {
      XSetForeground(display, gc, hover_pixel);
      FillRoundedRectangle(
          display, menu_window, gc, kMenuItemHorizontalInset,
          kMenuVerticalPadding + hovered_menu_item * kMenuItemHeight +
              kMenuItemVerticalInset,
          menu_width - kMenuItemHorizontalInset * 2,
          kMenuItemHeight - kMenuItemVerticalInset * 2,
          kMenuItemCornerRadius);
    }
    XSetForeground(display, gc, hover_pixel);
    XDrawLine(display, menu_window, gc, kMenuSeparatorHorizontalInset,
              kMenuVerticalPadding + kMenuItemHeight * 2,
              menu_width - kMenuSeparatorHorizontalInset,
              kMenuVerticalPadding + kMenuItemHeight * 2);
    XSetForeground(display, gc, black_pixel);

    XftDraw* xft_draw = menu_font && menu_text_color_allocated
                            ? XftDrawCreate(display, menu_window,
                                            DefaultVisual(display, screen),
                                            DefaultColormap(display, screen))
                            : nullptr;
    auto draw_label = [&](int item_index, const std::string& label,
                          const std::string& ascii_label) {
      const int item_y =
          kMenuVerticalPadding + item_index * kMenuItemHeight;
      int baseline = item_y + kMenuItemHeight / 2 + 5;
      if (xft_draw) {
        baseline = item_y +
                   (kMenuItemHeight -
                    (menu_font->ascent + menu_font->descent)) /
                       2 +
                   menu_font->ascent;
        XftDrawStringUtf8(
            xft_draw, &menu_text_color, menu_font, kMenuHorizontalPadding,
            baseline, reinterpret_cast<const FcChar8*>(label.c_str()),
            static_cast<int>(label.size()));
      } else if (font_set) {
        XFontSetExtents* extents = XExtentsOfFontSet(font_set);
        if (extents) {
          baseline = item_y +
                     (kMenuItemHeight -
                      extents->max_logical_extent.height) /
                         2 -
                     extents->max_logical_extent.y;
        }
        Xutf8DrawString(display, menu_window, font_set, gc,
                        kMenuHorizontalPadding, baseline, label.c_str(),
                        static_cast<int>(label.size()));
      } else {
        if (fallback_font) {
          XSetFont(display, gc, fallback_font->fid);
          baseline = item_y +
                     (kMenuItemHeight + fallback_font->ascent -
                      fallback_font->descent) /
                         2;
        }
        XDrawString(display, menu_window, gc, kMenuHorizontalPadding, baseline,
                    ascii_label.c_str(),
                    static_cast<int>(ascii_label.size()));
      }
    };
    draw_label(0, show_menu_label, show_menu_ascii_label);
    draw_label(1, settings_menu_label, settings_menu_ascii_label);
    draw_label(2, exit_menu_label, exit_menu_ascii_label);
    if (xft_draw) {
      XftDrawDestroy(xft_draw);
    }

    XFreeGC(display, gc);
    XFlush(display);
  }

  void ShowWindow() {
    if (show_window) {
      show_window();
    } else if (app_window) {
      SDL_ShowWindow(app_window);
      SDL_RestoreWindow(app_window);
      SDL_RaiseWindow(app_window);
    }
  }

  void OpenSettings() {
    if (open_settings) {
      open_settings();
    } else {
      ShowWindow();
    }
  }

  void RequestExit() {
    if (exit_app) {
      exit_app();
      return;
    }
    SDL_Event event{};
    event.type =
        exit_event_type == 0 || exit_event_type == static_cast<uint32_t>(-1)
            ? SDL_EVENT_QUIT
            : exit_event_type;
    SDL_PushEvent(&event);
  }

  ::SDL_Window* app_window = nullptr;
  std::function<void()> show_window;
  std::function<void()> hide_window;
  std::function<void()> open_settings;
  std::function<void()> exit_app;
  std::string tooltip;
  int language_index = 0;
  uint32_t exit_event_type = 0;
  std::string show_menu_label;
  std::string show_menu_ascii_label;
  std::string settings_menu_label;
  std::string settings_menu_ascii_label;
  std::string exit_menu_label;
  std::string exit_menu_ascii_label;
  DesktopIndicatorApi desktop_indicator_api;
  void* desktop_indicator = nullptr;
  void* desktop_menu = nullptr;
  bool desktop_indicator_attempted = false;
  Display* display = nullptr;
  int screen = 0;
  ::Window root_window = 0;
  ::Window tray_manager_window = 0;
  ::Window icon_window = 0;
  ::Window menu_window = 0;
  Atom selection_atom = None;
  Atom tray_opcode_atom = None;
  Atom xembed_atom = None;
  Atom xembed_info_atom = None;
  Atom utf8_string_atom = None;
  Atom net_wm_name_atom = None;
  Atom net_wm_window_type_atom = None;
  Atom net_wm_window_type_popup_menu_atom = None;
  unsigned long black_pixel = 0;
  unsigned long white_pixel = 0;
  unsigned long brand_pixel = 0;
  unsigned long hover_pixel = 0;
  unsigned long menu_border_pixel = 0;
  Visual* icon_visual = nullptr;
  int icon_depth = 0;
  Colormap icon_colormap = 0;
  bool icon_colormap_owned = false;
  XRenderPictFormat* icon_format = nullptr;
  XFontSet font_set = nullptr;
  XFontStruct* fallback_font = nullptr;
  XftFont* menu_font = nullptr;
  XftColor menu_text_color{};
  bool menu_text_color_allocated = false;
  std::vector<unsigned char> icon_rgba;
  int icon_width = 0;
  int icon_height = 0;
  int menu_width = 72;
  int hovered_menu_item = -1;
  bool docked = false;
  bool embedded = false;
  bool menu_visible = false;
  bool icon_needs_redraw = false;
  bool shape_available = false;
};

LinuxTray::LinuxTray(::SDL_Window* app_window, const std::string& tooltip,
                     int language_index, uint32_t exit_event_type)
    : impl_(std::make_unique<LinuxTrayImpl>(app_window, tooltip, language_index,
                                            exit_event_type)) {}

LinuxTray::LinuxTray(std::function<void()> show_window,
                     std::function<void()> hide_window,
                     std::function<void()> open_settings,
                     std::function<void()> exit_app,
                     const std::string& tooltip, int language_index)
    : impl_(std::make_unique<LinuxTrayImpl>(
          std::move(show_window), std::move(hide_window),
          std::move(open_settings), std::move(exit_app), tooltip,
          language_index)) {}

LinuxTray::~LinuxTray() = default;

bool LinuxTray::MinimizeToTray() { return impl_->MinimizeToTray(); }

void LinuxTray::RemoveTrayIcon() { impl_->RemoveTrayIcon(); }

void LinuxTray::ProcessEvents() { impl_->ProcessEvents(); }

}  // namespace crossdesk

#endif  // defined(__linux__) && !defined(__APPLE__)
