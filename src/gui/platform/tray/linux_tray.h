/*
 * @Author: DI JUNKUN
 * @Date: 2026-06-23
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _LINUX_TRAY_H_
#define _LINUX_TRAY_H_

#if defined(__linux__) && !defined(__APPLE__)

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct SDL_Window;

namespace crossdesk {

struct LinuxTrayImpl;

class LinuxTray {
 public:
  LinuxTray(::SDL_Window* app_window, const std::string& tooltip,
            int language_index, uint32_t exit_event_type);
  LinuxTray(std::function<void()> show_window,
            std::function<void()> hide_window,
            std::function<void()> open_settings,
            std::function<void()> exit_app,
            const std::string& tooltip, int language_index);
  ~LinuxTray();

  bool MinimizeToTray();
  void RemoveTrayIcon();
  void ProcessEvents();

 private:
  std::unique_ptr<LinuxTrayImpl> impl_;
};

}  // namespace crossdesk

#endif  // defined(__linux__) && !defined(__APPLE__)

#endif  // _LINUX_TRAY_H_
