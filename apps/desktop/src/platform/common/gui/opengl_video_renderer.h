#ifndef CROSSDESK_GUI_PLATFORM_OPENGL_VIDEO_RENDERER_H_
#define CROSSDESK_GUI_PLATFORM_OPENGL_VIDEO_RENDERER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "platform/video_renderer.h"

namespace crossdesk {

// NV12 underlay for Slint's FemtoVG OpenGL renderer on Windows and Linux.
// MiniRTC submits cross-platform native NV12 descriptors or tightly packed CPU
// fallback frames from its decode callback thread. CUDA frames rotate through
// fenced OpenGL upload resources; CPU descriptors expose their planes without
// an intermediate queue copy. The UI thread always consumes the newest frame.
class OpenGlVideoRenderer final : public VideoRenderer {
public:
  OpenGlVideoRenderer();
  ~OpenGlVideoRenderer() override;

  OpenGlVideoRenderer(const OpenGlVideoRenderer &) = delete;
  OpenGlVideoRenderer &operator=(const OpenGlVideoRenderer &) = delete;

  // These methods must run while Slint's OpenGL context is current, from the
  // RenderingSetup and RenderingTeardown notifier states respectively.
  bool Setup();
  void Teardown();
  bool IsReady() const override;
  bool IsActive() const override;

  // Only the selected stream is queued for upload. Frames for other tabs are
  // retained by GuiRuntime at a throttled rate for thumbnail/tab restoration.
  bool SetSelectedStream(std::string remote_id) override;
  void DiscardStream(std::string_view remote_id) override;

  // Thread-safe; called from MiniRTC's decode callback thread.
  SubmitResult SubmitNv12(std::string_view remote_id, const uint8_t *data,
                          size_t size, int width, int height) override;
  SubmitResult SubmitCachedNv12(std::string_view remote_id, const uint8_t *data,
                                size_t size, int width, int height) override;
  SubmitResult SubmitNativeFrame(std::string_view remote_id,
                                 const XNativeVideoFrame &frame) override;

  // Draws below Slint while its OpenGL context is current. All dimensions are
  // physical pixels in the window client area. corner_radius_pixels clips the
  // underlay to Slint's custom rounded window surface.
  RenderOutcome RenderLatest(std::string_view remote_id, int target_width,
                             int target_height, int top_inset_pixels,
                             int corner_radius_pixels);

  // Used during disconnect cleanup so thumbnail generation does not require a
  // second per-frame CPU copy during normal playback.
  bool CopyLatestNv12(std::string_view remote_id,
                      std::vector<unsigned char> *output, int *width,
                      int *height) const override;

private:
  SubmitResult SubmitNv12Internal(std::string_view remote_id,
                                  const uint8_t *data, size_t size, int width,
                                  int height, bool replace_pending);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace crossdesk

#endif // CROSSDESK_GUI_PLATFORM_OPENGL_VIDEO_RENDERER_H_
