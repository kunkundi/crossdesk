#pragma once

#include <functional>
#include <string>

namespace crossdesk {

// Starts the platform's native move loop for the window receiving the current
// left-mouse-down event. Returns false when no suitable native event exists.
bool StartNativeWindowDrag();

// Gives a titled AppKit window an opaque white backing, preserving its resize
// and fullscreen behavior. Returns false until its native window is ready.
bool ConfigureWhiteWindowChrome(void* slint_view);
// Centers the complete AppKit window frame in its screen's usable area.
// Returns false until its native window and screen are ready.
bool CenterNativeWindowOnScreen(void* slint_view);

// Configures the connection or DeskHub window with a white native titlebar.
// Only DeskHub is resizable. |slint_view| is its AppKit NSView*.
// Returns false while the native window or its titlebar controls are not ready.
bool ConfigureMainWindowChrome(void* slint_view, bool gallery = false);
// Winit's initial macOS position is a content origin, while subsequent moves
// use the outer frame. Return the logical titlebar inset for initial placement.
float MainWindowInitialPositionYOffset();
bool SetGalleryWindowFullscreen(bool fullscreen);
bool IsGalleryWindowFullscreen();
bool IsGalleryWindowActive();
// AppKit's animated zoom spins a nested run loop and cannot be called from a
// Slint timer. Restore maximization by setting the visible screen frame directly.
bool MaximizeGalleryWindowWithoutAnimation();
bool IsGalleryWindowMaximized();

// Configures live resize and allows the control window to occupy its own
// native macOS fullscreen Space. Reports when AppKit starts or finishes the
// transition for this window. The callback returns whether video uses a native
// surface that can resize independently of Slint. |slint_view| is its NSView*.
bool ConfigureStreamWindowLiveResize(void* slint_view,
                                     std::function<bool(bool)> transition_changed);
bool IsStreamWindowFullscreenTransitioning(void* slint_view);
bool IsStreamWindowFullscreen(void* slint_view);
// Leave the native fullscreen Space to minimize, then restore it on Dock activation.
void MinimizeMacStreamWindow(void* slint_view);
// Signed top inset for an aspect-fit video in the moving control window.
float StreamWindowVideoTransitionInset(void* view, float content_width, float content_height,
                                      bool* transitioning = nullptr);

// Returns whether the configured stream window is currently the active AppKit
// key window.
bool IsStreamWindowActive(void* slint_view);
// Moves the system pointer to a control in the stream view (backing pixels).
bool PlaceMacStreamControlPointer(void* slint_view, float pixel_x, float pixel_y);

// Shows a desktop transfer notification without replacing the current key
// window, including while the stream is fullscreen.
// |slint_view| must be the notification's existing AppKit NSView*.
void ShowFileTransferNotification(void* slint_view,
                                  const std::function<void()>& show);

// Schedule hidden initialization after the main window's initial display.
void PrepareNativeFileDialog();
// Opens a macOS file picker asynchronously on the main thread, without a
// nested modal loop. Completion receives an empty path on cancel or failure.
void OpenNativeFileDialog(const std::string& title,
                          std::function<void(std::string)> completion);
// Asynchronous ZIP destination picker with native overwrite confirmation.
void SaveNativeFileDialog(const std::string& title, const std::string& filename,
                          std::function<void(std::string)> completion);
// Cancels an open or pending picker, completes it once with an empty path, and
// releases the cached panel and pending initialization during application exit.
void CancelNativeFileDialog();

} // namespace crossdesk
