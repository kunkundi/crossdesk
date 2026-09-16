#ifndef CROSSDESK_WINDOWS_WINDOW_ICONS_H_
#define CROSSDESK_WINDOWS_WINDOW_ICONS_H_

#include <windows.h>

namespace crossdesk {

// Call on the window's UI thread once its native handle exists. Repeated calls
// are harmless. Icons follow DPI changes and are released with the window.
bool ConfigureWindowsWindowIcons(HWND window);

}  // namespace crossdesk

#endif  // CROSSDESK_WINDOWS_WINDOW_ICONS_H_
