#include "mac_tray.h"

#if defined(__APPLE__)

#include <SDL3/SDL.h>

#import <Cocoa/Cocoa.h>

#include "localization.h"

#include <utility>

@interface CrossDeskMacTrayTarget : NSObject
- (instancetype)initWithOwner:(crossdesk::MacTrayImpl *)owner;
- (void)statusItemClicked:(id)sender;
- (void)exitApplication:(id)sender;
@end

namespace crossdesk {

struct MacTrayImpl {
  explicit MacTrayImpl(::SDL_Window *window, std::string tray_tooltip,
                       int language_index_value)
      : app_window(window),
        tooltip(std::move(tray_tooltip)),
        language_index(language_index_value),
        target([[CrossDeskMacTrayTarget alloc] initWithOwner:this]) {}

  ~MacTrayImpl() {
    RemoveTrayIcon();
    target = nil;
  }

  void MinimizeToTray() {
    EnsureStatusItem();
    if (app_window) {
      SDL_HideWindow(app_window);
    }
  }

  void RemoveTrayIcon() {
    if (!status_item) {
      return;
    }

    [[NSStatusBar systemStatusBar] removeStatusItem:status_item];
    status_item = nil;
  }

  void ShowWindow() {
    if (!app_window) {
      return;
    }

    SDL_ShowWindow(app_window);
    SDL_RaiseWindow(app_window);
    [NSApp activateIgnoringOtherApps:YES];
  }

  void ShowMenu() {
    EnsureStatusItem();
    if (!status_item) {
      return;
    }

    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"CrossDesk"];
    NSString *exit_title =
        NSStringFromUtf8(localization::exit_program
                             [localization::detail::ClampLanguageIndex(
                                 language_index)]);
    NSMenuItem *exit_item = [[NSMenuItem alloc] initWithTitle:exit_title
                                                       action:@selector(exitApplication:)
                                                keyEquivalent:@""];
    [exit_item setTarget:target];
    [menu addItem:exit_item];

    NSStatusBarButton *button = [status_item button];
    if (!button) {
      return;
    }

    const NSRect bounds = [button bounds];
    [menu popUpMenuPositioningItem:nil
                        atLocation:NSMakePoint(NSMinX(bounds), NSMinY(bounds))
                            inView:button];
  }

  void RequestExit() {
    SDL_Event event;
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
  }

 private:
  void EnsureStatusItem() {
    if (status_item) {
      return;
    }

    status_item = [[NSStatusBar systemStatusBar]
        statusItemWithLength:NSSquareStatusItemLength];
    NSStatusBarButton *button = [status_item button];
    if (!button) {
      return;
    }

    [button setToolTip:NSStringFromUtf8(tooltip)];

    NSImage *crossdesk_icon = LoadCrossDeskIcon();
    if (crossdesk_icon) {
      NSImage *status_icon = [crossdesk_icon copy];
      [status_icon setSize:NSMakeSize(18.0, 18.0)];
      [status_icon setTemplate:NO];
      [button setImage:status_icon];
      [button setImagePosition:NSImageOnly];
    } else {
      [button setTitle:@"CD"];
    }

    [button setTarget:target];
    [button setAction:@selector(statusItemClicked:)];
    [button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
  }

  NSString *NSStringFromUtf8(const std::string &text) {
    return [NSString stringWithUTF8String:text.c_str()];
  }

  NSImage *LoadCrossDeskIcon() {
    NSImage *icon = LoadIconFromBundleResource(@"crossdesk");
    if (!icon) {
      icon = LoadIconFromBundleResource(@"crossedesk");
    }
    if (!icon) {
      icon = LoadIconFromDevelopmentPath();
    }
    if (!icon) {
      icon = [NSApp applicationIconImage];
    }
    return icon;
  }

  NSImage *LoadIconFromBundleResource(NSString *resource_name) {
    NSString *icon_path =
        [[NSBundle mainBundle] pathForResource:resource_name ofType:@"icns"];
    return LoadIconFromPath(icon_path);
  }

  NSImage *LoadIconFromDevelopmentPath() {
    NSMutableArray<NSString *> *candidate_paths = [NSMutableArray array];

    NSString *current_directory =
        [[NSFileManager defaultManager] currentDirectoryPath];
    [candidate_paths
        addObject:[current_directory
                      stringByAppendingPathComponent:
                          @"icons/macos/crossdesk.icns"]];

    const char *base_path = SDL_GetBasePath();
    if (base_path && base_path[0] != '\0') {
      NSString *base_directory = NSStringFromUtf8(base_path);
      [candidate_paths
          addObject:[base_directory
                        stringByAppendingPathComponent:
                            @"icons/macos/crossdesk.icns"]];
      [candidate_paths
          addObject:[base_directory
                        stringByAppendingPathComponent:
                            @"../../../../icons/macos/crossdesk.icns"]];
    }

    for (NSString *candidate_path in candidate_paths) {
      NSImage *icon = LoadIconFromPath(
          [candidate_path stringByStandardizingPath]);
      if (icon) {
        return icon;
      }
    }

    return nil;
  }

  NSImage *LoadIconFromPath(NSString *icon_path) {
    if (![icon_path length]) {
      return nil;
    }
    if (![[NSFileManager defaultManager] fileExistsAtPath:icon_path]) {
      return nil;
    }
    return [[NSImage alloc] initWithContentsOfFile:icon_path];
  }

  ::SDL_Window *app_window = nullptr;
  std::string tooltip;
  int language_index = 0;
  NSStatusItem *status_item = nil;
  CrossDeskMacTrayTarget *target = nil;
};

MacTray::MacTray(::SDL_Window *app_window, const std::string &tooltip,
                 int language_index)
    : impl_(
          std::make_unique<MacTrayImpl>(app_window, tooltip, language_index)) {}

MacTray::~MacTray() = default;

void MacTray::MinimizeToTray() { impl_->MinimizeToTray(); }

void MacTray::RemoveTrayIcon() { impl_->RemoveTrayIcon(); }

}  // namespace crossdesk

@implementation CrossDeskMacTrayTarget {
  crossdesk::MacTrayImpl *owner_;
}

- (instancetype)initWithOwner:(crossdesk::MacTrayImpl *)owner {
  self = [super init];
  if (self) {
    owner_ = owner;
  }
  return self;
}

- (void)statusItemClicked:(id)sender {
  (void)sender;
  if (!owner_) {
    return;
  }

  NSEvent *event = [NSApp currentEvent];
  if (event && [event type] == NSEventTypeRightMouseUp) {
    owner_->ShowMenu();
    return;
  }

  owner_->ShowWindow();
}

- (void)exitApplication:(id)sender {
  (void)sender;
  if (owner_) {
    owner_->RequestExit();
  }
}

@end

#endif  // __APPLE__
