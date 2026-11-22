/* Copyright (c) 2026 by DI JUNKUN, All Rights Reserved. */

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <cstdlib>
#include <libproc.h>
#include <mutex>
#include <utility>
#include <unistd.h>

#include "privacy_backend.h"
#include "privacy_capture_state.h"
#include "privacy_cover_view.h"
#include "privacy_cursor_mac.h"
#include "rd_log.h"
#include "../input/mac_injected_event.h"

// These windows never take keyboard focus. Remote clicks pass through them;
// physical input is intercepted independently on the controller's run loop.
@interface CrossDeskPrivacyPanel : NSPanel
@end
@implementation CrossDeskPrivacyPanel
- (BOOL)canBecomeKeyWindow { return NO; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

namespace crossdesk {
namespace {
using Clock = std::chrono::steady_clock;
std::mutex tasks_mutex;
std::deque<std::function<void()>> tasks;
std::mutex capture_mutex;
MacPrivacyCaptureState capture_state;

void OnMain(std::function<void()> task) {
  {
    std::lock_guard lock(tasks_mutex);
    tasks.push_back(std::move(task));
  }
  dispatch_async(dispatch_get_main_queue(), ^{ FlushMacPrivacyTasks(); });
}

void PublishCovers(std::vector<uint32_t> windows, uint32_t cursor_window = 0) {
  {
    std::lock_guard lock(capture_mutex);
    capture_state.excluded_windows = std::move(windows);
    capture_state.cursor_window = cursor_window;
    ++capture_state.revision;
  }
  dispatch_async(dispatch_get_main_queue(), ^{
    [NSNotificationCenter.defaultCenter postNotificationName:@"CrossDeskPrivacyCoversChanged"
                                                     object:nil];
  });
}

struct Monitor {
  CGDirectDisplayID id = 0;
  CGRect bounds{};  // CoreGraphics desktop coordinates (points).
  NSRect frame{};   // AppKit desktop coordinates (bottom left).
  CrossDeskPrivacyPanel* __strong cover = nil;
};
struct WindowState {
  std::mutex mutex;
  std::vector<Monitor> monitors;
  bool recovered = true;
  bool recovering = false;
  bool covers_shown = false;
  bool check_queued = false;
  MacPrivacyCursor cursor;
  std::string failure;
  Clock::time_point checked = Clock::now();
};

CrossDeskPrivacyPanel* MakePanel(NSRect frame, NSInteger level) {
  auto* panel = [[CrossDeskPrivacyPanel alloc]
      initWithContentRect:frame
                styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                  backing:NSBackingStoreBuffered defer:NO];
  panel.releasedWhenClosed = NO;
  panel.hidesOnDeactivate = NO;
  panel.opaque = YES;
  panel.backgroundColor = NSColor.blackColor;
  panel.hasShadow = NO;
  panel.ignoresMouseEvents = YES;
  panel.level = level;
  panel.animationBehavior = NSWindowAnimationBehaviorNone;
  panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
      NSWindowCollectionBehaviorFullScreenAuxiliary |
      NSWindowCollectionBehaviorStationary |
      NSWindowCollectionBehaviorIgnoresCycle;
  // Exclusion is explicit in ScreenCaptureKit. SharingNone alone does not
  // exclude windows on recent macOS releases, and can hide them from discovery.
  panel.sharingType = NSWindowSharingReadOnly;
  return panel;
}

bool IsSystemCursorPlane(NSDictionary* info, CGRect bounds) {
  const auto layer = [info[(__bridge NSString*)kCGWindowLayer] intValue];
  // WindowServer's cursor plane also carries small recording indicators.
  // Validate both its geometry and executable identity before ignoring it.
  if (layer != CGWindowLevelForKey(kCGCursorWindowLevelKey) ||
      bounds.size.width <= 0 || bounds.size.height <= 0 ||
      bounds.size.width > 512 || bounds.size.height > 512)
    return false;
  char executable[PROC_PIDPATHINFO_MAXSIZE]{};
  const auto pid = [info[(__bridge NSString*)kCGWindowOwnerPID] intValue];
  static const std::string system_executable = [] {
    char resolved[PROC_PIDPATHINFO_MAXSIZE]{};
    return realpath("/System/Library/PrivateFrameworks/SkyLight.framework/Resources/WindowServer",
                    resolved) ? std::string(resolved) : std::string();
  }();
  return proc_pidpath(pid, executable, sizeof(executable)) > 0 &&
      !system_executable.empty() && system_executable == executable;
}

std::string CoverageFailure(const std::vector<Monitor>& monitors) {
  uint32_t count = 0;
  if (CGGetActiveDisplayList(0, nullptr, &count) != kCGErrorSuccess || !count)
    return "No active macOS desktop; privacy screen unavailable";
  std::vector<CGDirectDisplayID> displays(count);
  if (CGGetActiveDisplayList(count, displays.data(), &count) != kCGErrorSuccess ||
      count != monitors.size())
    return "Display topology changed; turn privacy off to recover";
  for (const auto& m : monitors) {
    if (std::find(displays.begin(), displays.end(), m.id) == displays.end() ||
        !CGRectEqualToRect(CGDisplayBounds(m.id), m.bounds) ||
        !m.cover.visible || !m.cover.isOnActiveSpace ||
        !NSEqualRects(m.cover.frame, m.frame) || m.cover.alphaValue != 1.0)
      return "Privacy cover or display geometry changed; privacy screen unavailable";
  }
  NSDictionary* session = CFBridgingRelease(CGSessionCopyCurrentDictionary());
  if (!session || ![session[(__bridge NSString*)kCGSessionOnConsoleKey] boolValue])
    return "macOS session is not on console; privacy screen unavailable";

  NSArray* windows = CFBridgingRelease(CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
      kCGNullWindowID));
  if (!windows) return "Unable to verify macOS privacy window order";
  for (const auto& m : monitors) {
    bool found = false;
    for (NSDictionary* info in windows) {
      const auto window = [info[(__bridge NSString*)kCGWindowNumber] unsignedIntValue];
      if (window == m.cover.windowNumber) { found = true; break; }
      if ([info[(__bridge NSString*)kCGWindowAlpha] doubleValue] <= 0) continue;
      CGRect bounds{};
      if (!CGRectMakeWithDictionaryRepresentation(
              (__bridge CFDictionaryRef)info[(__bridge NSString*)kCGWindowBounds], &bounds))
        return "Unable to verify a window above the privacy cover";
      if (CGRectIntersectsRect(bounds, m.bounds)) {
        if (IsSystemCursorPlane(info, bounds)) continue;
        LOG_WARN("macOS privacy overlap: window={}, pid={}, layer={}, alpha={}, "
                 "bounds=({}, {}, {}, {}), cover={}, cover_level={}",
                 window,
                 [info[(__bridge NSString*)kCGWindowOwnerPID] intValue],
                 [info[(__bridge NSString*)kCGWindowLayer] intValue],
                 [info[(__bridge NSString*)kCGWindowAlpha] doubleValue],
                 bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height,
                 m.cover.windowNumber, m.cover.level);
        return "A system or application window overlaps the privacy cover; privacy screen unavailable";
      }
    }
    if (!found) return "Privacy cover disappeared; privacy screen unavailable";
  }
  return {};
}

class MacPrivacyBackend final : public PrivacyBackend {
 public:
  MacPrivacyBackend() : state_(std::make_shared<WindowState>()) {
    loop_ = CFRunLoopGetCurrent();
    CFRetain(loop_);
    CFRunLoopSourceContext context{};
    wake_source_ = CFRunLoopSourceCreate(nullptr, 0, &context);
    CFRunLoopAddSource(loop_, wake_source_, kCFRunLoopDefaultMode);
  }
  ~MacPrivacyBackend() override {
    Recover();
    CFRunLoopRemoveSource(loop_, wake_source_, kCFRunLoopDefaultMode);
    CFRelease(wake_source_);
    CFRelease(loop_);
  }
  PrivacyCapabilities Query() override {
    if (@available(macOS 14.0, *)) {
      if (!CGPreflightScreenCaptureAccess())
        return {false, false, "Grant Screen Recording permission to use privacy screen"};
      if (!AXIsProcessTrusted())
        return {false, false, "Grant Accessibility permission to protect local input and enable local recovery"};
      if (!MacPrivacyCursor::Supported())
        return {false, false, "Background cursor control unavailable on this macOS version"};
      return {true, true, "macOS privacy screen available (ScreenCaptureKit)"};
    }
    return {false, false, "Privacy screen requires macOS 14 or newer"};
  }
  bool Enable(bool block_input, const PrivacyScreenText& text, std::string& error) override {
    const auto caps = Query();
    if (!caps.overlay) { error = caps.reason; return false; }
    block_input_ = block_input;
    allow_local_unlock_ = text.allow_local_unlock;
    emergency_ = false;
    tap_failed_ = false;
    CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) |
        CGEventMaskBit(kCGEventFlagsChanged) | CGEventMaskBit(kCGEventLeftMouseDown) |
        CGEventMaskBit(kCGEventLeftMouseUp) | CGEventMaskBit(kCGEventRightMouseDown) |
        CGEventMaskBit(kCGEventRightMouseUp) | CGEventMaskBit(kCGEventMouseMoved) |
        CGEventMaskBit(kCGEventLeftMouseDragged) | CGEventMaskBit(kCGEventRightMouseDragged) |
        CGEventMaskBit(kCGEventOtherMouseDown) | CGEventMaskBit(kCGEventOtherMouseUp) |
        CGEventMaskBit(kCGEventOtherMouseDragged) | CGEventMaskBit(kCGEventScrollWheel) |
        CGEventMaskBit(14); // NX_SYSDEFINED: media/brightness keys.
    tap_ = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap,
                           kCGEventTapOptionDefault, mask, Input, this);
    if (!tap_) { error = "Cannot install macOS privacy input tap"; return false; }
    tap_source_ = CFMachPortCreateRunLoopSource(nullptr, tap_, 0);
    if (!tap_source_) { error = "Cannot create macOS privacy input run loop"; return false; }
    CFRunLoopAddSource(loop_, tap_source_, kCFRunLoopDefaultMode);
    CGEventTapEnable(tap_, true);
    {
      std::lock_guard lock(state_->mutex);
      state_->recovered = false;
      state_->failure.clear();
      state_->checked = Clock::now();
    }
    auto state = state_;
    OnMain([state, text] {
      @autoreleasepool {
        std::lock_guard lock(state->mutex);
        if (!state->cursor.Prepare()) {
          state->failure = "Cannot prepare the privacy cursor";
          return;
        }
        std::vector<uint32_t> covers;
        for (NSScreen* screen in NSScreen.screens) {
          Monitor m;
          m.id = [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
          m.bounds = CGDisplayBounds(m.id);
          m.frame = screen.frame;
          NSView* content = CreateMacPrivacyCoverView(
              NSMakeRect(0, 0, m.frame.size.width, m.frame.size.height),
              [NSString stringWithUTF8String:(text.allow_local_unlock ? text.unlock_hint : text.locked_hint).c_str()],
              text.allow_local_unlock, text.black_screen);
          if (!content) {
            state->failure = "Could not load the CrossDesk privacy screen logo";
            break;
          }
          m.cover = MakePanel(m.frame, CGWindowLevelForKey(kCGScreenSaverWindowLevelKey) + 1);
          m.cover.contentView = content;
          // Register transparent windows first. Make them opaque only after
          // SCK has acknowledged exclusion, avoiding a black remote frame.
          m.cover.alphaValue = 0;
          [m.cover orderFrontRegardless];
          covers.push_back(static_cast<uint32_t>(m.cover.windowNumber));
          state->monitors.push_back(std::move(m));
        }
        PublishCovers(std::move(covers), state->cursor.WindowId());
        if (state->monitors.empty()) state->failure = "No active macOS desktop";
        state->checked = Clock::now();
      }
    });
    return true;
  }
  void UpdateText(const PrivacyScreenText& text) override {
    allow_local_unlock_ = text.allow_local_unlock;
    auto state = state_;
    OnMain([state, text] {
      std::lock_guard lock(state->mutex);
      for (const auto& monitor : state->monitors) {
        if (!UpdateMacPrivacyCoverView(monitor.cover.contentView,
            [NSString stringWithUTF8String:(text.allow_local_unlock ? text.unlock_hint : text.locked_hint).c_str()],
            text.allow_local_unlock, text.black_screen)) {
          state->failure = "Could not load the CrossDesk privacy screen logo";
          break;
        }
      }
    });
  }
  void Recover() override {
    if (tap_) { CGEventTapEnable(tap_, false); CFMachPortInvalidate(tap_); }
    if (tap_source_) {
      CFRunLoopRemoveSource(loop_, tap_source_, kCFRunLoopDefaultMode);
      CFRelease(tap_source_); tap_source_ = nullptr;
    }
    if (tap_) { CFRelease(tap_); tap_ = nullptr; }
    {
      std::lock_guard lock(state_->mutex);
      if (state_->recovered || state_->recovering) return;
      state_->recovering = true;
    }
    auto state = state_;
    OnMain([state] {
      std::lock_guard lock(state->mutex);
      for (auto& m : state->monitors) {
        [m.cover orderOut:nil]; [m.cover close]; m.cover = nil;
      }
      state->monitors.clear();
      state->covers_shown = false;
      state->cursor.Restore();
      PublishCovers({});
      state->failure.clear();
      state->recovering = false;
      state->recovered = true;
    });
  }
  bool IsRecovered() const override {
    std::lock_guard lock(state_->mutex);
    return state_->recovered && !tap_;
  }
  bool RecoveryPending() const override {
    std::lock_guard lock(state_->mutex);
    return state_->recovering;
  }
  PrivacyHealth Poll() override {
    if (emergency_) { emergency_ = false; return {{}, true}; }
    if (tap_ && (tap_failed_ || !CGEventTapIsEnabled(tap_) || !AXIsProcessTrusted()))
      return {"Local input protection interrupted; privacy screen unavailable", false};
    auto state = state_;
    std::lock_guard lock(state->mutex);
    if (state->recovered || state->recovering) return {};
    if (!state->failure.empty()) return {state->failure, false};
    const auto age = Clock::now() - state->checked;
    if (age > std::chrono::seconds(1))
      return {"macOS privacy window checks stopped responding; privacy screen unavailable", false};
    if (!state->check_queued && age > std::chrono::milliseconds(100)) {
      state->check_queued = true;
      OnMain([state] {
        std::lock_guard lock(state->mutex);
        if (!state->recovered && !state->recovering && state->failure.empty()) {
          const auto capture = GetMacPrivacyCaptureState();
          if (!state->covers_shown && !state->monitors.empty() &&
              capture.revision == capture.applied_revision) {
            if (!state->cursor.Hide()) {
              state->failure = "Cannot hide the local privacy cursor";
              state->checked = Clock::now();
              state->check_queued = false;
              return;
            }
            for (auto& m : state->monitors) {
              m.cover.alphaValue = 1;
              [m.cover displayIfNeeded];
              [m.cover orderFrontRegardless];
            }
            state->covers_shown = true;
          }
          if (state->covers_shown) {
            state->failure = state->cursor.Healthy() ? CoverageFailure(state->monitors)
                : "Cannot keep the local privacy cursor hidden";
          }
        }
        state->checked = Clock::now();
        state->check_queued = false;
      });
    }
    const auto capture = GetMacPrivacyCaptureState();
    return {{}, false, state->covers_shown &&
                       capture.revision == capture.applied_revision};
  }
  void WaitForEvents(unsigned timeout_ms) override {
    @autoreleasepool {
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, timeout_ms / 1000.0, true);
    }
  }
  void Wake() override { CFRunLoopSourceSignal(wake_source_); CFRunLoopWakeUp(loop_); }
 private:
  static CGEventRef Input(CGEventTapProxy, CGEventType type, CGEventRef event, void* context) {
    auto* self = static_cast<MacPrivacyBackend*>(context);
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
      self->tap_failed_ = true;
      CGEventTapEnable(self->tap_, true); // Keep the local recovery chord usable.
      return event;
    }
    if (!event) return event;
    if (CGEventGetIntegerValueField(event, kCGEventSourceUserData) == kCrossDeskInjectedEvent &&
        CGEventGetIntegerValueField(event, kCGEventSourceUnixProcessID) == getpid()) return event;
    const auto flags = CGEventGetFlags(event);
    const auto chord = kCGEventFlagMaskControl | kCGEventFlagMaskAlternate | kCGEventFlagMaskShift;
    if (self->allow_local_unlock_ && type == kCGEventKeyDown && (flags & chord) == chord &&
        CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode) == 53) {
      self->emergency_ = true;
      return nullptr;
    }
    return self->block_input_ ? nullptr : event;
  }
  std::shared_ptr<WindowState> state_;
  CFRunLoopRef loop_ = nullptr;
  CFRunLoopSourceRef wake_source_ = nullptr;
  CFMachPortRef tap_ = nullptr;
  CFRunLoopSourceRef tap_source_ = nullptr;
  bool block_input_ = false;
  bool allow_local_unlock_ = true;
  bool emergency_ = false;
  bool tap_failed_ = false;
};
}  // namespace

void FlushMacPrivacyTasks() {
  if (![NSThread isMainThread]) return;
  std::deque<std::function<void()>> pending;
  {
    std::lock_guard lock(tasks_mutex);
    pending.swap(tasks);
  }
  @autoreleasepool { for (auto& task : pending) task(); }
}
MacPrivacyCaptureState GetMacPrivacyCaptureState() {
  std::lock_guard lock(capture_mutex);
  return capture_state;
}
void AcknowledgeMacPrivacyCapture(uint64_t revision) {
  std::lock_guard lock(capture_mutex);
  if (revision == capture_state.revision) capture_state.applied_revision = revision;
}
std::unique_ptr<PrivacyBackend> CreateMacPrivacyBackend() {
  return std::make_unique<MacPrivacyBackend>();
}
}  // namespace crossdesk
