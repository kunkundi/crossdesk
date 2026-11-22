#include "platform/window_drag.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/CALayer.h>
#import <QuartzCore/CAMediaTimingFunction.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <objc/runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>

static char stream_transition_key;

static NSTimeInterval StreamWindowAnimationDuration(NSTimeInterval duration) {
  return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion ? 0 : duration;
}

static void AnimateStreamWindowFrame(NSWindow* window, NSRect start,
                                     NSRect target, NSTimeInterval duration) {
  if (duration <= 0 || NSEqualRects(start, target)) {
    if (!NSEqualRects(window.frame, target)) [window setFrame:target display:YES];
    return;
  }
  if (!NSEqualRects(window.frame, start)) [window setFrame:start display:NO];
  [NSAnimationContext runAnimationGroup:^(NSAnimationContext* context) {
    context.duration = duration;
    context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
    context.allowsImplicitAnimation = YES;
    [window.animator setFrame:target display:YES];
  } completionHandler:nil];
}

@interface CrossDeskStreamTransitionDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, weak) id<NSWindowDelegate> backendDelegate;
@property(nonatomic, copy) void (^transitionChanged)(bool active);
@property(nonatomic, readonly) BOOL animatingVideo;
- (CGFloat)videoInsetForContentSize:(NSSize)size inWindow:(NSWindow*)window;
@end

@implementation CrossDeskStreamTransitionDelegate {
  NSRect _windowedFrame;
  NSRect _windowedScreenFrame;
  NSRect _fullscreenFrame;
  CGFloat _titleHeight;
  NSImage* _windowedTitle;
  NSImageView* _exitingHeader;
  NSViewLayerContentsRedrawPolicy _savedRedrawPolicy;
  BOOL _transitioning;
}

- (CGFloat)videoInsetForContentSize:(NSSize)size inWindow:(NSWindow*)window {
  if (!_animatingVideo) return 0;
  // The real titlebar changes style immediately. Interpolate its occupied
  // space with the actual window size so the picture follows the same path.
  const CGFloat dw = NSWidth(_fullscreenFrame) - NSWidth(_windowedFrame);
  const CGFloat dh = NSHeight(_fullscreenFrame) - NSHeight(_windowedFrame);
  if (dw == 0 && dh == 0) return 0;
  const CGFloat progress = std::abs(dw) >= std::abs(dh)
      ? (size.width - NSWidth(_windowedFrame)) / dw
      : (NSHeight(window.frame) - NSHeight(_windowedFrame)) / dh;
  const CGFloat from = NSHeight(_windowedFrame) - _titleHeight;
  const CGFloat height = from + std::clamp<CGFloat>(progress, 0, 1) *
                                    (NSHeight(_fullscreenFrame) - from);
  return std::clamp<CGFloat>(size.height - height, -_titleHeight, _titleHeight);
}

- (BOOL)respondsToSelector:(SEL)selector {
  return [super respondsToSelector:selector] ||
         [self.backendDelegate respondsToSelector:selector];
}

- (id)forwardingTargetForSelector:(SEL)selector {
  if ([self.backendDelegate respondsToSelector:selector]) return self.backendDelegate;
  return [super forwardingTargetForSelector:selector];
}

- (NSArray<NSWindow*>*)customWindowsToEnterFullScreenForWindow:(NSWindow*)window
                                                      onScreen:(NSScreen*)screen {
  return @[window];
}

- (NSImageView*)showTitleInContent:(NSView*)content
                           exiting:(BOOL)exiting
                          duration:(NSTimeInterval)duration {
  if (!_windowedTitle || duration <= 0) return nil;
  NSImageView* header = [NSImageView imageViewWithImage:_windowedTitle];
  header.imageScaling = NSImageScaleAxesIndependently;
  header.wantsLayer = YES;
  header.layer.masksToBounds = YES;
  header.layer.maskedCorners = kCALayerMinXMaxYCorner | kCALayerMaxXMaxYCorner;
  header.layer.cornerRadius = _titleHeight * 0.5;
  header.layer.cornerCurve = kCACornerCurveContinuous;
  header.translatesAutoresizingMaskIntoConstraints = NO;
  header.alphaValue = exiting ? 0 : 1;
  [content addSubview:header];
  [NSLayoutConstraint activateConstraints:@[
      [header.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
      [header.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
      [header.topAnchor constraintEqualToAnchor:content.topAnchor],
      [header.heightAnchor constraintEqualToConstant:_titleHeight],
  ]];
  [content layoutSubtreeIfNeeded];
  [NSAnimationContext runAnimationGroup:^(NSAnimationContext* context) {
    context.duration = duration * (exiting ? 1 : 0.4);
    header.animator.alphaValue = exiting ? 1 : 0;
  } completionHandler:^{
    if (!exiting) [header removeFromSuperview];
  }];
  return header;
}

- (void)window:(NSWindow*)window
    startCustomAnimationToEnterFullScreenOnScreen:(NSScreen*)screen
                                     withDuration:(NSTimeInterval)duration {
  const NSRect start = window.frame;
  _windowedFrame = start;
  _windowedScreenFrame = window.screen.frame;
  duration = StreamWindowAnimationDuration(duration);
  NSView* content = window.contentView;
  NSView* frame = content.superview;
  NSRect titleRect = frame.bounds;
  const CGFloat contentTop = NSMaxY([content convertRect:content.bounds toView:frame]);
  titleRect.size.height = NSMaxY(titleRect) - contentTop;
  titleRect.origin.y = contentTop;
  NSImage* title = nil;
  if (duration > 0 && !NSIsEmptyRect(titleRect)) {
    NSBitmapImageRep* rep = [frame bitmapImageRepForCachingDisplayInRect:titleRect];
    if (rep) {
      [frame cacheDisplayInRect:titleRect toBitmapImageRep:rep];
      title = [[NSImage alloc] initWithSize:titleRect.size];
      [title addRepresentation:rep];
      // Stretch only the empty fill so the title and traffic lights keep their shape.
      const CGFloat right = NSHeight(titleRect);
      if (NSWidth(titleRect) > right + 1) {
        title.capInsets = NSEdgeInsetsMake(0, NSWidth(titleRect) - right - 1, 0, right);
        title.resizingMode = NSImageResizingModeStretch;
      }
    }
  }

  // AppKit's default moving snapshot has a bright one-pixel rim over black
  // video. Animate the actual fullscreen window. Applying the style first
  // lets AppKit choose the destination frame, including the camera housing.
  window.styleMask |= NSWindowStyleMaskFullScreen;
  const NSRect target = window.frame;
  _fullscreenFrame = target;
  _titleHeight = std::max<CGFloat>(0, NSHeight(titleRect));
  _windowedTitle = title;
  _animatingVideo = duration > 0 && !NSEqualRects(start, target);
  if (_animatingVideo) [self showTitleInContent:content exiting:NO duration:duration];
  AnimateStreamWindowFrame(window, start, target, duration);
}

- (NSArray<NSWindow*>*)customWindowsToExitFullScreenForWindow:(NSWindow*)window {
  // Let AppKit choose the restore frame if the display configuration changed.
  if (NSIsEmptyRect(_windowedFrame) || !NSEqualRects(_windowedScreenFrame, window.screen.frame)) {
    return nil;
  }
  return @[window];
}

- (void)window:(NSWindow*)window
    startCustomAnimationToExitFullScreenWithDuration:(NSTimeInterval)duration {
  // Keep fullscreen geometry until the window has shrunk. Otherwise AppKit
  // can constrain an intermediate frame to the desktop's menu bar or Dock.
  const NSRect start = window.frame;
  duration = StreamWindowAnimationDuration(duration);
  _animatingVideo = duration > 0 && !NSEqualRects(start, _windowedFrame);
  if (_animatingVideo)
    _exitingHeader = [self showTitleInContent:window.contentView exiting:YES duration:duration];
  AnimateStreamWindowFrame(window, start, _windowedFrame, duration);
}

- (void)willToggleFullscreen:(NSNotification*)notification {
  NSView* content = ((NSWindow*)notification.object).contentView;
  if (!_transitioning) _savedRedrawPolicy = content.layerContentsRedrawPolicy;
  _transitioning = YES;
  content.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
  self.transitionChanged(true);
}

- (void)finishTransition:(NSWindow*)window {
  _animatingVideo = NO;
  [_exitingHeader removeFromSuperview];
  _exitingHeader = nil;
  if (_transitioning) window.contentView.layerContentsRedrawPolicy = _savedRedrawPolicy;
  _transitioning = NO;
  self.transitionChanged(false);
}

- (void)didToggleFullscreen:(NSNotification*)notification {
  if ([notification.name isEqualToString:NSWindowDidExitFullScreenNotification]) {
    _windowedFrame = NSZeroRect;
    _windowedTitle = nil;
  }
  [self finishTransition:notification.object];
}

- (void)windowDidFailToEnterFullScreen:(NSWindow*)window {
  _windowedFrame = NSZeroRect;
  _windowedTitle = nil;
  [self finishTransition:window];
  if ([self.backendDelegate respondsToSelector:_cmd])
    [self.backendDelegate windowDidFailToEnterFullScreen:window];
}

- (void)windowDidFailToExitFullScreen:(NSWindow*)window {
  [self finishTransition:window];
  if ([self.backendDelegate respondsToSelector:_cmd])
    [self.backendDelegate windowDidFailToExitFullScreen:window];
}
@end

namespace crossdesk {
namespace {

__weak NSWindow *gallery_window = nil;
bool gallery_fullscreen = false;
NSRect gallery_restore_frame = NSZeroRect;
NSWindowStyleMask gallery_restore_style = NSWindowStyleMaskTitled;
NSApplicationPresentationOptions saved_presentation;

void UpdateFullscreenPresentation(bool enter) {
  if (!enter) {
    NSApp.presentationOptions = saved_presentation;
    return;
  }
  saved_presentation = NSApp.presentationOptions;
  auto options = saved_presentation;
  if (!(options & NSApplicationPresentationHideDock))
    options |= NSApplicationPresentationAutoHideDock;
  if (!(options & NSApplicationPresentationHideMenuBar))
    options |= NSApplicationPresentationAutoHideMenuBar;
  NSApp.presentationOptions = options;
}

struct FileDialogRequest {
  std::string title;
  std::function<void(std::string)> completion;
  std::string filename;
};
std::shared_ptr<FileDialogRequest> file_dialog_request;
NSSavePanel *file_dialog = nil;
NSOpenPanel *prepared_file_dialog = nil;
uint64_t file_dialog_prepare_generation = 0;

NSOpenPanel *CreateFileDialog() {
  NSOpenPanel *panel = [NSOpenPanel openPanel];
  panel.canChooseFiles = YES;
  panel.canChooseDirectories = NO;
  panel.allowsMultipleSelection = NO;
  panel.resolvesAliases = YES;
  return panel;
}

void SetTitlebarButtonsHidden(NSWindow *window, BOOL hidden) {
  [window standardWindowButton:NSWindowCloseButton].hidden = hidden;
  [window standardWindowButton:NSWindowMiniaturizeButton].hidden = hidden;
  [window standardWindowButton:NSWindowZoomButton].hidden = hidden;
}

void HideMainWindowZoomButton(NSWindow *window) {
  NSButton *button = [window standardWindowButton:NSWindowZoomButton];
  button.target = nil;
  button.action = nil;
  button.enabled = NO;
  button.hidden = YES;
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

float MainWindowInitialPositionYOffset() {
  @autoreleasepool {
    const NSRect content = NSMakeRect(0, 0, 640, 486);
    const NSRect frame = [NSWindow frameRectForContentRect:content
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                  NSWindowStyleMaskMiniaturizable];
    return float(NSMaxY(frame) - NSMaxY(content));
  }
}

bool CenterNativeWindowOnScreen(void *opaque_slint_view) {
  @autoreleasepool {
    NSView *slint_view = (__bridge NSView *)opaque_slint_view;
    NSWindow *window = slint_view.window;
    NSScreen *screen = window.screen ?: NSScreen.mainScreen;
    if (!window || !screen) return false;
    CenterWindowOnScreen(window, screen);
    return true;
  }
}

bool ConfigureWhiteWindowChrome(void *opaque_slint_view) {
  @autoreleasepool {
    NSView *slint_view = (__bridge NSView *)opaque_slint_view;
    NSWindow *window = slint_view.window;
    if (window == nil ||
        (window.styleMask & NSWindowStyleMaskTitled) == 0) {
      return false;
    }

    // Winit creates a transparent AppKit backing even when Slint paints an
    // opaque client area. macOS 27 can expose that backing in the titlebar.
    // Use a fixed white backing instead of AppKit's theme-dependent titlebar
    // material. Aqua keeps the native title and controls readable on white.
    window.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
    window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    window.titlebarAppearsTransparent = YES;
    window.opaque = YES;
    window.backgroundColor = NSColor.whiteColor;
    NSView *frame_view = window.contentView.superview;
    frame_view.needsLayout = YES;
    [frame_view layoutSubtreeIfNeeded];
    return true;
  }
}

bool ConfigureMainWindowChrome(void *opaque_slint_view, bool gallery) {
  @autoreleasepool {
    NSView *slint_view = (__bridge NSView *)opaque_slint_view;
    NSWindow *window = slint_view.window;
    if (window == nil ||
        (window.styleMask & NSWindowStyleMaskTitled) == 0) {
      return false;
    }

    NSButton *zoom_button =
        [window standardWindowButton:NSWindowZoomButton];
    if (zoom_button == nil) return false;
    // Keep the connection/DeskHub and remote-control windows independently
    // switchable even when the user prefers opening documents in tabs.
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    window.excludedFromWindowsMenu = NO;
    if (gallery) gallery_window = window;
    if (gallery && gallery_fullscreen) return true;
    if (gallery)
      window.styleMask |= NSWindowStyleMaskResizable;
    else
      window.styleMask &= ~NSWindowStyleMaskResizable;
    window.collectionBehavior =
        (window.collectionBehavior & ~(NSWindowCollectionBehaviorFullScreenPrimary |
                                       NSWindowCollectionBehaviorFullScreenAuxiliary |
                                       NSWindowCollectionBehaviorFullScreenAllowsTiling)) |
        NSWindowCollectionBehaviorFullScreenNone;

    HideMainWindowZoomButton(window);
    return ConfigureWhiteWindowChrome(opaque_slint_view);
  }
}

bool SetGalleryWindowFullscreen(bool fullscreen) {
  @autoreleasepool {
    if (!gallery_window) return false;
    if (gallery_fullscreen == fullscreen) return true;
    if (fullscreen) {
      NSScreen *screen = gallery_window.screen ?: NSScreen.mainScreen;
      if (!screen) return false;
      gallery_restore_frame = gallery_window.frame;
      gallery_restore_style = gallery_window.styleMask;
      gallery_fullscreen = true;
      gallery_window.styleMask |= NSWindowStyleMaskFullSizeContentView;
      gallery_window.titleVisibility = NSWindowTitleHidden;
      gallery_window.movable = NO;
      SetTitlebarButtonsHidden(gallery_window, YES);
      UpdateFullscreenPresentation(true);
      // Let the next event-loop frame paint the updated Slint layout. A
      // synchronous draw here exposes stale grid geometry during the resize.
      [gallery_window setFrame:screen.frame display:NO animate:NO];
    } else {
      gallery_fullscreen = false;
      UpdateFullscreenPresentation(false);
      gallery_window.styleMask = gallery_restore_style;
      gallery_window.titleVisibility = NSWindowTitleVisible;
      gallery_window.movable = YES;
      [gallery_window standardWindowButton:NSWindowCloseButton].hidden = NO;
      [gallery_window standardWindowButton:NSWindowMiniaturizeButton].hidden = NO;
      // DeskHub uses its in-app fullscreen action.
      HideMainWindowZoomButton(gallery_window);
      [gallery_window setFrame:gallery_restore_frame display:NO animate:NO];
    }
    gallery_window.contentView.needsDisplay = YES;
    return true;
  }
}
bool IsGalleryWindowFullscreen() { return gallery_fullscreen; }
bool IsGalleryWindowActive() { return gallery_window && [NSApp isActive] && [gallery_window isKeyWindow]; }

bool MaximizeGalleryWindowWithoutAnimation() {
  @autoreleasepool {
    if (!gallery_window || gallery_fullscreen) return false;
    NSScreen *screen = gallery_window.screen ?: NSScreen.mainScreen;
    if (!screen) return false;
    [gallery_window setFrame:screen.visibleFrame display:YES animate:NO];
    return true;
  }
}

bool IsGalleryWindowMaximized() {
  @autoreleasepool {
    if (!gallery_window || gallery_fullscreen) return false;
    NSScreen *screen = gallery_window.screen;
    if (!screen) return false;
    // isZoomed can report true for a constrained compact window. Only treat
    // the ordinary window as maximized when it fills the usable screen.
    const NSRect frame = gallery_window.frame;
    const NSRect available = screen.visibleFrame;
    return std::abs(frame.origin.x - available.origin.x) < 1 &&
           std::abs(frame.origin.y - available.origin.y) < 1 &&
           std::abs(frame.size.width - available.size.width) < 1 &&
           std::abs(frame.size.height - available.size.height) < 1;
  }
}

bool ConfigureStreamWindowLiveResize(void *opaque_slint_view,
                                     std::function<void(bool)> transition_changed) {
  @autoreleasepool {
    NSView *slint_view = (__bridge NSView *)opaque_slint_view;
    NSWindow *window = slint_view.window;
    NSView *content_view = window.contentView;
    if (content_view == nil) {
      return false;
    }

    // Customize the fullscreen animations. Winit still handles native
    // fullscreen state, closing, input and resizing.
    auto* delegate = (CrossDeskStreamTransitionDelegate*)
        objc_getAssociatedObject(window, &stream_transition_key);
    if (!delegate) {
      delegate = [[CrossDeskStreamTransitionDelegate alloc] init];
      objc_setAssociatedObject(window, &stream_transition_key, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
      NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
      for (NSNotificationName name in @[NSWindowWillEnterFullScreenNotification,
                                        NSWindowWillExitFullScreenNotification]) {
        [center addObserver:delegate selector:@selector(willToggleFullscreen:) name:name object:window];
      }
      for (NSNotificationName name in @[NSWindowDidEnterFullScreenNotification,
                                        NSWindowDidExitFullScreenNotification]) {
        [center addObserver:delegate selector:@selector(didToggleFullscreen:) name:name object:window];
      }
    }
    delegate.transitionChanged = ^(bool active) { transition_changed(active); };
    if (window.delegate != delegate) {
      delegate.backendDelegate = window.delegate;
      window.delegate = delegate;
    }

    // During an ordinary edge resize, scale one cached frame and redraw once
    // the new window size has settled.
    window.preservesContentDuringLiveResize = YES;
    content_view.layerContentsRedrawPolicy =
        NSViewLayerContentsRedrawBeforeViewResize;
    content_view.layerContentsPlacement =
        NSViewLayerContentsPlacementScaleProportionallyToFit;
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    window.excludedFromWindowsMenu = NO;
    window.collectionBehavior =
        (window.collectionBehavior & ~(NSWindowCollectionBehaviorFullScreenAuxiliary |
                                       NSWindowCollectionBehaviorFullScreenNone)) |
        NSWindowCollectionBehaviorFullScreenPrimary;
    content_view.needsDisplay = YES;
    return true;
  }
}

float StreamWindowVideoTransitionInset(void* opaque_view, float width, float height,
                                      bool* transitioning) {
  NSView* view = (__bridge NSView*)opaque_view;
  auto* delegate = (CrossDeskStreamTransitionDelegate*)
      objc_getAssociatedObject(view.window, &stream_transition_key);
  if (transitioning) *transitioning = delegate.animatingVideo;
  return [delegate videoInsetForContentSize:NSMakeSize(width, height) inWindow:view.window];
}

bool IsStreamWindowActive(void *opaque_slint_view) {
  NSView *view = (__bridge NSView *)opaque_slint_view;
  return [NSApp isActive] && view.window.isKeyWindow;
}

bool PlaceMacStreamControlPointer(void* slint_view, float pixel_x, float pixel_y) {
  @autoreleasepool {
    NSView *view = (__bridge NSView *)slint_view;
    NSWindow *window = view.window;
    if (!window || !IsStreamWindowActive(slint_view)) return false;
    NSPoint current = [view convertPoint:window.mouseLocationOutsideOfEventStream fromView:nil];
    if (!NSPointInRect(current, view.bounds) ||
        [NSWindow windowNumberAtPoint:NSEvent.mouseLocation belowWindowWithWindowNumber:0]
            != window.windowNumber) return false;
    const CGFloat scale = window.backingScaleFactor;
    NSRect bounds = view.bounds;
    if (NSIsEmptyRect(bounds) || scale <= 0) return false;
    const CGFloat x = std::clamp<CGFloat>(pixel_x / scale, 0,
        std::max<CGFloat>(0, NSWidth(bounds) - 1 / scale));
    const CGFloat y = std::clamp<CGFloat>(pixel_y / scale, 0,
        std::max<CGFloat>(0, NSHeight(bounds) - 1 / scale));
    NSPoint local = NSMakePoint(NSMinX(bounds) + x,
        view.isFlipped ? NSMinY(bounds) + y : NSMaxY(bounds) - y);
    NSPoint client = [view convertPoint:local toView:nil];
    NSPoint screen = [window convertPointToScreen:client];
    if ([NSWindow windowNumberAtPoint:screen belowWindowWithWindowNumber:0] != window.windowNumber)
      return false;
    NSScreen *primary = NSScreen.screens.firstObject;
    if (!primary) return false;
    const CGPoint target = CGPointMake(screen.x, NSMaxY(primary.frame) - screen.y);
    if (CGWarpMouseCursorPosition(target) != kCGErrorSuccess) return false;
    // Quartz warps do not post a mouse event. Feed AppKit a move so hover and
    // the next button event use the new location even if the mouse stays still.
    NSEvent *move = [NSEvent mouseEventWithType:NSEventTypeMouseMoved location:client
        modifierFlags:NSEvent.modifierFlags timestamp:NSProcessInfo.processInfo.systemUptime
        windowNumber:window.windowNumber context:nil eventNumber:0 clickCount:0 pressure:0];
    [NSApp postEvent:move atStart:YES];
    return true;
  }
}

bool IsStreamWindowFullscreen(void *opaque_slint_view) {
  NSView *view = (__bridge NSView *)opaque_slint_view;
  return (view.window.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

void ShowFileTransferNotification(void* slint_view,
                                  const std::function<void()>& show) {
  @autoreleasepool {
    NSWindow *previous_key = NSApp.keyWindow;
    NSView *view = (__bridge NSView *)slint_view;
    NSWindow *notification = view.window;
    notification.excludedFromWindowsMenu = YES;
    notification.hidesOnDeactivate = NO;
    notification.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
        NSWindowCollectionBehaviorFullScreenAuxiliary |
        NSWindowCollectionBehaviorIgnoresCycle;
    show();
    // Winit's show path calls makeKeyAndOrderFront even for frameless windows.
    // Restore the input owner synchronously before the next event-loop turn.
    if (notification.isKeyWindow) {
      if (previous_key && previous_key != notification) [previous_key makeKeyWindow];
      else [notification resignKeyWindow];
    }
  }
}

void PrepareNativeFileDialog() {
  const auto generation = ++file_dialog_prepare_generation;
  // Creating NSOpenPanel initializes the system file browser and can take
  // hundreds of milliseconds. Do that once after the main window is visible,
  // without showing a panel, changing focus, or blocking the initial frame.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC),
      dispatch_get_main_queue(), ^{
        if (generation != file_dialog_prepare_generation ||
            file_dialog_request || prepared_file_dialog) return;
        NSOpenPanel *panel = CreateFileDialog();
        if (generation == file_dialog_prepare_generation &&
            !file_dialog_request && !prepared_file_dialog)
          prepared_file_dialog = panel;
      });
}

void OpenNativeFileDialog(const std::string& title,
                          std::function<void(std::string)> completion) {
  if (file_dialog_request) {
    completion({});
    return;
  }
  const auto request = std::make_shared<FileDialogRequest>(
      FileDialogRequest{title, std::move(completion)});
  file_dialog_request = request;
  // Leave the current Slint callback before AppKit creates or shows the panel.
  // runModal would let display-link callbacks reenter Slint's timer dispatch.
  dispatch_async(dispatch_get_main_queue(), ^{
    if (file_dialog_request != request) return;
    NSOpenPanel *panel = prepared_file_dialog ?: CreateFileDialog();
    prepared_file_dialog = nil;
    file_dialog = panel;

    NSString *prompt =
        [[NSString alloc] initWithBytes:request->title.data()
                                 length:request->title.size()
                               encoding:NSUTF8StringEncoding];
    panel.title = prompt ?: @"";
    panel.message = prompt ?: @"";

    // tinyfiledialogs uses an external osascript process on macOS, so its
    // chooser can appear without becoming the active window. Keep the picker
    // inside CrossDesk and explicitly activate both the app and panel.
    [NSApp activateIgnoringOtherApps:YES];
    NSWindow *owner = NSApp.keyWindow ?: NSApp.mainWindow;
    if (owner != nil) {
      [owner makeKeyAndOrderFront:nil];
    }
    NSScreen *target_screen = owner.screen ?: NSScreen.mainScreen;
    CenterWindowOnScreen(panel, target_screen);
    __weak NSOpenPanel *weak_panel = panel;
    [panel beginWithCompletionHandler:^(NSModalResponse result) {
      // Cancellation can complete a request before AppKit calls this block.
      if (file_dialog_request != request) return;
      NSOpenPanel *finished_panel = weak_panel;
      const char *selected = result == NSModalResponseOK
          ? finished_panel.URL.fileSystemRepresentation : nullptr;
      const std::string path = selected ? selected : "";
      [finished_panel orderOut:nil];
      file_dialog = nil;
      // AppKit has returned the panel to its configuration phase. Keep the
      // browser alive for the next selection instead of rebuilding it.
      prepared_file_dialog = finished_panel;
      file_dialog_request.reset();
      auto finished = std::move(request->completion);
      finished(path);
    }];
    if (file_dialog_request == request) [panel makeKeyAndOrderFront:nil];
  });
}

void SaveNativeFileDialog(const std::string& title, const std::string& filename,
                          std::function<void(std::string)> completion) {
  if (file_dialog_request) { completion({}); return; }
  const auto request = std::make_shared<FileDialogRequest>(
      FileDialogRequest{title, std::move(completion), filename});
  file_dialog_request = request;
  dispatch_async(dispatch_get_main_queue(), ^{
    if (file_dialog_request != request) return;
    NSSavePanel *panel = [NSSavePanel savePanel];
    file_dialog = panel;
    panel.title = [NSString stringWithUTF8String:request->title.c_str()];
    panel.nameFieldStringValue = [NSString stringWithUTF8String:request->filename.c_str()];
    panel.allowedContentTypes = @[UTTypeZIP];
    panel.allowsOtherFileTypes = NO;
    panel.canCreateDirectories = YES;
    [NSApp activateIgnoringOtherApps:YES];
    NSWindow *owner = NSApp.keyWindow ?: NSApp.mainWindow;
    CenterWindowOnScreen(panel, owner.screen ?: NSScreen.mainScreen);
    __weak NSSavePanel *weak_panel = panel;
    [panel beginWithCompletionHandler:^(NSModalResponse result) {
      if (file_dialog_request != request) return;
      NSSavePanel *finished_panel = weak_panel;
      const char *selected = result == NSModalResponseOK
          ? finished_panel.URL.fileSystemRepresentation : nullptr;
      const std::string path = selected ? selected : "";
      [finished_panel orderOut:nil];
      file_dialog = nil;
      file_dialog_request.reset();
      auto finished = std::move(request->completion);
      finished(path);
    }];
    if (file_dialog_request == request) [panel makeKeyAndOrderFront:nil];
  });
}

void CancelNativeFileDialog() {
  ++file_dialog_prepare_generation;
  prepared_file_dialog = nil;
  const auto request = std::move(file_dialog_request);
  if (!request) return;
  NSSavePanel *panel = file_dialog;
  file_dialog = nil;
  [panel cancel:nil];
  [panel orderOut:nil];
  auto finished = std::move(request->completion);
  finished({});
}

} // namespace crossdesk
