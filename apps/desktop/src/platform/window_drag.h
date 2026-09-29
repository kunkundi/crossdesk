#pragma once

#include <string>

namespace crossdesk {

// Starts the platform's native move loop for the window receiving the current
// left-mouse-down event. Returns false when no suitable native event exists.
bool StartNativeWindowDrag();

// Sets a fixed white native titlebar and hides the disabled zoom button on
// the fixed-size main window. |slint_view| is its AppKit NSView*. Returns false
// while the native window or its fixed-size constraints are not ready.
bool ConfigureMainWindowChrome(void* slint_view);

// Configures live resize and replaces the stream window's native Space
// fullscreen action with an immediate, single-window fullscreen transition.
// |slint_view| is the current stream window's AppKit NSView*. Passing the
// identity explicitly prevents a closing window with the same title from
// being selected during a later connection.
bool ConfigureStreamWindowLiveResize(void* slint_view);

// Drops the native stream-window reference if it belongs to |slint_view|.
// This must run before the corresponding Slint component is destroyed.
void UnregisterStreamWindow(void* slint_view);

// Returns whether the configured stream window is currently the active AppKit
// key window.
bool IsStreamWindowActive();

// Enters or leaves the stream window's animation-free fullscreen mode.
bool SetStreamWindowFullscreen(bool fullscreen);
bool IsStreamWindowFullscreen();

// Opens an application-modal macOS file picker. The panel is made key before
// entering its modal loop so it receives keyboard input immediately.
std::string OpenNativeFileDialog(const std::string &title);

} // namespace crossdesk
