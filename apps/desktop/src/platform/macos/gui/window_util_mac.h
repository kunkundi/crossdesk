/*
 * @Author: DI JUNKUN
 * @Date: 2026-01-20
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WINDOW_UTIL_MAC_H_
#define _WINDOW_UTIL_MAC_H_

struct SDL_Window;

namespace crossdesk {

// Best-effort: keep an SDL window above normal windows on macOS.
// No-op on non-macOS builds.
void MacSetWindowAlwaysOnTop(::SDL_Window* window, bool always_on_top);

// Best-effort: exclude an SDL window from the Window menu and window cycling.
// Note: Cmd-Tab switches apps (not individual windows), so this primarily
// affects the Window menu and Cmd-` window cycling.
void MacSetWindowExcludedFromWindowMenu(::SDL_Window* window, bool excluded);

}  // namespace crossdesk

#endif