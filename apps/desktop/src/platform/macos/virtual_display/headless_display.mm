// Display creation adapted from Snug 2.8.0 (MIT), VirtualDisplayService.swift.
// The private CoreGraphics declarations originate from FreeDisplay via Snug.
// See apps/desktop/resources/macos/Snug-LICENSE.txt.
#import <AppKit/AppKit.h>

#include "headless_display.h"
#include "rd_log.h"

#include <vector>

// Look up the classes at runtime so a macOS change cannot prevent app launch.
@interface CDVirtualDisplayDescriptor : NSObject
@property(copy) NSString* name;
@property CGSize sizeInMillimeters;
@property uint32_t maxPixelsWide, maxPixelsHigh, vendorID, productID, serialNum;
@end

@interface CDVirtualDisplayMode : NSObject
- (instancetype)initWithWidth:(NSUInteger)width height:(NSUInteger)height
                  refreshRate:(double)rate;
@end

@interface CDVirtualDisplaySettings : NSObject
@property BOOL hiDPI;
@property(copy) NSArray* modes;
@end

@interface CDVirtualDisplay : NSObject
- (instancetype)initWithDescriptor:(CDVirtualDisplayDescriptor*)descriptor;
- (BOOL)applySettings:(CDVirtualDisplaySettings*)settings;
@property(readonly) CGDirectDisplayID displayID;
@end

namespace crossdesk {
namespace {

constexpr char kDisplayName[] = "CrossDesk Virtual Display";
constexpr uint32_t kVendor = 0x4352;
constexpr uint32_t kProduct = 1;
constexpr uint32_t kMaxDisplayWidth = 3840;

CDVirtualDisplay* CreateDisplay() {
  Class descriptor_class = NSClassFromString(@"CGVirtualDisplayDescriptor");
  Class mode_class = NSClassFromString(@"CGVirtualDisplayMode");
  Class settings_class = NSClassFromString(@"CGVirtualDisplaySettings");
  Class display_class = NSClassFromString(@"CGVirtualDisplay");
  if (!descriptor_class || !mode_class || !settings_class || !display_class)
    return nil;

  @try {
    CDVirtualDisplayDescriptor* descriptor = [[descriptor_class alloc] init];
    descriptor.name = @(kDisplayName);
    descriptor.maxPixelsWide = kMaxDisplayWidth;
    descriptor.maxPixelsHigh = kMaxDisplayWidth * 9 / 16;
    descriptor.sizeInMillimeters = CGSizeMake(509, 286);
    // Stable identity, distinct from standalone Snug screens.
    descriptor.vendorID = kVendor;
    descriptor.productID = kProduct;
    descriptor.serialNum = 1;

    CDVirtualDisplay* screen = [(CDVirtualDisplay*)[display_class alloc]
        initWithDescriptor:descriptor];
    if (!screen) return nil;
    CDVirtualDisplaySettings* settings = [[settings_class alloc] init];
    settings.hiDPI = NO;
    NSMutableArray* modes = [NSMutableArray array];
    // Keep 1080p first as the preferred mode; higher modes are opt-in.
    for (NSUInteger width : {1920u, kMaxDisplayWidth, 2560u, 1600u, 1280u}) {
      CDVirtualDisplayMode* mode = [(CDVirtualDisplayMode*)[mode_class alloc]
          initWithWidth:width height:width * 9 / 16 refreshRate:60];
      if (!mode) return nil;
      [modes addObject:mode];
    }
    settings.modes = modes;
    return [screen applySettings:settings] ? screen : nil;
  } @catch (NSException* exception) {
    LOG_WARN("macOS virtual display API failed: {}", exception.name.UTF8String);
    return nil;
  }
}

// A monitorless Mac can report a 1080p synthetic output instead of an empty
// list. Leave all other outputs (including sleeping panels and Snug) alone.
bool IsSystemPlaceholder(CGDirectDisplayID id) {
  return !CGDisplayIsBuiltin(id) && CGDisplayVendorNumber(id) == 0x756e6b6e &&
         CGDisplayModelNumber(id) == 0x76697274;  // 'unkn' / 'virt'
}

}  // namespace

MacHeadlessDisplay::~MacHeadlessDisplay() { Release(); }

const char* MacHeadlessDisplay::NameFor(CGDirectDisplayID id) {
  return CGDisplayVendorNumber(id) == kVendor && CGDisplayModelNumber(id) == kProduct &&
         CGDisplaySerialNumber(id) == 1 ? kDisplayName : nullptr;
}

void MacHeadlessDisplay::Release() {
  if (display_) {
    // Snug waits at least 2.5 s before retiring a newly created screen. A
    // rapid settings toggle must not tear it down mid-registration in WindowServer.
    CFTypeRef retiring = display_;
    display_ = nullptr;
    const auto delay = std::chrono::duration_cast<std::chrono::nanoseconds>(
        release_after_ - std::chrono::steady_clock::now());
    if (delay.count() > 0) {
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay.count()),
                     dispatch_get_main_queue(), ^{ CFRelease(retiring); });
    } else {
      CFRelease(retiring);
    }
    LOG_INFO("Retiring macOS headless virtual display");
  }
  next_check_ = {};
}

void MacHeadlessDisplay::Update(bool enabled, bool capturing) {
  if (!enabled) {
    if (!capturing) Release();
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now < next_check_) return;
  next_check_ = now + std::chrono::seconds(2);

  uint32_t count = 0;
  if (CGGetOnlineDisplayList(0, nullptr, &count) != kCGErrorSuccess) return;
  std::vector<CGDirectDisplayID> ids(count);
  if (count && CGGetOnlineDisplayList(count, ids.data(), &count) != kCGErrorSuccess) return;
  ids.resize(count);
  CDVirtualDisplay* owned = (__bridge CDVirtualDisplay*)display_;
  const CGDirectDisplayID owned_id = owned ? owned.displayID : kCGNullDirectDisplay;
  bool owned_online = false;
  for (auto id : ids) {
    if (id == owned_id) {
      owned_online = true;
    } else if (!IsSystemPlaceholder(id)) {
      // Retain the negotiated stream until the final controller disconnects.
      if (owned && !capturing) Release();
      return;
    }
  }
  if (owned_online) return;
  if (owned) Release();
  @autoreleasepool {
    CDVirtualDisplay* screen = CreateDisplay();
    if (screen) {
      display_ = CFBridgingRetain(screen);
      release_after_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
      LOG_INFO("Created macOS headless virtual display: id={}", screen.displayID);
    } else {
      LOG_WARN("Could not create macOS headless virtual display; keeping system output");
    }
  }
  // Allow WindowServer to publish the new screen; rate-limit failed attempts.
  next_check_ = now + std::chrono::seconds(8);
}

}  // namespace crossdesk
