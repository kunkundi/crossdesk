/*
 * @Author: DI JUNKUN
 * @Date: 2025-12-18
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#include "clipboard.h"

#include <AppKit/AppKit.h>
#include <CoreFoundation/CoreFoundation.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "rd_log.h"

namespace crossdesk {

extern std::atomic<bool> g_monitoring;
extern std::mutex g_monitor_mutex;
extern std::string g_last_clipboard_text;
extern Clipboard::OnClipboardChanged g_on_clipboard_changed;

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

extern void HandleClipboardChange();

void StartMacOSClipboardMonitoring() {
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
                                            if (!g_monitoring.load()) {
                                              [timer invalidate];
                                              return;
                                            }
                                            NSInteger currentChangeCount = [pasteboard changeCount];
                                            if (currentChangeCount != lastChangeCount) {
                                              lastChangeCount = currentChangeCount;
                                              HandleClipboardChange();
                                            }
                                          }];

    while (g_monitoring.load()) {
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

void StopMacOSClipboardMonitoring() {
  // Wake up the RunLoop immediately so it can check g_monitoring and exit
  // This ensures the RunLoop exits promptly instead of waiting up to 0.1 seconds
  if (g_monitor_runloop) {
    CFRunLoopWakeUp(g_monitor_runloop);
  }
}

}  // namespace crossdesk
