/*
 * @Author: DI JUNKUN
 * @Date: 2025-12-18
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#include "clipboard.h"

#include <AppKit/AppKit.h>
#include <CoreFoundation/CoreFoundation.h>
#include <string>

#include "platform/clipboard_backend.h"
#include "rd_log.h"

namespace crossdesk {

static CFRunLoopRef g_monitor_runloop = nullptr;

std::string Clipboard::GetText() {
  @autoreleasepool {
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    NSString* string = [pasteboard stringForType:NSPasteboardTypeString];
    if (string == nil) {
      return "";
    }
    return std::string([string UTF8String]);
  }
}

bool Clipboard::SetText(const std::string& text) {
  @autoreleasepool {
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    NSString* string = [NSString stringWithUTF8String:text.c_str()];
    if (string == nil) {
      LOG_ERROR("Clipboard::SetText: failed to create NSString");
      return false;
    }
    BOOL success = [pasteboard setString:string forType:NSPasteboardTypeString];
    return success == YES;
  }
}

bool Clipboard::HasText() {
  @autoreleasepool {
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    NSArray* types = [pasteboard types];
    return [types containsObject:NSPasteboardTypeString];
  }
}

namespace platform {

void RunClipboardMonitor(int) {
  @autoreleasepool {
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];

    // Store RunLoop reference for waking up
    NSRunLoop* runLoop = [NSRunLoop currentRunLoop];
    g_monitor_runloop = [runLoop getCFRunLoop];
    if (g_monitor_runloop) {
      CFRetain(g_monitor_runloop);
    }

    // Track changeCount to detect clipboard changes
    // Use __block to allow modification inside the block
    __block NSInteger lastChangeCount = [pasteboard changeCount];

    LOG_INFO("Clipboard event monitoring started (macOS)");

    // Use a timer to periodically check changeCount
    // This is more reliable than NSPasteboardDidChangeNotification which may not be available
    NSTimer* timer =
        [NSTimer scheduledTimerWithTimeInterval:0.1
                                        repeats:YES
                                          block:^(NSTimer* timer) {
                                            if (!ClipboardMonitoring()) {
                                              [timer invalidate];
                                              return;
                                            }
                                            NSInteger currentChangeCount = [pasteboard changeCount];
                                            if (currentChangeCount != lastChangeCount) {
                                              lastChangeCount = currentChangeCount;
                                              HandleClipboardChange();
                                            }
                                          }];

    while (ClipboardMonitoring()) {
      @autoreleasepool {
        NSDate* date = [NSDate dateWithTimeIntervalSinceNow:0.1];
        [runLoop runMode:NSDefaultRunLoopMode beforeDate:date];
      }
    }

    // Cleanup
    [timer invalidate];
    if (g_monitor_runloop) {
      CFRelease(g_monitor_runloop);
      g_monitor_runloop = nullptr;
    }
  }
}

void WakeClipboardMonitor() {
  // Wake up the RunLoop immediately so it can observe monitoring shutdown.
  // This ensures the RunLoop exits promptly instead of waiting up to 0.1 seconds
  if (g_monitor_runloop) {
    CFRunLoopWakeUp(g_monitor_runloop);
  }
}

}  // namespace platform
}  // namespace crossdesk
