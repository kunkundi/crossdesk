#include "runtime/peer_event_handler.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "runtime/gui_runtime.h"

namespace crossdesk {

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
    {
      std::lock_guard<std::mutex> lock(props->video_frame_mutex_);

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
      ++props->video_frame_sequence_;
    }

    props->streaming_ = true;
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
