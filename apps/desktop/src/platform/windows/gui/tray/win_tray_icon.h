/*
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WIN_TRAY_ICON_H_
#define _WIN_TRAY_ICON_H_

#include <Windows.h>
#include <shellapi.h>

#include <functional>
#include <string>
#include <utility>

#ifndef WM_TRAY_CALLBACK
#define WM_TRAY_CALLBACK (WM_USER + 1)
#endif

namespace crossdesk {

// Owns the hidden top-level tool window and Shell_NotifyIconW lifecycle for
// the Windows notification area.
//
// Why not HWND_MESSAGE: message-only windows never receive top-level
// HWND_BROADCAST messages such as Explorer's "TaskbarCreated" notification,
// and an elevated (--autostart HighestAvailable) client must also explicitly
// allow "TaskbarCreated" through UIPI so Medium-IL Explorer broadcasts arrive
// both at early login and after Explorer restarts.
class WinTrayIconHost {
 public:
  static constexpr wchar_t kWindowClassName[] =
      L"CrossDesk.TrayMessageWindow";
  static constexpr ULONGLONG kRetryIntervalMs = 1000;

  using MessageHandler = std::function<void(MSG*)>;
  using NotifyIconFn = std::function<BOOL(DWORD, NOTIFYICONDATAW*)>;
  using TickCountFn = std::function<ULONGLONG()>;

  WinTrayIconHost(HICON icon, const std::wstring& tooltip,
                  MessageHandler on_tray_message,
                  NotifyIconFn notify_icon = DefaultNotifyIcon,
                  TickCountFn tick_count = DefaultTickCount)
      : on_tray_message_(std::move(on_tray_message)),
        notify_icon_(notify_icon ? std::move(notify_icon) : DefaultNotifyIcon),
        tick_count_(tick_count ? std::move(tick_count) : DefaultTickCount) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClassName;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      return;
    }

    taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
    // Unshown WS_EX_TOOLWINDOW top-level window: invisible and excluded from
    // taskbar/Alt-Tab, while still receiving top-level TaskbarCreated
    // broadcasts.
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClassName,
                            L"CrossDeskTrayMsg", WS_OVERLAPPED, 0, 0, 0, 0,
                            nullptr, nullptr, wc.hInstance, this);
    if (!hwnd_) {
      return;
    }

    if (taskbar_created_message_ != 0) {
      ChangeWindowMessageFilterEx(hwnd_, taskbar_created_message_, MSGFLT_ALLOW,
                                  nullptr);
    }

    ZeroMemory(&nid_, sizeof(nid_));
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAY_CALLBACK;
    nid_.hIcon = icon;
    wcsncpy_s(nid_.szTip, tooltip.c_str(), _TRUNCATE);

    EnsureTrayIcon();
  }

  ~WinTrayIconHost() {
    RemoveTrayIcon();
    if (hwnd_) {
      DestroyWindow(hwnd_);
    }
  }

  WinTrayIconHost(const WinTrayIconHost&) = delete;
  WinTrayIconHost& operator=(const WinTrayIconHost&) = delete;

  HWND hwnd() const { return hwnd_; }
  UINT taskbar_created_message() const { return taskbar_created_message_; }
  bool IsReady() const { return tray_icon_added_; }

  bool EnsureTrayIcon() {
    if (!hwnd_) {
      return false;
    }
    if (tray_icon_added_) {
      // NIM_MODIFY also detects an icon lost when Explorer was restarted.
      if (notify_icon_(NIM_MODIFY, &nid_)) {
        return true;
      }
      tray_icon_added_ = false;
    }

    tray_icon_added_ = notify_icon_(NIM_ADD, &nid_) != FALSE;
    if (!tray_icon_added_) {
      last_retry_tick_ = tick_count_();
      has_retry_tick_ = true;
    } else {
      has_retry_tick_ = false;
    }
    return tray_icon_added_;
  }

  void ProcessEvents() {
    if (tray_icon_added_ || !hwnd_) {
      return;
    }
    const ULONGLONG now = tick_count_();
    if (has_retry_tick_ && now - last_retry_tick_ < kRetryIntervalMs) {
      return;
    }
    EnsureTrayIcon();
  }

  void RemoveTrayIcon() {
    if (tray_icon_added_) {
      notify_icon_(NIM_DELETE, &nid_);
      tray_icon_added_ = false;
    }
    has_retry_tick_ = false;
  }

 private:
  static BOOL DefaultNotifyIcon(DWORD message, NOTIFYICONDATAW* data) {
    return Shell_NotifyIconW(message, data);
  }

  static ULONGLONG DefaultTickCount() { return GetTickCount64(); }

  void OnTaskbarCreated() {
    // Explorer recreated the notification area; a fresh NIM_ADD is required.
    tray_icon_added_ = false;
    EnsureTrayIcon();
  }

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam,
                                     LPARAM lparam) {
    if (msg == WM_NCCREATE) {
      const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
      auto* self = static_cast<WinTrayIconHost*>(create->lpCreateParams);
      if (self) {
        self->hwnd_ = hwnd;
      }
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(self));
    }

    auto* self = reinterpret_cast<WinTrayIconHost*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    if (msg == WM_TRAY_CALLBACK) {
      if (self->on_tray_message_) {
        MSG tray_msg{};
        tray_msg.hwnd = hwnd;
        tray_msg.message = msg;
        tray_msg.wParam = wparam;
        tray_msg.lParam = lparam;
        self->on_tray_message_(&tray_msg);
      }
      return 0;
    }

    if (self->taskbar_created_message_ != 0 &&
        msg == self->taskbar_created_message_) {
      self->OnTaskbarCreated();
      return 0;
    }

    if (msg == WM_NCDESTROY) {
      self->hwnd_ = nullptr;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }

  HWND hwnd_ = nullptr;
  UINT taskbar_created_message_ = 0;
  NOTIFYICONDATAW nid_{};
  bool tray_icon_added_ = false;
  bool has_retry_tick_ = false;
  ULONGLONG last_retry_tick_ = 0;
  MessageHandler on_tray_message_;
  NotifyIconFn notify_icon_;
  TickCountFn tick_count_;
};

}  // namespace crossdesk

#endif  // _WIN_TRAY_ICON_H_
