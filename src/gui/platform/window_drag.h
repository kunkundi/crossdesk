#pragma once

#include <string>

namespace crossdesk {

// Starts the platform's native move loop for the window receiving the current
// left-mouse-down event. Returns false when no suitable native event exists.
bool StartNativeWindowDrag();

// Normalizes the native title bar and hides the disabled zoom button on
// CrossDesk's fixed-size main window. Returns true once the matching AppKit
// window has been configured.
bool ConfigureMainWindowTitlebar();

// Configures live resize and replaces the stream window's native Space
// fullscreen action with an immediate, single-window fullscreen transition.
bool ConfigureStreamWindowLiveResize();

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
