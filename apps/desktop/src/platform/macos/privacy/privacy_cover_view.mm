/* Copyright (c) 2026 by DI JUNKUN, All Rights Reserved. */

#include "privacy_cover_view.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
NSImage* LoadPrivacyLogo() {
  // The macOS installer currently packages the icon as "crossedesk.icns".
  for (NSString* name in @[@"crossdesk", @"crossedesk"]) {
    NSString* path = [NSBundle.mainBundle pathForResource:name ofType:@"icns"];
    if (path) {
      NSImage* image = [[NSImage alloc] initWithContentsOfFile:path];
      if (image.valid) return image;
    }
  }
  // Support unbundled development builds, including launches from outside
  // the repository. No source-tree path is embedded in the executable.
  NSString* resource = @"apps/desktop/resources/macos/crossdesk.icns";
  NSString* directory = NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent;
  NSMutableArray<NSString*>* paths = [NSMutableArray arrayWithObject:
      [NSFileManager.defaultManager.currentDirectoryPath stringByAppendingPathComponent:resource]];
  for (int depth = 0; directory.length && depth <= 4; ++depth) {
    [paths addObject:[directory stringByAppendingPathComponent:resource]];
    directory = directory.stringByDeletingLastPathComponent;
  }
  for (NSString* path in paths) {
    NSImage* image = [[NSImage alloc] initWithContentsOfFile:path.stringByStandardizingPath];
    if (image.valid) return image;
  }
  return nil;
}

NSAttributedString* Text(NSString* value, CGFloat size, NSFontWeight weight, NSColor* color,
                         NSTextAlignment alignment = NSTextAlignmentLeft) {
  NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
  paragraph.lineBreakMode = NSLineBreakByWordWrapping;
  paragraph.alignment = alignment;
  return [[NSAttributedString alloc] initWithString:value attributes:@{
      NSFontAttributeName: [NSFont systemFontOfSize:size weight:weight],
      NSForegroundColorAttributeName: color,
      NSParagraphStyleAttributeName: paragraph}];
}
constexpr NSStringDrawingOptions kTextOptions =
    NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingUsesFontLeading;

CGFloat TextHeight(NSAttributedString* text, CGFloat width) {
  return std::ceil([text boundingRectWithSize:NSMakeSize(width, CGFLOAT_MAX)
                                    options:kTextOptions].size.height);
}

void DrawUnlockInstructions(NSString* value, NSSize size, CGFloat scale,
                            CGFloat brand_top) {
  NSColor* foreground = [NSColor colorWithSRGBRed:229.0 / 255 green:233.0 / 255
                                           blue:240.0 / 255 alpha:1];
  NSColor* muted = [NSColor colorWithSRGBRed:166.0 / 255 green:174.0 / 255
                                      blue:186.0 / 255 alpha:1];
  NSAttributedString* hint = Text(value, 17 * scale, NSFontWeightRegular, muted,
                                   NSTextAlignmentCenter);
  NSAttributedString* plus = Text(@"+", 16 * scale, NSFontWeightRegular, muted);
  const CGFloat text_width = 420 * scale;
  const CGFloat hint_height = TextHeight(hint, text_width);
  const CGFloat key_height = 44 * scale, key_gap = 28 * scale;
  const CGFloat line_gap = 16 * scale;
  NSArray<NSString*>* keys = @[@"Control", @"Option", @"Shift", @"Esc"];
  NSMutableArray<NSAttributedString*>* labels = [NSMutableArray array];
  std::array<CGFloat, 4> widths{};
  CGFloat row_width = key_gap * (keys.count - 1);
  for (NSUInteger i = 0; i < keys.count; ++i) {
    NSAttributedString* label = Text(keys[i], 16 * scale, NSFontWeightSemibold, foreground);
    [labels addObject:label];
    widths[i] = std::max(48 * scale, std::ceil(label.size.width) + 28 * scale);
    row_width += widths[i];
  }
  const CGFloat content_height = hint_height + line_gap + key_height;
  // Center the instructions independently of the footer, leaving room on short displays.
  const CGFloat top = std::max(32 * scale, std::min((size.height - content_height) / 2,
      brand_top - 32 * scale - content_height));
  [hint drawWithRect:NSMakeRect((size.width - text_width) / 2, top, text_width, hint_height)
             options:kTextOptions];
  const CGFloat key_top = top + hint_height + line_gap;
  CGFloat left = (size.width - row_width) / 2;
  for (NSUInteger i = 0; i < keys.count; ++i) {
    NSRect bounds = NSMakeRect(left, key_top, widths[i], key_height);
    NSBezierPath* key = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(bounds, scale / 2, scale / 2)
                                                     xRadius:8 * scale yRadius:8 * scale];
    [[NSColor colorWithSRGBRed:16.0 / 255 green:19.0 / 255 blue:24.0 / 255 alpha:1] setFill];
    [key fill];
    [[NSColor colorWithSRGBRed:52.0 / 255 green:58.0 / 255 blue:68.0 / 255 alpha:1] setStroke];
    key.lineWidth = scale;
    [key stroke];
    NSAttributedString* label = labels[i];
    [label drawAtPoint:NSMakePoint(left + (widths[i] - label.size.width) / 2,
                                  key_top + (key_height - label.size.height) / 2)];
    left += widths[i];
    if (i + 1 < keys.count) {
      [plus drawAtPoint:NSMakePoint(left + (key_gap - plus.size.width) / 2,
                                   key_top + (key_height - plus.size.height) / 2)];
      left += key_gap;
    }
  }
}

}

@interface CrossDeskPrivacyCoverView : NSView
@property(nonatomic, strong) NSImage* logo;
@property(nonatomic, copy) NSString* unlockHint;
@property(nonatomic) BOOL allowLocalUnlock;
@property(nonatomic) BOOL blackScreen;
@end

@implementation CrossDeskPrivacyCoverView
- (BOOL)isOpaque { return YES; }
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
  [NSColor.blackColor setFill];
  NSRectFill(self.bounds);
  if (self.blackScreen) return;
  const CGFloat width = NSWidth(self.bounds), height = NSHeight(self.bounds);
  if (width <= 0 || height <= 0) return;
  // Match the Windows cover's sizes, colors and bottom-left alignment.
  // AppKit coordinates are points; Retina backing scale is applied by AppKit.
  const CGFloat scale = std::min({CGFloat(1), width / 960, height / 280});
  const CGFloat margin = 32 * scale, brand_gap = 8 * scale;
  NSAttributedString* brand = Text(@" CrossDesk 远程控制中", 29 * scale,
                                   NSFontWeightSemibold, NSColor.whiteColor);
  const CGFloat logo_size = 60 * scale;
  const CGFloat brand_text_width = std::ceil(brand.size.width);
  const CGFloat brand_text_height = std::ceil(brand.size.height);
  // Keep the corner branding fixed when local unlocking is toggled.
  const CGFloat brand_bottom = height - margin;
  const CGFloat logo_top = brand_bottom - logo_size;
  [self.logo drawInRect:NSMakeRect(margin, logo_top, logo_size, logo_size)
              fromRect:NSZeroRect operation:NSCompositingOperationSourceOver
              fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
  const CGFloat text_left = margin + logo_size + brand_gap;
  [brand drawWithRect:NSMakeRect(text_left, brand_bottom - brand_text_height,
                                brand_text_width, brand_text_height)
             options:kTextOptions];
  if (!self.allowLocalUnlock) return;
  DrawUnlockInstructions(self.unlockHint, self.bounds.size, scale,
                         brand_bottom - std::max(logo_size, brand_text_height));
}
@end

namespace crossdesk {
bool UpdateMacPrivacyCoverView(NSView* view, NSString* hint, bool allow_local_unlock,
                               bool black_screen) {
  auto* cover = static_cast<CrossDeskPrivacyCoverView*>(view);
  if (!black_screen && !cover.logo) {
    cover.logo = LoadPrivacyLogo();
    if (!cover.logo) return false;
  }
  cover.unlockHint = hint ?: @"To turn off the privacy screen, press";
  cover.allowLocalUnlock = allow_local_unlock;
  cover.blackScreen = black_screen;
  cover.needsDisplay = YES;
  return true;
}
NSView* CreateMacPrivacyCoverView(NSRect frame, NSString* unlock_hint, bool allow_local_unlock,
                                  bool black_screen) {
  auto* view = [[CrossDeskPrivacyCoverView alloc] initWithFrame:frame];
  if (!UpdateMacPrivacyCoverView(view, unlock_hint, allow_local_unlock, black_screen)) return nil;
  view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  return view;
}
}
