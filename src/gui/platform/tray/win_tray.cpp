#include "win_tray.h"

#include <SDL3/SDL.h>

#include "localization.h"

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

WinTray::~WinTray() {
  RemoveTrayIcon();
  if (hwnd_message_only_) DestroyWindow(hwnd_message_only_);
}

void WinTray::MinimizeToTray() {
  Shell_NotifyIcon(NIM_ADD, &nid_);
  // hide application window
  ShowWindow(app_hwnd_, SW_HIDE);
}

void WinTray::RemoveTrayIcon() { Shell_NotifyIcon(NIM_DELETE, &nid_); }

bool WinTray::HandleTrayMessage(MSG* msg) {
  if (!msg || msg->message != WM_TRAY_CALLBACK) return false;

  switch (LOWORD(msg->lParam)) {
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP: {
      ShowWindow(app_hwnd_, SW_SHOW);
      SetForegroundWindow(app_hwnd_);
      break;
    }

    case WM_RBUTTONUP: {
      POINT pt;
      GetCursorPos(&pt);
      HMENU menu = CreatePopupMenu();
      AppendMenuW(menu, MF_STRING, 1001,
                  localization::GetExitProgramLabel(language_index_));

      SetForegroundWindow(hwnd_message_only_);
      int cmd =
          TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN,
                         pt.x, pt.y, 0, hwnd_message_only_, nullptr);
      DestroyMenu(menu);

      // handle menu command
      if (cmd == 1001) {
        // exit application
        SDL_Event event;
        event.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&event);
      } else if (cmd == 1002) {
        ShowWindow(app_hwnd_, SW_SHOW);  // show main window
        SetForegroundWindow(app_hwnd_);
      }
      break;
    }
  }
  return true;
}
}  // namespace crossdesk
