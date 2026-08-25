#ifndef CROSSDESK_GUI_RENDERING_SLINT_VIDEO_PRESENTER_H_
#define CROSSDESK_GUI_RENDERING_SLINT_VIDEO_PRESENTER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "crossdesk_ui.h"

namespace crossdesk {

class VideoRenderer;

// Owns the bridge between decoded NV12 frames, the selected native graphics
// backend and Slint's StreamWindow. Platform macros and graphics-API lifecycle
// details are intentionally contained in the implementation file.
class SlintVideoPresenter {
public:
  struct SurfaceState {
    std::string selected_stream;
    bool fullscreen = false;
    size_t tab_count = 0;
  };

  struct Frame {
    std::string remote_id;
    std::shared_ptr<std::vector<unsigned char>> nv12;
    int width = 0;
    int height = 0;
    uint64_t sequence = 0;
  };

  struct PresentResult {
    int width = 0;
    int height = 0;
    bool new_frame = false;
  };

  using StateProvider = std::function<SurfaceState()>;
  using DirtyCallback = std::function<void()>;

  explicit SlintVideoPresenter(VideoRenderer& renderer);
  ~SlintVideoPresenter();

  SlintVideoPresenter(const SlintVideoPresenter&) = delete;
  SlintVideoPresenter& operator=(const SlintVideoPresenter&) = delete;

  // Called before the window is shown. OpenGL installs Slint's rendering
  // notifier here; macOS deliberately defers native view attachment until a
  // later UI pass, after AppKit has committed the ordinary window style.
  void PrepareWindow(slint::ComponentHandle<ui::StreamWindow>& stream,
                     StateProvider state_provider,
                     DirtyCallback dirty_callback);

  // Returns true only when a deferred native surface became attached during
  // this call, allowing the caller to seed it from a cached frame once.
  bool EnsureAttached();
  bool NeedsRedraw() const;

  bool SelectStream(std::string remote_id);
  PresentResult Present(const Frame& frame);
  void ForgetStream(const std::string& remote_id);
  void Detach();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crossdesk

#endif  // CROSSDESK_GUI_RENDERING_SLINT_VIDEO_PRESENTER_H_
