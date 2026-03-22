#include "screen_capturer_wayland.h"

#include "screen_capturer_wayland_build.h"

#if CROSSDESK_WAYLAND_BUILD_ENABLED

#include <cstdint>
#include <thread>
#include <unistd.h>

#include <vector>

#include "libyuv.h"
#include "rd_log.h"

namespace crossdesk {

namespace {

const char* PipeWireFormatName(uint32_t spa_format) {
  switch (spa_format) {
    case SPA_VIDEO_FORMAT_BGRx:
      return "BGRx";
    case SPA_VIDEO_FORMAT_BGRA:
      return "BGRA";
    default:
      return "unsupported";
  }
}

}  // namespace

bool ScreenCapturerWayland::SetupPipeWireStream() {
  if (pipewire_fd_ < 0 || pipewire_node_id_ == 0) {
    return false;
  }

  if (!pipewire_initialized_) {
    pw_init(nullptr, nullptr);
    pipewire_initialized_ = true;
  }

  pw_thread_loop_ = pw_thread_loop_new("crossdesk-wayland-capture", nullptr);
  if (!pw_thread_loop_) {
    LOG_ERROR("Failed to create PipeWire thread loop");
    return false;
  }

  if (pw_thread_loop_start(pw_thread_loop_) < 0) {
    LOG_ERROR("Failed to start PipeWire thread loop");
    CleanupPipeWire();
    return false;
  }
  pipewire_thread_loop_started_ = true;

  pw_thread_loop_lock(pw_thread_loop_);

  pw_context_ =
      pw_context_new(pw_thread_loop_get_loop(pw_thread_loop_), nullptr, 0);
  if (!pw_context_) {
    LOG_ERROR("Failed to create PipeWire context");
    pw_thread_loop_unlock(pw_thread_loop_);
    CleanupPipeWire();
    return false;
  }

  pw_core_ = pw_context_connect_fd(pw_context_, pipewire_fd_, nullptr, 0);
  if (!pw_core_) {
    LOG_ERROR("Failed to connect to PipeWire remote");
    pw_thread_loop_unlock(pw_thread_loop_);
    CleanupPipeWire();
    return false;
  }
  pipewire_fd_ = -1;

  pw_stream_ = pw_stream_new(
      pw_core_, "CrossDesk Wayland Capture",
      pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY,
                        "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr));
  if (!pw_stream_) {
    LOG_ERROR("Failed to create PipeWire stream");
    pw_thread_loop_unlock(pw_thread_loop_);
    CleanupPipeWire();
    return false;
  }

  auto* listener = new spa_hook();
  stream_listener_ = listener;

  static const pw_stream_events stream_events = [] {
    pw_stream_events events{};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.state_changed =
        [](void* userdata, enum pw_stream_state old_state,
           enum pw_stream_state state, const char* error_message) {
          auto* self = static_cast<ScreenCapturerWayland*>(userdata);
          if (!self) {
            return;
          }

          if (state == PW_STREAM_STATE_ERROR) {
            LOG_ERROR("PipeWire stream error: {}",
                      error_message ? error_message : "unknown");
            self->running_ = false;
            return;
          }

          LOG_INFO("PipeWire stream state: {} -> {}",
                   pw_stream_state_as_string(old_state),
                   pw_stream_state_as_string(state));
        };
    events.param_changed =
        [](void* userdata, uint32_t id, const struct spa_pod* param) {
          auto* self = static_cast<ScreenCapturerWayland*>(userdata);
          if (!self || id != SPA_PARAM_Format || !param) {
            return;
          }

          spa_video_info_raw info{};
          if (spa_format_video_raw_parse(param, &info) < 0) {
            LOG_ERROR("Failed to parse PipeWire video format");
            return;
          }

          self->spa_video_format_ = info.format;
          self->frame_width_ = static_cast<int>(info.size.width);
          self->frame_height_ = static_cast<int>(info.size.height);
          self->frame_stride_ = static_cast<int>(info.size.width) * 4;

          if (self->spa_video_format_ != SPA_VIDEO_FORMAT_BGRx &&
              self->spa_video_format_ != SPA_VIDEO_FORMAT_BGRA) {
            LOG_ERROR("Unsupported PipeWire pixel format: {}",
                      PipeWireFormatName(self->spa_video_format_));
            self->running_ = false;
            return;
          }

          self->UpdateDisplayGeometry(self->frame_width_, self->frame_height_);
          LOG_INFO("PipeWire video format: {}, {}x{}",
                   PipeWireFormatName(self->spa_video_format_),
                   self->frame_width_, self->frame_height_);
        };
    events.process = [](void* userdata) {
      auto* self = static_cast<ScreenCapturerWayland*>(userdata);
      if (self) {
        self->HandlePipeWireBuffer();
      }
    };
    return events;
  }();

  pw_stream_add_listener(pw_stream_, listener, &stream_events, this);

  uint8_t buffer[1024];
  spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  const spa_pod* params[1];
  const spa_rectangle min_size{1, 1};
  const spa_rectangle max_size{8192, 8192};
  const spa_rectangle default_size{kFallbackWidth, kFallbackHeight};
  const spa_fraction any_rate{0, 1};

  params[0] = reinterpret_cast<const spa_pod*>(spa_pod_builder_add_object(
      &builder, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
      SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
      SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
      SPA_FORMAT_VIDEO_format, SPA_POD_Id(SPA_VIDEO_FORMAT_BGRx),
      SPA_FORMAT_VIDEO_size,
      SPA_POD_CHOICE_RANGE_Rectangle(&default_size, &min_size, &max_size),
      SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&any_rate)));

  const int ret = pw_stream_connect(
      pw_stream_, PW_DIRECTION_INPUT, pipewire_node_id_,
      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                   PW_STREAM_FLAG_MAP_BUFFERS),
      params, 1);
  pw_thread_loop_unlock(pw_thread_loop_);

  if (ret < 0) {
    LOG_ERROR("pw_stream_connect failed: {}", spa_strerror(ret));
    CleanupPipeWire();
    return false;
  }

  return true;
}

void ScreenCapturerWayland::CleanupPipeWire() {
  const bool need_lock = pw_thread_loop_ &&
                         (pw_stream_ != nullptr || pw_core_ != nullptr ||
                          pw_context_ != nullptr);
  if (need_lock) {
    pw_thread_loop_lock(pw_thread_loop_);
  }

  if (pw_stream_) {
    pw_stream_disconnect(pw_stream_);
    pw_stream_destroy(pw_stream_);
    pw_stream_ = nullptr;
  }

  if (stream_listener_) {
    delete static_cast<spa_hook*>(stream_listener_);
    stream_listener_ = nullptr;
  }

  if (pw_core_) {
    pw_core_disconnect(pw_core_);
    pw_core_ = nullptr;
  }

  if (pw_context_) {
    pw_context_destroy(pw_context_);
    pw_context_ = nullptr;
  }

  if (need_lock) {
    pw_thread_loop_unlock(pw_thread_loop_);
  }

  if (pw_thread_loop_) {
    if (pipewire_thread_loop_started_) {
      pw_thread_loop_stop(pw_thread_loop_);
      pipewire_thread_loop_started_ = false;
    }
    pw_thread_loop_destroy(pw_thread_loop_);
    pw_thread_loop_ = nullptr;
  }

  if (pipewire_fd_ >= 0) {
    close(pipewire_fd_);
    pipewire_fd_ = -1;
  }

  if (pipewire_initialized_) {
    pw_deinit();
    pipewire_initialized_ = false;
  }
}

void ScreenCapturerWayland::HandlePipeWireBuffer() {
  if (!pw_stream_) {
    return;
  }

  pw_buffer* buffer = pw_stream_dequeue_buffer(pw_stream_);
  if (!buffer) {
    return;
  }

  auto requeue = [&]() { pw_stream_queue_buffer(pw_stream_, buffer); };

  if (paused_) {
    requeue();
    return;
  }

  spa_buffer* spa_buffer = buffer->buffer;
  if (!spa_buffer || spa_buffer->n_datas == 0 || !spa_buffer->datas[0].data) {
    requeue();
    return;
  }

  const spa_data& data = spa_buffer->datas[0];
  if (!data.chunk) {
    requeue();
    return;
  }

  if (frame_width_ <= 1 || frame_height_ <= 1) {
    requeue();
    return;
  }

  uint8_t* src = static_cast<uint8_t*>(data.data);
  src += data.chunk->offset;

  int stride = frame_stride_;
  if (data.chunk->stride > 0) {
    stride = data.chunk->stride;
  } else if (stride <= 0) {
    stride = frame_width_ * 4;
  }

  int even_width = frame_width_ & ~1;
  int even_height = frame_height_ & ~1;
  if (even_width <= 0 || even_height <= 0) {
    requeue();
    return;
  }

  const size_t y_size = static_cast<size_t>(even_width) * even_height;
  const size_t uv_size = y_size / 2;
  if (y_plane_.size() != y_size) {
    y_plane_.resize(y_size);
  }
  if (uv_plane_.size() != uv_size) {
    uv_plane_.resize(uv_size);
  }

  libyuv::ARGBToNV12(src, stride, y_plane_.data(), even_width,
                     uv_plane_.data(), even_width, even_width, even_height);

  std::vector<uint8_t> nv12;
  nv12.reserve(y_plane_.size() + uv_plane_.size());
  nv12.insert(nv12.end(), y_plane_.begin(), y_plane_.end());
  nv12.insert(nv12.end(), uv_plane_.begin(), uv_plane_.end());

  if (callback_) {
    callback_(nv12.data(), static_cast<int>(nv12.size()), even_width,
              even_height, display_name_.c_str());
  }

  requeue();
}

void ScreenCapturerWayland::UpdateDisplayGeometry(int width, int height) {
  if (width <= 0 || height <= 0) {
    return;
  }

  frame_width_ = width;
  frame_height_ = height;

  if (display_info_list_.empty()) {
    display_info_list_.push_back(DisplayInfo(display_name_, 0, 0, width, height));
    return;
  }

  auto& display = display_info_list_[0];
  display.left = 0;
  display.top = 0;
  display.right = width;
  display.bottom = height;
  display.width = width;
  display.height = height;
}

}  // namespace crossdesk

#endif
