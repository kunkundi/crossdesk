#include "runtime/peer_event_handler.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "device_controller.h"
#include "file_transfer.h"
#include "localization.h"
#include "platform.h"
#include "rd_log.h"
#include "runtime/gui_runtime.h"
#include "runtime/remote_action_codec.h"

#if _WIN32
#include "interactive_state.h"
#include "service_host.h"
#endif

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

      if (!props->back_frame_) {
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
    }

    SDL_Event event;
    event.type = runtime->STREAM_REFRESH_EVENT;
    event.user.data1 = props;
    SDL_PushEvent(&event);
    props->streaming_ = true;

    if (props->net_traffic_stats_button_pressed_) {
      props->frame_count_++;
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - props->last_time_)
                         .count();

      if (elapsed >= 1000) {
        props->fps_ = props->frame_count_ * 1000 / elapsed;
        props->frame_count_ = 0;
        props->last_time_ = now;
      }
    }
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
