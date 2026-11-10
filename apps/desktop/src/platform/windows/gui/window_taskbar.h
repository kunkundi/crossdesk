#ifndef CROSSDESK_WINDOWS_WINDOW_TASKBAR_H_
#define CROSSDESK_WINDOWS_WINDOW_TASKBAR_H_

#include <windows.h>

namespace crossdesk {

// Give the remote control window its own taskbar group, leaving the main
// window's shortcut grouping intact. Call on the UI thread before showing a
// native window; repeated calls are harmless, including after hide/show.
bool ConfigureWindowsStreamTaskbar(HWND window);

}  // namespace crossdesk

#endif  // CROSSDESK_WINDOWS_WINDOW_TASKBAR_H_
