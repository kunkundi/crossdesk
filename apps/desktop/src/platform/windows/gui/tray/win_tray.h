/*
 * @Author: DI JUNKUN
 * @Date: 2025-10-22
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WIN_TRAY_H_
#define _WIN_TRAY_H_

#include <Windows.h>
#include <shellapi.h>

#include <functional>
#include <string>

#define WM_TRAY_CALLBACK (WM_USER + 1)

namespace crossdesk {

class WinTray {
 public:
  WinTray(HWND app_hwnd, HICON icon, const std::wstring& tooltip,
          int language_index);
  WinTray(std::function<void()> show_window,
          std::function<void()> hide_window,
          std::function<void()> open_settings,
          std::function<void()> exit_app,
          HICON icon, const std::wstring& tooltip, int language_index);
  ~WinTray();

  void MinimizeToTray();
  void RemoveTrayIcon();
  bool HandleTrayMessage(MSG* msg);

 private:
  void ShowApplicationWindow();
  void OpenSettings();

  HWND app_hwnd_;
  HWND hwnd_message_only_;
  HICON icon_;
  std::wstring tip_;
  int language_index_;
  NOTIFYICONDATA nid_;
  std::function<void()> show_window_;
  std::function<void()> hide_window_;
  std::function<void()> open_settings_;
  std::function<void()> exit_app_;
};
}  // namespace crossdesk
#endif
