#include "win_tray.h"

#include <SDL3/SDL.h>

#include "localization.h"

#include <utility>

namespace crossdesk {

// callback for the message-only window that handles tray icon messages
static LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                   LPARAM lParam) {
  WinTray* tray =
      reinterpret_cast<WinTray*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  if (!tray) {
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }

  if (msg == WM_TRAY_CALLBACK) {
    MSG tmpMsg = {};
    tmpMsg.message = msg;
    tmpMsg.wParam = wParam;
    tmpMsg.lParam = lParam;
    tray->HandleTrayMessage(&tmpMsg);
    return 0;
  }

  return DefWindowProc(hwnd, msg, wParam, lParam);
}

WinTray::WinTray(HWND app_hwnd, HICON icon, const std::wstring& tooltip,
                 int language_index)
    : app_hwnd_(app_hwnd),
      icon_(icon),
      tip_(tooltip),
      hwnd_message_only_(nullptr),
      language_index_(language_index) {
  WNDCLASS wc = {};
  wc.lpfnWndProc = MsgWndProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = L"TrayMessageWindow";
  RegisterClass(&wc);

  // create a message-only window to receive tray messages
  hwnd_message_only_ =
      CreateWindowEx(0, wc.lpszClassName, L"TrayMsg", 0, 0, 0, 0, 0,
                     HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

  // store pointer to this WinTray instance in window data
  SetWindowLongPtr(hwnd_message_only_, GWLP_USERDATA,
                   reinterpret_cast<LONG_PTR>(this));

  // initialize NOTIFYICONDATA structure
  ZeroMemory(&nid_, sizeof(nid_));
  nid_.cbSize = sizeof(nid_);
  nid_.hWnd = hwnd_message_only_;
  nid_.uID = 1;
  nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid_.uCallbackMessage = WM_TRAY_CALLBACK;
  nid_.hIcon = icon_;
  wcsncpy_s(nid_.szTip, tip_.c_str(), _TRUNCATE);
}

WinTray::WinTray(std::function<void()> show_window,
                 std::function<void()> hide_window,
                 std::function<void()> open_settings,
                 std::function<void()> exit_app, HICON icon,
                 const std::wstring& tooltip, int language_index)
    : WinTray(nullptr, icon, tooltip, language_index) {
  show_window_ = std::move(show_window);
  hide_window_ = std::move(hide_window);
  open_settings_ = std::move(open_settings);
  exit_app_ = std::move(exit_app);
}

WinTray::~WinTray() {
  RemoveTrayIcon();
  if (hwnd_message_only_) DestroyWindow(hwnd_message_only_);
}

void WinTray::MinimizeToTray() {
  Shell_NotifyIcon(NIM_ADD, &nid_);
  if (hide_window_) {
    hide_window_();
  } else if (app_hwnd_) {
    ShowWindow(app_hwnd_, SW_HIDE);
  }
}

void WinTray::RemoveTrayIcon() { Shell_NotifyIcon(NIM_DELETE, &nid_); }

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

      SetForegroundWindow(hwnd_message_only_);
      int cmd =
          TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN,
                         pt.x, pt.y, 0, hwnd_message_only_, nullptr);
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
