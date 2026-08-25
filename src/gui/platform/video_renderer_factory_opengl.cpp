#include <memory>

#include "platform/opengl_video_renderer.h"
#include "platform/video_renderer.h"

namespace crossdesk {

std::unique_ptr<VideoRenderer> CreateVideoRenderer() {
  return std::make_unique<OpenGlVideoRenderer>();
}

}  // namespace crossdesk
