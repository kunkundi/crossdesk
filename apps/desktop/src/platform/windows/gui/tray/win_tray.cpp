#include "win_tray.h"

#include <SDL3/SDL.h>

#include "localization.h"

#include <utility>

namespace crossdesk {

WinTray::WinTray(HWND app_hwnd, HICON icon, const std::wstring& tooltip,
                 int language_index)
    : app_hwnd_(app_hwnd),
      language_index_(language_index),
      icon_host_(icon, tooltip, [this](MSG* msg) { HandleTrayMessage(msg); }) {}

WinTray::WinTray(std::function<void()> show_window,
                 std::function<void()> hide_window,
                 std::function<void()> open_settings,
                 std::function<void()> exit_app, HICON icon,
                 const std::wstring& tooltip, int language_index)
    : app_hwnd_(nullptr),
      language_index_(language_index),
      show_window_(std::move(show_window)),
      hide_window_(std::move(hide_window)),
      open_settings_(std::move(open_settings)),
      exit_app_(std::move(exit_app)),
      icon_host_(icon, tooltip, [this](MSG* msg) { HandleTrayMessage(msg); }) {}

WinTray::~WinTray() { RemoveTrayIcon(); }

bool WinTray::IsReady() const { return icon_host_.IsReady(); }

void WinTray::ProcessEvents() { icon_host_.ProcessEvents(); }

bool WinTray::MinimizeToTray() {
  if (!EnsureTrayIcon()) return false;
  if (hide_window_) {
    hide_window_();
  } else if (app_hwnd_) {
    ShowWindow(app_hwnd_, SW_HIDE);
  }
  return true;
}

bool WinTray::EnsureTrayIcon() { return icon_host_.EnsureTrayIcon(); }

void WinTray::RemoveTrayIcon() { icon_host_.RemoveTrayIcon(); }

void WinTray::ShowApplicationWindow() {
  if (show_window_) {
    show_window_();
  } else if (app_hwnd_) {
    ShowWindow(app_hwnd_, SW_SHOW);
    SetForegroundWindow(app_hwnd_);
  }
}

void WinTray::OpenSettings() {
  if (open_settings_) {
    open_settings_();
  } else {
    ShowApplicationWindow();
  }
}

bool WinTray::HandleTrayMessage(MSG* msg) {
  if (!msg || msg->message != WM_TRAY_CALLBACK) return false;

  switch (LOWORD(msg->lParam)) {
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP: {
      ShowApplicationWindow();
      break;
    }

    case WM_RBUTTONUP: {
      POINT pt;
      GetCursorPos(&pt);
      HMENU menu = CreatePopupMenu();
      HBRUSH menu_background = CreateSolidBrush(RGB(255, 255, 255));
      if (menu_background) {
        MENUINFO menu_info{};
        menu_info.cbSize = sizeof(menu_info);
        menu_info.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
        menu_info.hbrBack = menu_background;
        SetMenuInfo(menu, &menu_info);
      }

      AppendMenuW(menu, MF_STRING, 1002,
                  localization::GetShowMainWindowLabel(language_index_));
      AppendMenuW(menu, MF_STRING, 1003,
                  localization::GetSettingsLabel(language_index_));
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING, 1001,
                  localization::GetExitProgramLabel(language_index_));

      SetForegroundWindow(icon_host_.hwnd());
      int cmd =
          TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN,
                         pt.x, pt.y, 0, icon_host_.hwnd(), nullptr);
      DestroyMenu(menu);
      if (menu_background) {
        DeleteObject(menu_background);
      }

      // handle menu command
      if (cmd == 1001) {
        if (exit_app_) {
          exit_app_();
        } else {
          SDL_Event event;
          event.type = SDL_EVENT_QUIT;
          SDL_PushEvent(&event);
        }
      } else if (cmd == 1002) {
        ShowApplicationWindow();
      } else if (cmd == 1003) {
        OpenSettings();
      }
      break;
    }
  }
  return true;
}
}  // namespace crossdesk
