/*
 * @Author: DI JUNKUN
 * @Date: 2026-06-23
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MAC_TRAY_H_
#define _MAC_TRAY_H_

#include <functional>
#include <memory>
#include <string>

struct SDL_Window;

namespace crossdesk {

struct MacTrayImpl;

class MacTray {
 public:
  MacTray(::SDL_Window* app_window, const std::string& tooltip,
          int language_index);
  MacTray(std::function<void()> show_window,
          std::function<void()> hide_window,
          std::function<void()> open_settings,
          std::function<void()> exit_app,
          const std::string& tooltip, int language_index);
  ~MacTray();

  void MinimizeToTray();
  void RemoveTrayIcon();

 private:
  std::unique_ptr<MacTrayImpl> impl_;
};

}  // namespace crossdesk

#endif  // _MAC_TRAY_H_
