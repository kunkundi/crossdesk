#include "application/gui_application.h"

#include <libyuv.h>

#if defined(__linux__) && !defined(__APPLE__)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>

#include "fa_regular_400.h"
#include "fa_solid_900.h"
#include "layout_relative.h"
#include "localization.h"
#include "rd_log.h"
#include "version_checker.h"

#if defined(__APPLE__)
#include "window_util_mac.h"
#endif

namespace crossdesk {

namespace {
const ImWchar *GetMultilingualGlyphRanges() {
  static std::vector<ImWchar> glyph_ranges;
  if (glyph_ranges.empty()) {
    ImGuiIO &io = ImGui::GetIO();
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
    builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());

    ImVector<ImWchar> built_ranges;
    builder.BuildRanges(&built_ranges);
    glyph_ranges.assign(built_ranges.Data,
                        built_ranges.Data + built_ranges.Size);
  }
  return glyph_ranges.empty() ? nullptr : glyph_ranges.data();
}

bool CanReadFontFile(const char *font_path) {
  if (!font_path) {
    return false;
  }

  std::ifstream font_file(font_path, std::ios::binary);
  return font_file.good();
}

#if _WIN32
HICON LoadTrayIcon() {
  HMODULE module = GetModuleHandleW(nullptr);
  HICON icon = reinterpret_cast<HICON>(
      LoadImageW(module, L"IDI_ICON1", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
  if (icon) {
    return icon;
  }

  return LoadIconW(nullptr, IDI_APPLICATION);
}

#endif

#if defined(__linux__) && !defined(__APPLE__)
inline bool X11GetDisplayAndWindow(SDL_Window *window, Display **display_out,
                                   ::Window *x11_window_out) {
  if (!window || !display_out || !x11_window_out) {
    return false;
  }

#if !defined(SDL_PROP_WINDOW_X11_DISPLAY_POINTER) ||                           \
    !defined(SDL_PROP_WINDOW_X11_WINDOW_NUMBER)
  // SDL build does not expose X11 window properties.
  return false;
#else
  SDL_PropertiesID props = SDL_GetWindowProperties(window);
  Display *display = (Display *)SDL_GetPointerProperty(
      props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
  const Sint64 x11_window_num =
      SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
  const ::Window x11_window = (::Window)x11_window_num;

  if (!display || !x11_window) {
    return false;
  }

  *display_out = display;
  *x11_window_out = x11_window;
  return true;
#endif
}

inline void X11SendNetWmState(Display *display, ::Window x11_window,
                              long action, Atom state1, Atom state2 = 0) {
  if (!display || !x11_window) {
    return;
  }

  const Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);

  XEvent event;
  memset(&event, 0, sizeof(event));
  event.xclient.type = ClientMessage;
  event.xclient.serial = 0;
  event.xclient.send_event = True;
  event.xclient.message_type = wm_state;
  event.xclient.window = x11_window;
  event.xclient.format = 32;
  event.xclient.data.l[0] = action;
  event.xclient.data.l[1] = (long)state1;
  event.xclient.data.l[2] = (long)state2;
  event.xclient.data.l[3] = 1; // normal source indication
  event.xclient.data.l[4] = 0;

  XSendEvent(display, DefaultRootWindow(display), False,
             SubstructureRedirectMask | SubstructureNotifyMask, &event);
}

inline void X11SetWindowTypeUtility(Display *display, ::Window x11_window) {
  if (!display || !x11_window) {
    return;
  }

  const Atom wm_window_type =
      XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
  const Atom wm_window_type_utility =
      XInternAtom(display, "_NET_WM_WINDOW_TYPE_UTILITY", False);

  XChangeProperty(display, x11_window, wm_window_type, XA_ATOM, 32,
                  PropModeReplace, (unsigned char *)&wm_window_type_utility, 1);
}

inline void X11SetWindowAlwaysOnTop(SDL_Window *window) {
  Display *display = nullptr;
  ::Window x11_window = 0;
  if (!X11GetDisplayAndWindow(window, &display, &x11_window)) {
    return;
  }

  const Atom state_above = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
  const Atom state_stays_on_top =
      XInternAtom(display, "_NET_WM_STATE_STAYS_ON_TOP", False);

  // Request _NET_WM_STATE_ADD for ABOVE + STAYS_ON_TOP.
  X11SendNetWmState(display, x11_window, 1, state_above, state_stays_on_top);
  XFlush(display);
}

inline void X11SetWindowSkipTaskbar(SDL_Window *window) {
  Display *display = nullptr;
  ::Window x11_window = 0;
  if (!X11GetDisplayAndWindow(window, &display, &x11_window)) {
    return;
  }

  const Atom skip_taskbar =
      XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False);
  const Atom skip_pager =
      XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False);

  // Request _NET_WM_STATE_ADD for SKIP_TASKBAR + SKIP_PAGER.
  X11SendNetWmState(display, x11_window, 1, skip_taskbar, skip_pager);

  // Hint the WM that this is an auxiliary/utility window.
  X11SetWindowTypeUtility(display, x11_window);

  XFlush(display);
}
#endif
} // namespace

SDL_HitTestResult GuiApplication::HitTestCallback(SDL_Window *window,
                                                  const SDL_Point *area,
                                                  void *data) {
  GuiApplication *application = (GuiApplication *)data;
  if (!application) {
    return SDL_HITTEST_NORMAL;
  }

  if (application->fullscreen_button_pressed_) {
    return SDL_HITTEST_NORMAL;
  }

  // Server window: OS-level dragging for the title bar, but keep the left-side
  // collapse/expand button clickable.
  if (application->server_window_ && window == application->server_window_) {
    const float title_h = application->server_window_title_bar_height_;
    const float button_w = title_h;
    if (area->y >= 0 && area->y < title_h) {
      if (area->x >= 0 && area->x < button_w) {
        return SDL_HITTEST_NORMAL;
      }
      return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
  }

  int window_width, window_height;
  SDL_GetWindowSize(window, &window_width, &window_height);

  // check if curosor is in tab bar
  if (application->stream_window_inited_ && application->stream_window_created_ &&
      !application->fullscreen_button_pressed_ && application->stream_ctx_) {
    ImGuiContext *prev_ctx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(application->stream_ctx_);

    ImGuiWindow *tab_bar_window = ImGui::FindWindowByName("TabBar");
    if (tab_bar_window && tab_bar_window->Active) {
      ImGuiIO &io = ImGui::GetIO();
      float scale_x = io.DisplayFramebufferScale.x;
      float scale_y = io.DisplayFramebufferScale.y;

      float tab_bar_x = tab_bar_window->Pos.x * scale_x;
      float tab_bar_y = tab_bar_window->Pos.y * scale_y;
      float tab_bar_width = tab_bar_window->Size.x * scale_x;
      float tab_bar_height = tab_bar_window->Size.y * scale_y;

      ImGui::SetCurrentContext(prev_ctx);

      if (area->x >= tab_bar_x && area->x <= tab_bar_x + tab_bar_width &&
          area->y >= tab_bar_y && area->y <= tab_bar_y + tab_bar_height) {
        return SDL_HITTEST_NORMAL;
      }
    } else {
      ImGui::SetCurrentContext(prev_ctx);
    }
  }

  float mouse_grab_padding = application->title_bar_button_width_ * 0.16f;
  if (area->y < application->title_bar_button_width_ &&
      area->y > mouse_grab_padding &&
      area->x < window_width - application->title_bar_button_width_ * 3.0f &&
      area->x > mouse_grab_padding) {
    return SDL_HITTEST_DRAGGABLE;
  }

  // if (!application->streaming_) {
  //   return SDL_HITTEST_NORMAL;
  // }

  if (area->y < mouse_grab_padding) {
    if (area->x < mouse_grab_padding) {
      return SDL_HITTEST_RESIZE_TOPLEFT;
    } else if (area->x > window_width - mouse_grab_padding) {
      return SDL_HITTEST_RESIZE_TOPRIGHT;
    } else {
      return SDL_HITTEST_RESIZE_TOP;
    }
  } else if (area->y > window_height - mouse_grab_padding) {
    if (area->x < mouse_grab_padding) {
      return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    } else if (area->x > window_width - mouse_grab_padding) {
      return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    } else {
      return SDL_HITTEST_RESIZE_BOTTOM;
    }
  } else if (area->x < mouse_grab_padding) {
    return SDL_HITTEST_RESIZE_LEFT;
  } else if (area->x > window_width - mouse_grab_padding) {
    return SDL_HITTEST_RESIZE_RIGHT;
  }

  return SDL_HITTEST_NORMAL;
}

int GuiApplication::CreateMainWindow() {
  main_ctx_ = ImGui::CreateContext();
  if (!main_ctx_) {
    LOG_ERROR("Main context is null");
    return -1;
  }

  ImGui::SetCurrentContext(main_ctx_);

  if (!SDL_CreateWindowAndRenderer(
          "CrossDesk Main Window", (int)main_window_width_,
          (int)main_window_height_,
          SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_BORDERLESS |
              SDL_WINDOW_HIDDEN | SDL_WINDOW_TRANSPARENT,
          &main_window_, &main_renderer_)) {
    LOG_ERROR("Error creating MainWindow and MainRenderer: {}", SDL_GetError());
    return -1;
  }

  float dpi_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if (std::abs(dpi_scale_ - dpi_scale) > 0.01f) {
    dpi_scale_ = dpi_scale;

    main_window_width_ = (int)(main_window_width_default_ * dpi_scale_);
    main_window_height_ = (int)(main_window_height_default_ * dpi_scale_);
    stream_window_width_ = (int)(stream_window_width_default_ * dpi_scale_);
    stream_window_height_ = (int)(stream_window_height_default_ * dpi_scale_);
    server_window_width_ = (int)(server_window_width_default_ * dpi_scale_);
    server_window_height_ = (int)(server_window_height_default_ * dpi_scale_);
    server_window_normal_width_ =
        (int)(server_window_width_default_ * dpi_scale_);
    server_window_normal_height_ =
        (int)(server_window_height_default_ * dpi_scale_);
    window_rounding_ = window_rounding_default_ * dpi_scale_;

    SDL_SetWindowSize(main_window_, (int)main_window_width_,
                      (int)main_window_height_);
  }

  SDL_SetWindowResizable(main_window_, false);

  // for window region action
  SDL_SetWindowHitTest(main_window_, HitTestCallback, this);

  SDL_SetRenderDrawBlendMode(main_renderer_, SDL_BLENDMODE_BLEND);

  SetupFontAndStyle(&main_windows_system_chinese_font_);

  ImGuiStyle &style = ImGui::GetStyle();
  style.ScaleAllSizes(dpi_scale_);
  style.FontScaleDpi = dpi_scale_;

#if _WIN32
  SDL_PropertiesID props = SDL_GetWindowProperties(main_window_);
  HWND main_hwnd = (HWND)SDL_GetPointerProperty(
      props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);

  HICON tray_icon = LoadTrayIcon();
  tray_ = std::make_unique<WinTray>(main_hwnd, tray_icon, L"CrossDesk",
                                    localization_language_index_);
#elif defined(__APPLE__)
  tray_ = std::make_unique<MacTray>(main_window_, "CrossDesk",
                                    localization_language_index_);
#elif defined(__linux__) && !defined(__APPLE__)
  tray_ = std::make_unique<LinuxTray>(
      main_window_, "CrossDesk", localization_language_index_, APP_EXIT_EVENT);
#endif

  ImGui_ImplSDL3_InitForSDLRenderer(main_window_, main_renderer_);
  ImGui_ImplSDLRenderer3_Init(main_renderer_);

  return 0;
}

int GuiApplication::DestroyMainWindow() {
  if (main_ctx_) {
    ImGui::SetCurrentContext(main_ctx_);
  }

  if (main_renderer_) {
    SDL_DestroyRenderer(main_renderer_);
  }

  if (main_window_) {
    SDL_DestroyWindow(main_window_);
  }

  return 0;
}

int GuiApplication::CreateStreamWindow() {
  if (stream_window_created_) {
    return 0;
  }

  stream_window_width_ = (int)(stream_window_width_default_ * dpi_scale_);
  stream_window_height_ = (int)(stream_window_height_default_ * dpi_scale_);

  stream_ctx_ = ImGui::CreateContext();
  if (!stream_ctx_) {
    LOG_ERROR("Stream context is null");
    return -1;
  }

  ImGui::SetCurrentContext(stream_ctx_);

  if (!SDL_CreateWindowAndRenderer(
          "CrossDesk Stream Window", (int)stream_window_width_,
          (int)stream_window_height_,
          SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_BORDERLESS |
              SDL_WINDOW_TRANSPARENT,
          &stream_window_, &stream_renderer_)) {
    LOG_ERROR("Error creating stream_window_ and stream_renderer_: {}",
              SDL_GetError());
    return -1;
  }

  stream_pixformat_ = SDL_PIXELFORMAT_NV12;

  SDL_SetWindowResizable(stream_window_, true);

  // for window region action
  SDL_SetWindowHitTest(stream_window_, HitTestCallback, this);

  SDL_SetRenderDrawBlendMode(stream_renderer_, SDL_BLENDMODE_BLEND);

  SetupFontAndStyle(&stream_windows_system_chinese_font_);

  ImGuiStyle &style = ImGui::GetStyle();
  style.ScaleAllSizes(dpi_scale_);
  style.FontScaleDpi = dpi_scale_;

  ImGui_ImplSDL3_InitForSDLRenderer(stream_window_, stream_renderer_);
  ImGui_ImplSDLRenderer3_Init(stream_renderer_);

  // change props->stream_render_rect_
  SDL_Event event;
  event.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
  event.window.windowID = SDL_GetWindowID(stream_window_);
  SDL_PushEvent(&event);

  stream_window_created_ = true;
  just_created_ = true;

  stream_window_inited_ = true;
  LOG_INFO("Stream window inited");

  return 0;
}

int GuiApplication::DestroyStreamWindow() {
  stream_window_width_ = (float)stream_window_width_default_;
  stream_window_height_ = (float)stream_window_height_default_;

  if (stream_ctx_) {
    ImGui::SetCurrentContext(stream_ctx_);
  }

  if (stream_renderer_) {
    SDL_DestroyRenderer(stream_renderer_);
    stream_renderer_ = nullptr;
  }

  if (stream_window_) {
    SDL_DestroyWindow(stream_window_);
    stream_window_ = nullptr;
  }

  stream_window_created_ = false;
  focus_on_stream_window_ = false;
  stream_window_grabbed_ = false;
  control_mouse_ = false;

  return 0;
}

int GuiApplication::CreateServerWindow() {
  if (server_window_created_) {
    return 0;
  }
  server_ctx_ = ImGui::CreateContext();
  if (!server_ctx_) {
    LOG_ERROR("Server context is null");
    return -1;
  }
  ImGui::SetCurrentContext(server_ctx_);
  if (!SDL_CreateWindowAndRenderer(
          "CrossDesk Server Window", (int)server_window_width_,
          (int)server_window_height_,
          SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_BORDERLESS |
              SDL_WINDOW_TRANSPARENT,
          &server_window_, &server_renderer_)) {
    LOG_ERROR("Error creating server_window_ and server_renderer_: {}",
              SDL_GetError());
    return -1;
  }

#if _WIN32
  // Hide server window from the taskbar by making it a tool window.
  {
    SDL_PropertiesID server_props = SDL_GetWindowProperties(server_window_);
    HWND server_hwnd = (HWND)SDL_GetPointerProperty(
        server_props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);

    if (server_hwnd) {
      LONG_PTR ex_style = GetWindowLongPtr(server_hwnd, GWL_EXSTYLE);
      ex_style |= WS_EX_TOOLWINDOW;
      ex_style &= ~WS_EX_APPWINDOW;
      SetWindowLongPtr(server_hwnd, GWL_EXSTYLE, ex_style);

      // Keep the server window above normal windows.
      SetWindowPos(server_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
  }
#endif

#if defined(__linux__) && !defined(__APPLE__)
  // Best-effort keep above other windows on X11.
  X11SetWindowAlwaysOnTop(server_window_);
  // Best-effort hide from taskbar on X11.
  X11SetWindowSkipTaskbar(server_window_);
#endif

#if defined(__APPLE__)
  // Best-effort keep above other windows on macOS.
  MacSetWindowAlwaysOnTop(server_window_, true);
  // Best-effort exclude from Window menu / window cycling.
  MacSetWindowExcludedFromWindowMenu(server_window_, true);
#endif

  // Set window position to bottom-right corner
  SDL_Rect display_bounds;
  if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(server_window_),
                                 &display_bounds)) {
    int window_x =
        display_bounds.x + display_bounds.w - (int)server_window_width_;
    int window_y =
        display_bounds.y + display_bounds.h - (int)server_window_height_;
    SDL_SetWindowPosition(server_window_, window_x, window_y);
  }

  SDL_SetWindowResizable(server_window_, false);

  SDL_SetRenderDrawBlendMode(server_renderer_, SDL_BLENDMODE_BLEND);

  // for window region action
  SDL_SetWindowHitTest(server_window_, HitTestCallback, this);

  SetupFontAndStyle(&server_windows_system_chinese_font_);

  ImGuiStyle &style = ImGui::GetStyle();
  style.ScaleAllSizes(dpi_scale_);
  style.FontScaleDpi = dpi_scale_;

  ImGui_ImplSDL3_InitForSDLRenderer(server_window_, server_renderer_);
  ImGui_ImplSDLRenderer3_Init(server_renderer_);

  server_window_created_ = true;
  server_window_inited_ = true;

  LOG_INFO("Server window inited");

  return 0;
}

int GuiApplication::DestroyServerWindow() {
  if (server_ctx_) {
    ImGui::SetCurrentContext(server_ctx_);
  }

  if (server_renderer_) {
    SDL_DestroyRenderer(server_renderer_);
    server_renderer_ = nullptr;
  }

  if (server_window_) {
    SDL_DestroyWindow(server_window_);
    server_window_ = nullptr;
  }

  server_window_created_ = false;
  server_window_inited_ = false;

  return 0;
}

int GuiApplication::SetupFontAndStyle(ImFont **system_chinese_font_out) {
  float font_size = 32.0f;

  // Setup Dear ImGui style
  ImGuiIO &io = ImGui::GetIO();

  io.IniFilename = NULL; // disable imgui.ini

  // Build one merged atlas: UI font + icon font + multilingual fallback fonts.
  ImFontConfig config;
  config.FontDataOwnedByAtlas = false;
  config.MergeMode = false;

  if (system_chinese_font_out) {
    *system_chinese_font_out = nullptr;
  }

  ImFont *ui_font = nullptr;
  const ImWchar *multilingual_ranges = GetMultilingualGlyphRanges();

#if defined(_WIN32)
  const char *base_font_paths[] = {
      "C:/Windows/Fonts/msyh.ttc",    "C:/Windows/Fonts/msyhbd.ttc",
      "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
      "C:/Windows/Fonts/simsun.ttc",  nullptr};
#elif defined(__APPLE__)
  const char *base_font_paths[] = {
      "/System/Library/Fonts/PingFang.ttc",
      "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/System/Library/Fonts/SFNS.ttf", nullptr};
#else
  const char *base_font_paths[] = {
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
      "/usr/share/fonts/opentype/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
      "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      nullptr};
#endif

  for (int i = 0; base_font_paths[i] != nullptr && ui_font == nullptr; ++i) {
    if (!CanReadFontFile(base_font_paths[i])) {
      continue;
    }
    ui_font = io.Fonts->AddFontFromFileTTF(base_font_paths[i], font_size,
                                           &config, multilingual_ranges);
    if (ui_font != nullptr) {
      LOG_INFO("Loaded base UI font: {}", base_font_paths[i]);
    }
  }
  if (!ui_font) {
    ui_font = io.Fonts->AddFontDefault(&config);
  }

  if (!ui_font) {
    LOG_WARN("Failed to initialize base UI font");
    ImGui::StyleColorsLight();
    return 0;
  }

  ImFontConfig icon_config = config;
  icon_config.MergeMode = true;
  static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
  io.Fonts->AddFontFromMemoryTTF(fa_solid_900_ttf, fa_solid_900_ttf_len,
                                 font_size, &icon_config, icon_ranges);

  io.FontDefault = ui_font;
  if (system_chinese_font_out) {
    *system_chinese_font_out = ui_font;
  }

  ImGui::StyleColorsLight();

  return 0;
}

int GuiApplication::DestroyMainWindowContext() {
  if (!main_ctx_) {
    return 0;
  }

  ImGui::SetCurrentContext(main_ctx_);
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext(main_ctx_);
  main_ctx_ = nullptr;

  return 0;
}

int GuiApplication::DestroyStreamWindowContext() {
  if (!stream_ctx_) {
    stream_window_inited_ = false;
    return 0;
  }

  stream_window_inited_ = false;
  ImGui::SetCurrentContext(stream_ctx_);
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext(stream_ctx_);
  stream_ctx_ = nullptr;

  return 0;
}

int GuiApplication::DestroyServerWindowContext() {
  if (!server_ctx_) {
    server_window_inited_ = false;
    return 0;
  }

  server_window_inited_ = false;
  ImGui::SetCurrentContext(server_ctx_);
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext(server_ctx_);
  server_ctx_ = nullptr;

  return 0;
}

} // namespace crossdesk
