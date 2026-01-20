#include "window_util_mac.h"

#if defined(__APPLE__)

#include <SDL3/SDL.h>

#import <Cocoa/Cocoa.h>

namespace crossdesk {

static NSWindow* GetNSWindowFromSDL(::SDL_Window* window) {
  if (!window) {
    return nil;
  }

#if !defined(SDL_PROP_WINDOW_COCOA_WINDOW_POINTER)
  return nil;
#else
  SDL_PropertiesID props = SDL_GetWindowProperties(window);
  void* cocoa_window_ptr =
      SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
  if (!cocoa_window_ptr) {
    return nil;
  }
  return (__bridge NSWindow*)cocoa_window_ptr;
#endif
}

void MacSetWindowAlwaysOnTop(::SDL_Window* window, bool always_on_top) {
  NSWindow* ns_window = GetNSWindowFromSDL(window);
  if (!ns_window) {
    (void)always_on_top;
    return;
  }

  // Keep above normal windows.
  const NSInteger level = always_on_top ? NSFloatingWindowLevel : NSNormalWindowLevel;
  [ns_window setLevel:level];

  // Optional: keep visible across Spaces/fullscreen. Safe as best-effort.
  NSWindowCollectionBehavior behavior = [ns_window collectionBehavior];
  behavior |= NSWindowCollectionBehaviorCanJoinAllSpaces;
  behavior |= NSWindowCollectionBehaviorFullScreenAuxiliary;
  [ns_window setCollectionBehavior:behavior];
}

void MacSetWindowExcludedFromWindowMenu(::SDL_Window* window, bool excluded) {
  NSWindow* ns_window = GetNSWindowFromSDL(window);
  if (!ns_window) {
    (void)excluded;
    return;
  }

  [ns_window setExcludedFromWindowsMenu:excluded];

  NSWindowCollectionBehavior behavior = [ns_window collectionBehavior];
  behavior |= NSWindowCollectionBehaviorIgnoresCycle;
  behavior |= NSWindowCollectionBehaviorTransient;
  [ns_window setCollectionBehavior:behavior];
}

}  // namespace crossdesk

#endif  // __APPLE__
