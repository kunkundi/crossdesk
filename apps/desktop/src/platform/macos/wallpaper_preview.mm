#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>

#include <vector>

#include "wallpaper_preview.h"

namespace crossdesk {

std::filesystem::path LocalWallpaperPath() {
  @autoreleasepool {
    // The first screen is the primary display, independent of focus.
    NSScreen* screen = NSScreen.screens.firstObject;
    if (!screen) return {};
    NSURL* url = [NSWorkspace.sharedWorkspace desktopImageURLForScreen:screen];
    const char* path = url.isFileURL ? url.fileSystemRepresentation : nullptr;
    return path ? std::filesystem::path(path) : std::filesystem::path{};
  }
}

std::vector<unsigned char> LoadMacWallpaper(const std::filesystem::path& path,
                                            int& width, int& height) {
  @autoreleasepool {
    NSURL* url = [NSURL fileURLWithFileSystemRepresentation:path.c_str()
                                               isDirectory:NO
                                              relativeToURL:nil];
    if (!url) return {};
    CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
    if (!source) return {};
    NSDictionary* options = @{
      (id)kCGImageSourceCreateThumbnailFromImageAlways: @YES,
      (id)kCGImageSourceCreateThumbnailWithTransform: @YES,
      (id)kCGImageSourceThumbnailMaxPixelSize: @(kWallpaperPreviewMaxEdge),
    };
    CGImageRef image = CGImageSourceCreateThumbnailAtIndex(
        source, CGImageSourceGetPrimaryImageIndex(source),
        (__bridge CFDictionaryRef)options);
    CFRelease(source);
    if (!image) return {};

    width = static_cast<int>(CGImageGetWidth(image));
    height = static_cast<int>(CGImageGetHeight(image));
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    CGColorSpaceRef colors = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(
        pixels.data(), width, height, 8, width * 4, colors,
        kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colors);
    if (context) {
      CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
      CGContextRelease(context);
    } else {
      pixels.clear();
    }
    CGImageRelease(image);
    return pixels;
  }
}

}  // namespace crossdesk
