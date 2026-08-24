#ifndef CROSSDESK_GUI_PLATFORM_METAL_VIDEO_RENDERER_H_
#define CROSSDESK_GUI_PLATFORM_METAL_VIDEO_RENDERER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace crossdesk {

// macOS video compositor used by CrossDesk only. MiniRTC continues to expose
// tightly packed CPU NV12 frames; this renderer performs the single required
// copy into shared Metal buffers and converts NV12 to RGB in the fragment
// shader. AppKit attachment and rendering are main-thread operations, while
// SubmitNv12() is safe to call from MiniRTC's decode callback thread.
class MacMetalVideoRenderer {
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

  MacMetalVideoRenderer();
  ~MacMetalVideoRenderer();

  MacMetalVideoRenderer(const MacMetalVideoRenderer&) = delete;
  MacMetalVideoRenderer& operator=(const MacMetalVideoRenderer&) = delete;

  bool IsReady() const;

  // The selected stream is the only stream uploaded. Frames for background
  // tabs are intentionally dropped to avoid wasting memory bandwidth.
  bool SetSelectedStream(std::string remote_id);
  void DiscardStream(std::string_view remote_id);

  // Copies one tightly packed NV12 frame into a free shared Metal slot.
  SubmitResult SubmitNv12(std::string_view remote_id, const uint8_t* data,
                          size_t size, int width, int height);

  // Seeds a newly selected stream from its retained CPU snapshot without
  // replacing a newer decoded frame that is already queued or rendering.
  SubmitResult SubmitCachedNv12(std::string_view remote_id,
                                const uint8_t* data, size_t size, int width,
                                int height);

  // Attaches a native CAMetalLayer-backed sibling below Slint's NSView and
  // restores the containing AppKit window's ordinary opaque titlebar.
  // |slint_view| is an NSView* kept opaque so this header remains valid C++.
  bool Attach(void* slint_view);
  void Detach();
  bool IsAttached() const;

  // Cheap main-thread geometry check used by the UI frame timer. New decoded
  // frames are tracked by GuiRuntime's dirty flag; this covers native resize,
  // scale, stream selection, and retry work without a full session sync.
  bool NeedsSurfaceRedraw() const;

  // Draws the newest queued frame for |remote_id|. top_inset_points reserves
  // Slint's tab strip above the native video surface. A rendered outcome
  // carries metadata from the exact slot submitted to the command buffer.
  RenderOutcome RenderLatest(std::string_view remote_id,
                             double top_inset_points,
                             bool native_titlebar_visible);

  // Used only when a session closes, so thumbnail generation does not require
  // a second per-frame CPU copy during normal playback.
  bool CopyLatestNv12(std::string_view remote_id,
                      std::vector<unsigned char>* output, int* width,
                      int* height) const;

 private:
  SubmitResult SubmitNv12Internal(std::string_view remote_id,
                                  const uint8_t* data, size_t size, int width,
                                  int height, bool replace_pending);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crossdesk

#endif  // CROSSDESK_GUI_PLATFORM_METAL_VIDEO_RENDERER_H_
