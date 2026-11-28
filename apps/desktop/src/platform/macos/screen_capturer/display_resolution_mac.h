/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DISPLAY_RESOLUTION_MAC_H_
#define _DISPLAY_RESOLUTION_MAC_H_

#include <ColorSync/ColorSync.h>
#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/graphics/IOGraphicsTypes.h>

#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <tuple>

#include "display_info.h"
#include <display_resolution.h>

namespace crossdesk {

inline DisplayResolution MacDisplayResolution(CGDisplayModeRef mode) {
  if (!mode)
    return {};
  // Desktop dimensions shown in the resolution picker.
  const auto width = CGDisplayModeGetWidth(mode);
  const auto height = CGDisplayModeGetHeight(mode);
  if (width > 32768 || height > 32768)
    return {};
  return {int(width), int(height)};
}

inline std::optional<DisplayResolution> MacDisplayPixelSize(CGDisplayModeRef mode) {
  if (!mode) return std::nullopt;
  const auto width = CGDisplayModeGetPixelWidth(mode);
  const auto height = CGDisplayModeGetPixelHeight(mode);
  if (!width || !height || width > 32768 || height > 32768) return std::nullopt;
  return DisplayResolution{int(width), int(height)};
}

inline bool ResolveMacResolutionDisplay(const DisplayInfo &display,
                                        CGDirectDisplayID &id,
                                        std::string &key) {
  id = static_cast<CGDirectDisplayID>(
      reinterpret_cast<uintptr_t>(display.handle));
  if (!id || !CGDisplayIsOnline(id) || !CGDisplayIsActive(id))
    return false;
  CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(id);
  if (!uuid)
    return false;
  CFStringRef uuid_string = CFUUIDCreateString(nullptr, uuid);
  CFRelease(uuid);
  if (!uuid_string)
    return false;
  char value[128]{};
  const bool valid = CFStringGetCString(uuid_string, value, sizeof(value),
                                        kCFStringEncodingUTF8);
  CFRelease(uuid_string);
  if (!valid)
    return false;
  key = std::to_string(id) + "|" + value;
  return true;
}

// Choices() borrows its mode references from this object.
class MacResolutionModes {
public:
  explicit MacResolutionModes(CGDirectDisplayID display)
      : current(CGDisplayCopyDisplayMode(display)) {
    // The default enumeration can omit the active Retina scaling modes.
    const void *key = kCGDisplayShowDuplicateLowResolutionModes;
    const void *value = kCFBooleanTrue;
    CFDictionaryRef options = CFDictionaryCreate(
        nullptr, &key, &value, 1, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    modes_ = CGDisplayCopyAllDisplayModes(display, options);
    if (options)
      CFRelease(options);
  }
  ~MacResolutionModes() {
    if (modes_)
      CFRelease(modes_);
    if (current)
      CGDisplayModeRelease(current);
  }
  MacResolutionModes(const MacResolutionModes &) = delete;
  MacResolutionModes &operator=(const MacResolutionModes &) = delete;

  std::vector<CGDisplayModeRef> Choices(
      std::optional<DisplayResolution> pixel_size = std::nullopt) const {
    std::vector<CGDisplayModeRef> result;
    const auto current_size = MacDisplayResolution(current);
    if (!modes_ || !ValidDisplayResolution(current_size))
      return result;
    const double scale = double(CGDisplayModeGetPixelWidth(current)) /
                         CGDisplayModeGetWidth(current);
    const double refresh = CGDisplayModeGetRefreshRate(current);
    const auto rank = [&](CGDisplayModeRef mode) {
      return std::make_tuple(
          std::abs(double(CGDisplayModeGetPixelWidth(mode)) /
                       CGDisplayModeGetWidth(mode) -
                   scale),
          std::abs(CGDisplayModeGetRefreshRate(mode) - refresh),
          CGDisplayModeGetIODisplayModeID(mode) !=
              CGDisplayModeGetIODisplayModeID(current));
    };
    std::map<std::pair<int, int>, CGDisplayModeRef> unique;
    const auto current_key = std::make_pair(current_size.width, current_size.height);
    const bool include_current = !pixel_size || MacDisplayPixelSize(current) == pixel_size;
    if (include_current) unique.emplace(current_key, current);
    for (CFIndex index = 0; index < CFArrayGetCount(modes_); ++index) {
      auto mode = static_cast<CGDisplayModeRef>(
          const_cast<void *>(CFArrayGetValueAtIndex(modes_, index)));
      const auto size = MacDisplayResolution(mode);
      const auto flags = CGDisplayModeGetIOFlags(mode);
      if ((pixel_size && !(MacDisplayPixelSize(mode) == pixel_size)) ||
          !ValidDisplayResolution(size) ||
          !CGDisplayModeIsUsableForDesktopGUI(mode) ||
          (flags & (kDisplayModeInterlacedFlag | kDisplayModeStretchedFlag)))
        continue;
      const auto key = std::make_pair(size.width, size.height);
      auto found = unique.find(key);
      // Prefer the current Retina density and then the closest refresh rate.
      if (found == unique.end() || rank(mode) < rank(found->second))
        unique[key] = mode;
    }
    for (auto it = unique.rbegin(); it != unique.rend(); ++it) {
      result.push_back(it->second);
      if (result.size() == kMaxDisplayResolutionModes) {
        // Keep the active mode selectable even when the wire limit truncates the list.
        if (include_current && it->first > current_key)
          result.back() = current;
        break;
      }
    }
    return result;
  }

  CGDisplayModeRef current;

private:
  CFArrayRef modes_;
};

inline std::vector<DisplayResolutionInfo>
QueryMacDisplayResolutions(const std::vector<DisplayInfo> &displays) {
  std::vector<DisplayResolutionInfo> result;
  for (int index = 0;
       index < int(displays.size()) && index < kMaxResolutionDisplays;
       ++index) {
    DisplayResolutionInfo info;
    info.display_id = index;
    CGDirectDisplayID id = 0;
    if (!ResolveMacResolutionDisplay(displays[index], id, info.display_key))
      continue;
    MacResolutionModes modes(id);
    info.current = MacDisplayResolution(modes.current);
    info.pixel_size = MacDisplayPixelSize(modes.current);
    if (!ValidDisplayResolution(info.current))
      continue;
    for (auto mode : modes.Choices())
      info.modes.push_back(MacDisplayResolution(mode));
    result.push_back(std::move(info));
  }
  return result;
}

inline bool ApplyMacDisplayResolution(const std::vector<DisplayInfo> &displays,
                                      const DisplayResolutionChange &change,
                                      std::optional<DisplayResolution> pixel_size = std::nullopt) {
  if (change.display_id < 0 || change.display_id >= int(displays.size()) ||
      !ValidDisplayResolution(change.resolution))
    return false;
  CGDirectDisplayID id = 0;
  std::string key;
  if (!ResolveMacResolutionDisplay(displays[change.display_id], id, key) ||
      key != change.display_key)
    return false;
  MacResolutionModes modes(id);
  const auto matches = [&](CGDisplayModeRef mode) {
    return MacDisplayResolution(mode) == change.resolution &&
           (!pixel_size || MacDisplayPixelSize(mode) == pixel_size);
  };
  if (matches(modes.current)) return true;
  for (auto mode : modes.Choices(pixel_size)) {
    if (!(MacDisplayResolution(mode) == change.resolution))
      continue;
    // A synchronous, application-lifetime change; never persist remote choices
    // in the user's permanent display configuration.
    if (CGDisplaySetDisplayMode(id, mode, nullptr) != kCGErrorSuccess)
      return false;
    CGDisplayModeRef applied = CGDisplayCopyDisplayMode(id);
    const bool accepted = matches(applied);
    if (applied)
      CGDisplayModeRelease(applied);
    if (!accepted)
      CGDisplaySetDisplayMode(id, modes.current, nullptr);
    return accepted;
  }
  return false;
}

inline bool SameMacResolutionMode(CGDisplayModeRef mode,
                                  CGDisplayModeRef original) {
  return mode && original &&
         MacDisplayResolution(mode) == MacDisplayResolution(original) &&
         CGDisplayModeGetPixelWidth(mode) ==
             CGDisplayModeGetPixelWidth(original) &&
         CGDisplayModeGetPixelHeight(mode) ==
             CGDisplayModeGetPixelHeight(original) &&
         CGDisplayModeGetRefreshRate(mode) ==
             CGDisplayModeGetRefreshRate(original);
}

inline std::function<bool()>
CaptureMacDisplayResolution(const std::vector<DisplayInfo> &displays,
                            const DisplayResolutionChange &change) {
  if (change.display_id < 0 || change.display_id >= int(displays.size()))
    return {};
  const auto display = displays[change.display_id];
  CGDirectDisplayID id = 0;
  std::string key;
  if (!ResolveMacResolutionDisplay(display, id, key) ||
      key != change.display_key)
    return {};
  // Retain the exact mode, including Retina pixel density and refresh rate.
  std::shared_ptr<CGDisplayMode> original(CGDisplayCopyDisplayMode(id),
                                          [](CGDisplayModeRef mode) {
                                            if (mode)
                                              CGDisplayModeRelease(mode);
                                          });
  if (!original)
    return {};
  return [display, key, original] {
    CGDirectDisplayID id = 0;
    std::string current_key;
    if (!ResolveMacResolutionDisplay(display, id, current_key) ||
        current_key != key)
      return false;
    CGDisplayModeRef current = CGDisplayCopyDisplayMode(id);
    const bool unchanged = SameMacResolutionMode(current, original.get());
    if (current)
      CGDisplayModeRelease(current);
    if (unchanged)
      return true;
    if (CGDisplaySetDisplayMode(id, original.get(), nullptr) != kCGErrorSuccess)
      return false;
    current = CGDisplayCopyDisplayMode(id);
    const bool restored = SameMacResolutionMode(current, original.get());
    if (current)
      CGDisplayModeRelease(current);
    return restored;
  };
}

} // namespace crossdesk

#endif
