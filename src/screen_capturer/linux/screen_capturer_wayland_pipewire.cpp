#include "screen_capturer_wayland.h"
#include "screen_capturer_wayland_build.h"

#if CROSSDESK_WAYLAND_BUILD_ENABLED

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>
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
#ifdef SPA_VIDEO_FORMAT_RGBx
    case SPA_VIDEO_FORMAT_RGBx:
      return "RGBx";
#endif
#ifdef SPA_VIDEO_FORMAT_RGBA
    case SPA_VIDEO_FORMAT_RGBA:
      return "RGBA";
#endif
    default:
      return "unsupported";
  }
}

const char* PipeWireConnectModeName(
    ScreenCapturerWayland::PipeWireConnectMode mode) {
  switch (mode) {
    case ScreenCapturerWayland::PipeWireConnectMode::kTargetObject:
      return "target-object";
    case ScreenCapturerWayland::PipeWireConnectMode::kNodeId:
      return "node-id";
    case ScreenCapturerWayland::PipeWireConnectMode::kAny:
      return "any";
    default:
      return "unknown";
  }
}

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

double SnapLikelyFractionalScale(double observed_scale) {
  static constexpr double kCandidates[] = {
      1.0, 1.25, 1.3333333333, 1.5, 1.6666666667, 1.75, 2.0, 2.25, 2.5, 3.0};
  double best = observed_scale;
  double best_error = std::numeric_limits<double>::max();
  for (double candidate : kCandidates) {
    const double error = std::abs(candidate - observed_scale);
    if (error < best_error) {
      best = candidate;
      best_error = error;
    }
  }

  return best_error <= 0.08 ? best : observed_scale;
}

struct PipeWireTargetLookupState {
  pw_thread_loop* loop = nullptr;
  uint32_t target_node_id = 0;
  int sync_seq = -1;
  bool done = false;
  bool found = false;
  std::string object_serial;
};

std::string LookupPipeWireTargetObjectSerial(pw_core* core,
                                             pw_thread_loop* loop,
                                             uint32_t node_id) {
  if (!core || !loop || node_id == 0) {
    return "";
  }

  PipeWireTargetLookupState state;
  state.loop = loop;
  state.target_node_id = node_id;

  pw_registry* registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
  if (!registry) {
    return "";
  }

  spa_hook registry_listener{};
  spa_hook core_listener{};

  pw_registry_events registry_events{};
  registry_events.version = PW_VERSION_REGISTRY_EVENTS;
  registry_events.global = [](void* userdata, uint32_t id, uint32_t permissions,
                              const char* type, uint32_t version,
                              const spa_dict* props) {
    (void)permissions;
    (void)version;
    auto* state = static_cast<PipeWireTargetLookupState*>(userdata);
    if (!state || !props || id != state->target_node_id || !type) {
      return;
    }
    if (strcmp(type, PW_TYPE_INTERFACE_Node) != 0) {
      return;
    }

    const char* object_serial = spa_dict_lookup(props, PW_KEY_OBJECT_SERIAL);
    if (!object_serial || object_serial[0] == '\0') {
      object_serial = spa_dict_lookup(props, "object.serial");
    }
    if (!object_serial || object_serial[0] == '\0') {
      return;
    }

    state->object_serial = object_serial;
    state->found = true;
  };

  pw_core_events core_events{};
  core_events.version = PW_VERSION_CORE_EVENTS;
  core_events.done = [](void* userdata, uint32_t id, int seq) {
    auto* state = static_cast<PipeWireTargetLookupState*>(userdata);
    if (!state || id != PW_ID_CORE || seq != state->sync_seq) {
      return;
    }
    state->done = true;
    pw_thread_loop_signal(state->loop, false);
  };
  core_events.error = [](void* userdata, uint32_t id, int seq, int res,
                         const char* message) {
    (void)id;
    (void)seq;
    (void)res;
    auto* state = static_cast<PipeWireTargetLookupState*>(userdata);
    if (!state) {
      return;
    }
    LOG_WARN("PipeWire registry lookup error: {}",
             message ? message : "unknown");
    state->done = true;
    pw_thread_loop_signal(state->loop, false);
  };

  pw_registry_add_listener(registry, &registry_listener, &registry_events,
                           &state);
  pw_core_add_listener(core, &core_listener, &core_events, &state);
  state.sync_seq = pw_core_sync(core, PW_ID_CORE, 0);

  while (!state.done) {
    pw_thread_loop_wait(loop);
  }

  spa_hook_remove(&registry_listener);
  spa_hook_remove(&core_listener);
  pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
  return state.found ? state.object_serial : "";
}

int BytesPerPixel(uint32_t spa_format) {
  switch (spa_format) {
    case SPA_VIDEO_FORMAT_BGRx:
    case SPA_VIDEO_FORMAT_BGRA:
#ifdef SPA_VIDEO_FORMAT_RGBx
    case SPA_VIDEO_FORMAT_RGBx:
#endif
#ifdef SPA_VIDEO_FORMAT_RGBA
    case SPA_VIDEO_FORMAT_RGBA:
#endif
      return 4;
    default:
      return 0;
  }
}

}  // namespace

bool ScreenCapturerWayland::SetupPipeWireStream(bool relaxed_connect,
                                                PipeWireConnectMode mode) {
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

  pw_properties* stream_props =
      pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY,
                        "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr);
  if (!stream_props) {
    LOG_ERROR("Failed to allocate PipeWire stream properties");
    pw_thread_loop_unlock(pw_thread_loop_);
    CleanupPipeWire();
    return false;
  }

  std::string target_object_serial;
  if (mode == PipeWireConnectMode::kTargetObject) {
    target_object_serial = LookupPipeWireTargetObjectSerial(
        pw_core_, pw_thread_loop_, pipewire_node_id_);
    if (!target_object_serial.empty()) {
      pw_properties_set(stream_props, PW_KEY_TARGET_OBJECT,
                        target_object_serial.c_str());
      LOG_INFO("PipeWire target object serial for node {} is {}",
               pipewire_node_id_, target_object_serial);
    } else {
      LOG_WARN(
          "PipeWire target object serial lookup failed for node {}, "
          "falling back to direct target id in target-object mode",
          pipewire_node_id_);
    }
  }

  pw_stream_ =
      pw_stream_new(pw_core_, "CrossDesk Wayland Capture", stream_props);
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
    events.state_changed = [](void* userdata, enum pw_stream_state old_state,
                              enum pw_stream_state state,
                              const char* error_message) {
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
    events.param_changed = [](void* userdata, uint32_t id,
                              const struct spa_pod* param) {
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

      bool supported_format =
          (self->spa_video_format_ == SPA_VIDEO_FORMAT_BGRx) ||
          (self->spa_video_format_ == SPA_VIDEO_FORMAT_BGRA);
#ifdef SPA_VIDEO_FORMAT_RGBx
      supported_format = supported_format ||
                         (self->spa_video_format_ == SPA_VIDEO_FORMAT_RGBx);
#endif
#ifdef SPA_VIDEO_FORMAT_RGBA
      supported_format = supported_format ||
                         (self->spa_video_format_ == SPA_VIDEO_FORMAT_RGBA);
#endif
      if (!supported_format) {
        LOG_ERROR("Unsupported PipeWire pixel format: {}",
                  PipeWireFormatName(self->spa_video_format_));
        self->running_ = false;
        return;
      }

      const int bytes_per_pixel = BytesPerPixel(self->spa_video_format_);
      if (bytes_per_pixel <= 0 || self->frame_width_ <= 0 ||
          self->frame_height_ <= 0) {
        LOG_ERROR("Invalid PipeWire frame layout: format={}, size={}x{}",
                  PipeWireFormatName(self->spa_video_format_),
                  self->frame_width_, self->frame_height_);
        self->running_ = false;
        return;
      }

      self->frame_stride_ = self->frame_width_ * bytes_per_pixel;

      uint8_t buffer[1024];
      spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
      const spa_pod* params[2];
      uint32_t param_count = 0;

      params[param_count++] =
          reinterpret_cast<const spa_pod*>(spa_pod_builder_add_object(
              &builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
              CROSSDESK_SPA_PARAM_BUFFERS_BUFFERS,
              SPA_POD_CHOICE_RANGE_Int(8, 4, 16),
              CROSSDESK_SPA_PARAM_BUFFERS_BLOCKS, SPA_POD_Int(1),
              CROSSDESK_SPA_PARAM_BUFFERS_SIZE,
              SPA_POD_CHOICE_RANGE_Int(
                  self->frame_stride_ * self->frame_height_,
                  self->frame_stride_ * self->frame_height_,
                  self->frame_stride_ * self->frame_height_),
              CROSSDESK_SPA_PARAM_BUFFERS_STRIDE,
              SPA_POD_CHOICE_RANGE_Int(self->frame_stride_, self->frame_stride_,
                                       self->frame_stride_)));

      params[param_count++] =
          reinterpret_cast<const spa_pod*>(spa_pod_builder_add_object(
              &builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
              CROSSDESK_SPA_PARAM_META_TYPE, SPA_POD_Id(SPA_META_Header),
              CROSSDESK_SPA_PARAM_META_SIZE,
              SPA_POD_Int(sizeof(struct spa_meta_header))));

      if (self->pw_stream_) {
        pw_stream_update_params(self->pw_stream_, params, param_count);
      }
      self->pipewire_format_ready_.store(true);

      int pointer_width =
          self->logical_width_ > 0 ? self->logical_width_ : self->frame_width_;
      int pointer_height = self->logical_height_ > 0 ? self->logical_height_
                                                     : self->frame_height_;
      double observed_scale_x = pointer_width > 0
                                    ? static_cast<double>(self->frame_width_) /
                                          static_cast<double>(pointer_width)
                                    : 1.0;
      double observed_scale_y = pointer_height > 0
                                    ? static_cast<double>(self->frame_height_) /
                                          static_cast<double>(pointer_height)
                                    : 1.0;
      double snapped_scale = 1.0;
      bool derived_pointer_space = false;

      if (!self->portal_has_logical_size_ && self->portal_stream_width_ > 0 &&
          self->portal_stream_height_ > 0 && self->frame_width_ > 0 &&
          self->frame_height_ > 0) {
        const double raw_scale_x =
            static_cast<double>(self->frame_width_) /
            static_cast<double>(self->portal_stream_width_);
        const double raw_scale_y =
            static_cast<double>(self->frame_height_) /
            static_cast<double>(self->portal_stream_height_);
        const double average_scale = (raw_scale_x + raw_scale_y) * 0.5;
        snapped_scale = SnapLikelyFractionalScale(average_scale);

        const bool scales_are_consistent =
            std::abs(raw_scale_x - raw_scale_y) <= 0.05;
        const bool scale_was_snapped =
            std::abs(snapped_scale - average_scale) <= 0.08;
        if (scales_are_consistent && scale_was_snapped &&
            snapped_scale > 1.05) {
          pointer_width =
              std::max(1, static_cast<int>(std::floor(
                              static_cast<double>(self->portal_stream_width_) *
                                  snapped_scale +
                              1e-6)));
          pointer_height =
              std::max(1, static_cast<int>(std::floor(
                              static_cast<double>(self->portal_stream_height_) *
                                  snapped_scale +
                              1e-6)));
          observed_scale_x = pointer_width > 0
                                 ? static_cast<double>(self->frame_width_) /
                                       static_cast<double>(pointer_width)
                                 : 1.0;
          observed_scale_y = pointer_height > 0
                                 ? static_cast<double>(self->frame_height_) /
                                       static_cast<double>(pointer_height)
                                 : 1.0;
          derived_pointer_space = true;
        }
      }

      self->UpdateDisplayGeometry(pointer_width, pointer_height);
      if (derived_pointer_space) {
        LOG_INFO(
            "PipeWire video format: {}, {}x{} stride={} (pointer space {}x{}, "
            "derived from portal stream {}x{} with compositor scale {:.4f}, "
            "effective scale {:.4f}x{:.4f})",
            PipeWireFormatName(self->spa_video_format_), self->frame_width_,
            self->frame_height_, self->frame_stride_, pointer_width,
            pointer_height, self->portal_stream_width_,
            self->portal_stream_height_, snapped_scale, observed_scale_x,
            observed_scale_y);
      } else {
        LOG_INFO(
            "PipeWire video format: {}, {}x{} stride={} (pointer space {}x{}, "
            "scale {:.4f}x{:.4f})",
            PipeWireFormatName(self->spa_video_format_), self->frame_width_,
            self->frame_height_, self->frame_stride_, pointer_width,
            pointer_height, observed_scale_x, observed_scale_y);
      }
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
  pipewire_format_ready_.store(false);
  pipewire_stream_start_ms_.store(NowMs());
  pipewire_last_frame_ms_.store(0);

  uint8_t buffer[4096];
  spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  const spa_pod* params[8];
  int param_count = 0;
  const spa_rectangle fixed_size{
      static_cast<uint32_t>(logical_width_ > 0 ? logical_width_
                                               : kFallbackWidth),
      static_cast<uint32_t>(logical_height_ > 0 ? logical_height_
                                                : kFallbackHeight)};
  const spa_rectangle min_size{1u, 1u};
  const spa_rectangle max_size{16384u, 16384u};

  if (!relaxed_connect) {
    auto add_format_param = [&](uint32_t spa_format) {
      if (param_count >= static_cast<int>(sizeof(params) / sizeof(params[0]))) {
        return;
      }
      params[param_count++] =
          reinterpret_cast<const spa_pod*>(spa_pod_builder_add_object(
              &builder, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
              SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
              SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
              SPA_FORMAT_VIDEO_format, SPA_POD_Id(spa_format),
              SPA_FORMAT_VIDEO_size,
              SPA_POD_CHOICE_RANGE_Rectangle(&fixed_size, &min_size,
                                             &max_size)));
    };

    add_format_param(SPA_VIDEO_FORMAT_BGRx);
    add_format_param(SPA_VIDEO_FORMAT_BGRA);
#ifdef SPA_VIDEO_FORMAT_RGBx
    add_format_param(SPA_VIDEO_FORMAT_RGBx);
#endif
#ifdef SPA_VIDEO_FORMAT_RGBA
    add_format_param(SPA_VIDEO_FORMAT_RGBA);
#endif

    if (param_count == 0) {
      LOG_ERROR("No valid PipeWire format params were built");
      pw_thread_loop_unlock(pw_thread_loop_);
      CleanupPipeWire();
      return false;
    }
  } else {
    LOG_INFO("PipeWire stream using relaxed format negotiation");
  }

  uint32_t target_id = PW_ID_ANY;
  if (mode == PipeWireConnectMode::kNodeId ||
      (mode == PipeWireConnectMode::kTargetObject &&
       target_object_serial.empty())) {
    target_id = pipewire_node_id_;
  }
  LOG_INFO(
      "PipeWire connecting stream: mode={}, node_id={}, target_id={}, "
      "target_object_serial={}, relaxed_connect={}, param_count={}, "
      "requested_size={}x{}",
      PipeWireConnectModeName(mode), pipewire_node_id_, target_id,
      target_object_serial.empty() ? "none" : target_object_serial.c_str(),
      relaxed_connect, param_count, fixed_size.width, fixed_size.height);
  const int ret = pw_stream_connect(
      pw_stream_, PW_DIRECTION_INPUT, target_id,
      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                   PW_STREAM_FLAG_MAP_BUFFERS),
      param_count > 0 ? params : nullptr, static_cast<uint32_t>(param_count));
  pw_thread_loop_unlock(pw_thread_loop_);

  if (ret < 0) {
    LOG_ERROR("pw_stream_connect failed: {}", spa_strerror(ret));
    CleanupPipeWire();
    return false;
  }

  return true;
}

void ScreenCapturerWayland::CleanupPipeWire() {
  const bool need_lock =
      pw_thread_loop_ &&
      (pw_stream_ != nullptr || pw_core_ != nullptr || pw_context_ != nullptr);
  if (need_lock) {
    pw_thread_loop_lock(pw_thread_loop_);
  }

  if (pw_stream_) {
    pw_stream_set_active(pw_stream_, false);
    pw_stream_disconnect(pw_stream_);
  }

  if (stream_listener_) {
    spa_hook_remove(static_cast<spa_hook*>(stream_listener_));
    delete static_cast<spa_hook*>(stream_listener_);
    stream_listener_ = nullptr;
  }

  if (pw_stream_) {
    pw_stream_destroy(pw_stream_);
    pw_stream_ = nullptr;
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

  pipewire_format_ready_.store(false);
  pipewire_stream_start_ms_.store(0);
  pipewire_last_frame_ms_.store(0);

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

  libyuv::ARGBToNV12(src, stride, y_plane_.data(), even_width, uv_plane_.data(),
                     even_width, even_width, even_height);

  std::vector<uint8_t> nv12;
  nv12.reserve(y_plane_.size() + uv_plane_.size());
  nv12.insert(nv12.end(), y_plane_.begin(), y_plane_.end());
  nv12.insert(nv12.end(), uv_plane_.begin(), uv_plane_.end());

  if (callback_) {
    callback_(nv12.data(), static_cast<int>(nv12.size()), even_width,
              even_height, display_name_.c_str());
  }
  pipewire_last_frame_ms_.store(NowMs());

  requeue();
}

void ScreenCapturerWayland::UpdateDisplayGeometry(int width, int height) {
  if (width <= 0 || height <= 0) {
    return;
  }

  void* stream_handle =
      reinterpret_cast<void*>(static_cast<uintptr_t>(pipewire_node_id_));

  if (display_info_list_.empty()) {
    display_info_list_.push_back(
        DisplayInfo(stream_handle, display_name_, true, 0, 0, width, height));
    return;
  }

  auto& display = display_info_list_[0];
  display.handle = stream_handle;
  display.left = 0;
  display.top = 0;
  display.right = width;
  display.bottom = height;
  display.width = width;
  display.height = height;
}

}  // namespace crossdesk

#endif
