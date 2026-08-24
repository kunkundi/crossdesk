#include "platform/window_drag.h"

#import <AppKit/AppKit.h>

@interface CrossDeskStreamFullscreenTarget : NSObject
- (void)toggleStreamFullscreen:(id)sender;
@end

@implementation CrossDeskStreamFullscreenTarget
- (void)toggleStreamFullscreen:(id)sender {
  (void)sender;
  crossdesk::SetStreamWindowFullscreen(
      !crossdesk::IsStreamWindowFullscreen());
}
@end

namespace crossdesk {
namespace {

__weak NSWindow *stream_window = nil;
__weak NSView *stream_slint_view = nil;
CrossDeskStreamFullscreenTarget *stream_fullscreen_target = nil;
bool stream_fullscreen = false;
NSRect stream_restore_frame = NSZeroRect;
NSWindowStyleMask stream_restore_style_mask = NSWindowStyleMaskTitled;
NSWindowTitleVisibility stream_restore_title_visibility =
    NSWindowTitleVisible;
BOOL stream_restore_titlebar_transparent = NO;
BOOL stream_restore_movable = YES;
BOOL stream_restore_close_hidden = NO;
BOOL stream_restore_miniaturize_hidden = NO;
BOOL stream_restore_zoom_hidden = NO;
NSApplicationPresentationOptions stream_restore_presentation_options =
    NSApplicationPresentationDefault;

void InstallStreamFullscreenButton(NSWindow *window) {
  if (window == nil) {
    return;
  }
  if (stream_fullscreen_target == nil) {
    stream_fullscreen_target =
        [[CrossDeskStreamFullscreenTarget alloc] init];
  }
  NSButton *zoom_button =
      [window standardWindowButton:NSWindowZoomButton];
  zoom_button.target = stream_fullscreen_target;
  zoom_button.action = @selector(toggleStreamFullscreen:);
}

void SetTitlebarButtonsHidden(NSWindow *window, BOOL hidden) {
  [window standardWindowButton:NSWindowCloseButton].hidden = hidden;
  [window standardWindowButton:NSWindowMiniaturizeButton].hidden = hidden;
  [window standardWindowButton:NSWindowZoomButton].hidden = hidden;
}

void CenterWindowOnScreen(NSWindow *window, NSScreen *screen) {
  if (window == nil || screen == nil) {
    return;
  }
  const NSRect available_frame = screen.visibleFrame;
  const NSRect window_frame = window.frame;
  const NSPoint centered_origin = NSMakePoint(
      NSMidX(available_frame) - NSWidth(window_frame) * 0.5,
      NSMidY(available_frame) - NSHeight(window_frame) * 0.5);
  [window setFrameOrigin:centered_origin];
}

} // namespace

bool StartNativeWindowDrag() {
  @autoreleasepool {
    NSEvent *event = [[NSApplication sharedApplication] currentEvent];
    if (event == nil || event.type != NSEventTypeLeftMouseDown ||
        event.window == nil) {
      return false;
    }

    [event.window performWindowDragWithEvent:event];
    return true;
  }
}

bool HideDisabledMainWindowZoomButton() {
  @autoreleasepool {
    for (NSWindow *window in [NSApp windows]) {
      if (![window.title isEqualToString:@"CrossDesk"]) {
        continue;
      }

      NSButton *zoom_button =
          [window standardWindowButton:NSWindowZoomButton];
      // The main window has a fixed size, so AppKit disables its zoom button.
      // Stream windows remain resizable and must keep their native button.
      if (zoom_button == nil || zoom_button.enabled) {
        continue;
      }

      zoom_button.hidden = YES;
      return true;
    }
    return false;
  }
}

bool ConfigureStreamWindowLiveResize(void *opaque_slint_view) {
  @autoreleasepool {
    NSView *slint_view = (__bridge NSView *)opaque_slint_view;
    NSWindow *window = slint_view.window;
    if (window == nil) {
      return false;
    }

    NSButton *zoom_button =
        [window standardWindowButton:NSWindowZoomButton];
    NSView *content_view = window.contentView;
    if (zoom_button == nil || !zoom_button.enabled || content_view == nil) {
      return false;
    }

    // During an ordinary edge resize, scale one cached frame and redraw once
    // the new window size has settled.
    window.preservesContentDuringLiveResize = YES;
    content_view.layerContentsRedrawPolicy =
        NSViewLayerContentsRedrawBeforeViewResize;
    content_view.layerContentsPlacement =
        NSViewLayerContentsPlacementScaleProportionallyToFit;
    stream_window = window;
    stream_slint_view = slint_view;
    // Native Space fullscreen cross-fades an AppKit source snapshot over a
    // separately rendered destination window. Disable that path for the
    // stream window and route the green button to the immediate transition.
    window.collectionBehavior =
        (window.collectionBehavior &
         ~(NSWindowCollectionBehaviorFullScreenPrimary |
           NSWindowCollectionBehaviorFullScreenAuxiliary |
           NSWindowCollectionBehaviorFullScreenAllowsTiling)) |
        NSWindowCollectionBehaviorFullScreenNone;
    InstallStreamFullscreenButton(window);
    content_view.needsDisplay = YES;
    return true;
  }
}

void UnregisterStreamWindow(void *opaque_slint_view) {
  @autoreleasepool {
    NSView *slint_view = (__bridge NSView *)opaque_slint_view;
    if (slint_view == nil || slint_view != stream_slint_view) {
      return;
    }
    stream_window = nil;
    stream_slint_view = nil;
    stream_fullscreen = false;
    stream_restore_frame = NSZeroRect;
    stream_restore_style_mask = NSWindowStyleMaskTitled;
    stream_restore_title_visibility = NSWindowTitleVisible;
    stream_restore_titlebar_transparent = NO;
    stream_restore_movable = YES;
    stream_restore_close_hidden = NO;
    stream_restore_miniaturize_hidden = NO;
    stream_restore_zoom_hidden = NO;
    stream_restore_presentation_options = NSApplicationPresentationDefault;
  }
}

bool IsStreamWindowActive() {
  @autoreleasepool {
    return stream_window != nil && [NSApp isActive] &&
           [stream_window isKeyWindow];
  }
}

bool SetStreamWindowFullscreen(bool fullscreen) {
  @autoreleasepool {
    if (stream_window == nil || stream_fullscreen == fullscreen) {
      return stream_window != nil;
    }

    if (fullscreen) {
      NSScreen *screen = stream_window.screen ?: NSScreen.mainScreen;
      if (screen == nil) {
        return false;
      }

      stream_restore_frame = stream_window.frame;
      stream_restore_style_mask = stream_window.styleMask;
      stream_restore_title_visibility = stream_window.titleVisibility;
      stream_restore_titlebar_transparent =
          stream_window.titlebarAppearsTransparent;
      stream_restore_movable = stream_window.movable;
      stream_restore_close_hidden =
          [stream_window standardWindowButton:NSWindowCloseButton].hidden;
      stream_restore_miniaturize_hidden =
          [stream_window standardWindowButton:NSWindowMiniaturizeButton].hidden;
      stream_restore_zoom_hidden =
          [stream_window standardWindowButton:NSWindowZoomButton].hidden;
      stream_restore_presentation_options = NSApp.presentationOptions;

      stream_fullscreen = true;
      stream_window.styleMask =
          stream_restore_style_mask | NSWindowStyleMaskFullSizeContentView;
      stream_window.titleVisibility = NSWindowTitleHidden;
      stream_window.titlebarAppearsTransparent = YES;
      stream_window.movable = NO;
      SetTitlebarButtonsHidden(stream_window, YES);

      NSApplicationPresentationOptions presentation_options =
          stream_restore_presentation_options;
      if ((presentation_options & (NSApplicationPresentationHideDock |
                                   NSApplicationPresentationAutoHideDock)) ==
          0) {
        presentation_options |= NSApplicationPresentationAutoHideDock;
      }
      if ((presentation_options & (NSApplicationPresentationHideMenuBar |
                                   NSApplicationPresentationAutoHideMenuBar)) ==
          0) {
        presentation_options |= NSApplicationPresentationAutoHideMenuBar;
      }
      NSApp.presentationOptions = presentation_options;
      [stream_window setFrame:screen.frame display:YES animate:NO];
      [stream_window makeKeyAndOrderFront:nil];
    } else {
      stream_fullscreen = false;
      NSApp.presentationOptions = stream_restore_presentation_options;
      stream_window.styleMask = stream_restore_style_mask;
      stream_window.titleVisibility = stream_restore_title_visibility;
      stream_window.titlebarAppearsTransparent =
          stream_restore_titlebar_transparent;
      stream_window.movable = stream_restore_movable;
      [stream_window setFrame:stream_restore_frame display:YES animate:NO];
      [stream_window standardWindowButton:NSWindowCloseButton].hidden =
          stream_restore_close_hidden;
      [stream_window standardWindowButton:NSWindowMiniaturizeButton].hidden =
          stream_restore_miniaturize_hidden;
      [stream_window standardWindowButton:NSWindowZoomButton].hidden =
          stream_restore_zoom_hidden;
      InstallStreamFullscreenButton(stream_window);
      [stream_window makeKeyAndOrderFront:nil];
    }

    stream_window.contentView.alphaValue = 1.0;
    stream_window.contentView.needsDisplay = YES;
    [stream_window displayIfNeeded];
    return true;
  }
}

bool IsStreamWindowFullscreen() {
  return stream_fullscreen;
}

std::string OpenNativeFileDialog(const std::string &title) {
  @autoreleasepool {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.resolvesAliases = YES;

    NSString *prompt =
        [[NSString alloc] initWithBytes:title.data()
                                 length:title.size()
                               encoding:NSUTF8StringEncoding];
    if (prompt != nil && prompt.length > 0) {
      panel.title = prompt;
      panel.message = prompt;
    }

    // tinyfiledialogs uses an external osascript process on macOS, so its
    // chooser can appear without becoming the active window. Keep the picker
    // inside CrossDesk and explicitly activate both the app and panel.
    [NSApp activateIgnoringOtherApps:YES];
    NSWindow *owner = stream_window ?: NSApp.keyWindow ?: NSApp.mainWindow;
    if (owner != nil) {
      [owner makeKeyAndOrderFront:nil];
    }
    NSScreen *target_screen = owner.screen ?: NSScreen.mainScreen;
    CenterWindowOnScreen(panel, target_screen);
    [panel makeKeyAndOrderFront:nil];

    if ([panel runModal] != NSModalResponseOK || panel.URL == nil) {
      return {};
    }
    const char *path = panel.URL.fileSystemRepresentation;
    return path != nullptr ? std::string(path) : std::string{};
  }
}

} // namespace crossdesk
