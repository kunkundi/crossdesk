#ifndef CROSSDESK_GUI_PLATFORM_VIDEO_RENDERER_H_
#define CROSSDESK_GUI_PLATFORM_VIDEO_RENDERER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "minirtc.h"

namespace crossdesk {

// Platform-independent contract for the native stream video renderer.
//
// Frame submission and snapshot methods may be called from MiniRTC callback
// threads. Surface setup, rendering and teardown remain backend-specific and
// are driven by the Slint presentation module on the UI thread.
class VideoRenderer {
public:
  enum class SubmitResult {
    submitted,
    not_selected,
    dropped,
    failed,
  };

  enum class RenderResult {
    rendered,
    idle,
    empty,
    failed,
  };

  struct RenderOutcome {
    RenderResult result = RenderResult::failed;
    int width = 0;
    int height = 0;
    uint64_t sequence = 0;
  };

  virtual ~VideoRenderer() = default;

  virtual bool IsReady() const = 0;

  // True only while the renderer can accept and present native frames. Metal
  // additionally requires attachment to an AppKit view; OpenGL requires an
  // active Slint rendering context.
  virtual bool IsActive() const = 0;

  virtual bool SetSelectedStream(std::string remote_id) = 0;
  virtual void DiscardStream(std::string_view remote_id) = 0;

  virtual SubmitResult SubmitNv12(std::string_view remote_id,
                                  const uint8_t* data, size_t size, int width,
                                  int height) = 0;
  virtual SubmitResult SubmitCachedNv12(std::string_view remote_id,
                                        const uint8_t* data, size_t size,
                                        int width, int height) = 0;
  // Retains a platform-native decoded frame when the backend supports its
  // storage and handle types. Unsupported backends keep the default fallback.
  virtual SubmitResult SubmitNativeFrame(std::string_view,
                                         const MiniRtcNativeVideoFrame&) {
    return SubmitResult::failed;
  }

  virtual bool CopyLatestNv12(std::string_view remote_id,
                              std::vector<unsigned char>* output, int* width,
                              int* height) const = 0;
};

// Exactly one factory implementation is linked for the target platform.
std::unique_ptr<VideoRenderer> CreateVideoRenderer();

}  // namespace crossdesk

#endif  // CROSSDESK_GUI_PLATFORM_VIDEO_RENDERER_H_
