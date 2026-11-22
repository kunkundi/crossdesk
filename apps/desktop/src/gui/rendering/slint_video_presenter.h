/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SLINT_VIDEO_PRESENTER_H_
#define _SLINT_VIDEO_PRESENTER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rendering/video_latency.h"

namespace slint {
template <typename T> class ComponentHandle;
}

namespace crossdesk::ui {
class StreamWindow;
}

namespace crossdesk {

class VideoRenderer;

// Owns the bridge between decoded NV12 frames, the selected native graphics
// backend and Slint's StreamWindow. Platform macros and graphics-API lifecycle
// details are intentionally contained in the implementation file.
class SlintVideoPresenter {
public:
  struct Frame {
    std::shared_ptr<std::vector<unsigned char>> nv12;
    int width = 0;
    int height = 0;
    uint64_t sequence = 0;
    VideoLatencyFrame timing;
  };

  struct PresentResult {
    int width = 0;
    int height = 0;
    bool new_frame = false;
  };

  using DirtyCallback = std::function<void()>;

  SlintVideoPresenter(std::shared_ptr<VideoRenderer> renderer, std::string remote_id);
  ~SlintVideoPresenter();

  SlintVideoPresenter(const SlintVideoPresenter&) = delete;
  SlintVideoPresenter& operator=(const SlintVideoPresenter&) = delete;

  // Called before the window is shown. OpenGL installs Slint's rendering
  // notifier here; macOS deliberately defers native view attachment until a
  // later UI pass, after AppKit has committed the ordinary window style.
  void PrepareWindow(slint::ComponentHandle<ui::StreamWindow>& stream,
                     DirtyCallback dirty_callback);

  // Returns true only when a deferred native surface became attached during
  // this call, allowing the caller to seed it from a cached frame once.
  bool EnsureAttached();
  bool NeedsRedraw() const;

  PresentResult Present(const Frame& frame);
  void Detach();

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace crossdesk

#endif
