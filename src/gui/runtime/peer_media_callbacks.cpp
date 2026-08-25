#include "runtime/peer_event_handler.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "runtime/gui_runtime.h"
#if defined(__APPLE__) || defined(_WIN32) || defined(__linux__)
#include <chrono>

#if defined(__APPLE__)
#include "platform/metal_video_renderer.h"
#else
#include "platform/opengl_video_renderer.h"
#endif
#endif

namespace crossdesk {
#if defined(__APPLE__) || defined(_WIN32) || defined(__linux__)
namespace {

constexpr auto kBackgroundSnapshotInterval = std::chrono::seconds(1);

}  // namespace
#endif

void PeerEventHandler::OnReceiveVideoBuffer(
    const XVideoFrame *video_frame, const char *user_id, size_t user_id_size,
    const char *src_id, size_t src_id_size, void *user_data) {
  auto *handler = static_cast<PeerEventHandler *>(user_data);
  GuiRuntime *runtime = handler ? &handler->owner_ : nullptr;
  if (!runtime) {
    return;
  }

  std::string remote_id(user_id, user_id_size);
  // std::shared_lock lock(runtime->remote_sessions_mutex_);
  if (runtime->remote_sessions_.find(remote_id) ==
      runtime->remote_sessions_.end()) {
    return;
  }
  GuiRuntime::RemoteSession *props =
      runtime->remote_sessions_.find(remote_id)->second.get();

  if (props->connection_established_) {
#if defined(__APPLE__) || defined(_WIN32) || defined(__linux__)
    bool background_snapshot_only = false;
#if defined(__APPLE__)
    auto* native_renderer = runtime->mac_metal_video_renderer_.get();
    using NativeVideoRenderer = MacMetalVideoRenderer;
#else
    auto* native_renderer = runtime->opengl_video_renderer_.get();
    using NativeVideoRenderer = OpenGlVideoRenderer;
#endif
    if (native_renderer && native_renderer->IsReady()
#if defined(__APPLE__)
        && native_renderer->IsAttached()
#endif
    ) {
      const auto submit_result = native_renderer->SubmitNv12(
          remote_id, reinterpret_cast<const uint8_t*>(video_frame->data),
          video_frame->size, video_frame->width, video_frame->height);
      if (submit_result == NativeVideoRenderer::SubmitResult::submitted) {
        std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
        const bool size_changed =
            (props->video_width_ != video_frame->width) ||
            (props->video_height_ != video_frame->height);
        if (size_changed) {
          props->render_rect_dirty_ = true;
        }
        props->video_width_ = video_frame->width;
        props->video_height_ = video_frame->height;
        props->video_size_ = video_frame->size;
        // Once the native renderer owns the current stream, do not leave an
        // older CPU frame ahead of the renderer's close snapshot.
        props->front_frame_.reset();
        props->back_frame_.reset();
        props->thumbnail_frame_.reset();
        props->thumbnail_width_ = 0;
        props->thumbnail_height_ = 0;
        props->background_snapshot_time_ = {};
        ++props->video_frame_sequence_;
        props->streaming_ = true;
        runtime->video_frame_dirty_.store(true, std::memory_order_release);
        return;
      }
      if (submit_result == NativeVideoRenderer::SubmitResult::dropped ||
          submit_result == NativeVideoRenderer::SubmitResult::failed) {
        // Keep presenting the last native frame. A later decoded frame can
        // reuse the renderer without changing ownership mid-window.
        props->streaming_ = true;
        return;
      }
      if (submit_result ==
          NativeVideoRenderer::SubmitResult::not_selected) {
        background_snapshot_only = true;
      }
    }
#endif
    {
      std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
#if defined(__APPLE__) || defined(_WIN32) || defined(__linux__)
      const auto now = std::chrono::steady_clock::now();
      if (background_snapshot_only && props->thumbnail_frame_ &&
          !props->thumbnail_frame_->empty() &&
          props->background_snapshot_time_ !=
              std::chrono::steady_clock::time_point{} &&
          now - props->background_snapshot_time_ <
              kBackgroundSnapshotInterval) {
        props->streaming_ = true;
        return;
      }
#endif

      // Allocate a third buffer only while the UI still owns the old snapshot.
      if (!props->back_frame_ || props->back_frame_.use_count() != 1) {
        props->back_frame_ =
            std::make_shared<std::vector<unsigned char>>(video_frame->size);
      }
      if (props->back_frame_->size() != video_frame->size) {
        props->back_frame_->resize(video_frame->size);
      }

      std::memcpy(props->back_frame_->data(), video_frame->data,
                  video_frame->size);

      const bool size_changed = (props->video_width_ != video_frame->width) ||
                                (props->video_height_ != video_frame->height);
      if (size_changed) {
        props->render_rect_dirty_ = true;
      }

      props->video_width_ = video_frame->width;
      props->video_height_ = video_frame->height;
      props->video_size_ = video_frame->size;

      props->front_frame_.swap(props->back_frame_);
#if defined(__APPLE__) || defined(_WIN32) || defined(__linux__)
      props->thumbnail_frame_ = props->front_frame_;
      props->thumbnail_width_ = video_frame->width;
      props->thumbnail_height_ = video_frame->height;
      if (background_snapshot_only) {
        props->background_snapshot_time_ = now;
      }
#endif
      ++props->video_frame_sequence_;
    }

    props->streaming_ = true;
#if defined(__APPLE__) || defined(_WIN32) || defined(__linux__)
    if (background_snapshot_only) {
      return;
    }
#endif
    runtime->video_frame_dirty_.store(true, std::memory_order_release);
  }
}

void PeerEventHandler::OnReceiveAudioBuffer(
    const char *data, size_t size, const char *user_id, size_t user_id_size,
    const char *src_id, size_t src_id_size, void *user_data) {
  auto *handler = static_cast<PeerEventHandler *>(user_data);
  GuiRuntime *runtime = handler ? &handler->owner_ : nullptr;
  if (!runtime) {
    return;
  }

  runtime->audio_buffer_fresh_ = true;

  runtime->devices_.PushAudio(data, size);
}

} // namespace crossdesk
