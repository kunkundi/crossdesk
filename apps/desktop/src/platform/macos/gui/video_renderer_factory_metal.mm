#include "platform/video_renderer.h"

#include <memory>
#import <AppKit/AppKit.h>

#include "platform/macos/gui/metal_video_renderer.h"

namespace crossdesk {

bool HasHdrVideoDisplay() {
  for (NSScreen* screen in NSScreen.screens)
    if (screen.maximumPotentialExtendedDynamicRangeColorComponentValue > 1) return true;
  return false;
}

std::unique_ptr<VideoRenderer> CreateVideoRenderer() {
  return std::make_unique<MacMetalVideoRenderer>();
}

}  // namespace crossdesk
