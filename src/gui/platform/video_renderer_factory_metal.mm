#include "platform/video_renderer.h"

#include <memory>

#include "platform/metal_video_renderer.h"

namespace crossdesk {

std::unique_ptr<VideoRenderer> CreateVideoRenderer() {
  return std::make_unique<MacMetalVideoRenderer>();
}

}  // namespace crossdesk
