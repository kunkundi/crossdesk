/*
 * @Author: DI JUNKUN
 * @Date: 2025-10-22
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WIN_TRAY_H_
#define _WIN_TRAY_H_

#include <Windows.h>
#include <shellapi.h>

#include <string>

#define WM_TRAY_CALLBACK (WM_USER + 1)

namespace crossdesk {

class WinTray {
 public:
  WinTray(HWND app_hwnd, HICON icon, const std::wstring& tooltip,
          int language_index);
  ~WinTray();

  void MinimizeToTray();
  void RemoveTrayIcon();
  bool HandleTrayMessage(MSG* msg);

 private:
  HWND app_hwnd_;
  HWND hwnd_message_only_;
  HICON icon_;
  std::wstring tip_;
  int language_index_;
  NOTIFYICONDATA nid_;
};
}  // namespace crossdesk
#endif