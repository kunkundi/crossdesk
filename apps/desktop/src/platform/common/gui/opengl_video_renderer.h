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
// MiniRTC submits tightly packed CPU NV12 frames from its decode callback
// thread. The Slint UI thread uploads the newest frame as Y and UV textures and
// performs color conversion and scaling in a fragment shader before Slint
// draws its overlay.
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
