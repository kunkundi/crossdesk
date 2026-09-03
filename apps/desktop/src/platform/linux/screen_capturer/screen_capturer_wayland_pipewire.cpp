#include "screen_capturer_wayland.h"

#include <remote_action.h>
#include "screen_capturer_wayland_build.h"

#if CROSSDESK_WAYLAND_BUILD_ENABLED

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include <display_stream_id.h>
#include "libyuv.h"
#include "linux_cursor_shape.h"
#include "rd_log.h"
#include "shared_cursor_state.h"

namespace crossdesk {

namespace {

struct PipeWireDynamicApi {
  void* library = nullptr;
  bool available = false;

  decltype(&::pw_init) init = nullptr;
  decltype(&::pw_deinit) deinit = nullptr;
  decltype(&::pw_thread_loop_new) thread_loop_new = nullptr;
  decltype(&::pw_thread_loop_destroy) thread_loop_destroy = nullptr;
  decltype(&::pw_thread_loop_get_loop) thread_loop_get_loop = nullptr;
  decltype(&::pw_thread_loop_start) thread_loop_start = nullptr;
  decltype(&::pw_thread_loop_stop) thread_loop_stop = nullptr;
  decltype(&::pw_thread_loop_lock) thread_loop_lock = nullptr;
  decltype(&::pw_thread_loop_unlock) thread_loop_unlock = nullptr;
  decltype(&::pw_thread_loop_wait) thread_loop_wait = nullptr;
  decltype(&::pw_thread_loop_signal) thread_loop_signal = nullptr;
  decltype(&::pw_context_new) context_new = nullptr;
  decltype(&::pw_context_destroy) context_destroy = nullptr;
  decltype(&::pw_context_connect_fd) context_connect_fd = nullptr;
  decltype(&::pw_properties_new) properties_new = nullptr;
  decltype(&::pw_properties_set) properties_set = nullptr;
  decltype(&::pw_stream_new) stream_new = nullptr;
  decltype(&::pw_stream_add_listener) stream_add_listener = nullptr;
  decltype(&::pw_stream_state_as_string) stream_state_as_string = nullptr;
  decltype(&::pw_stream_connect) stream_connect = nullptr;
  decltype(&::pw_stream_update_params) stream_update_params = nullptr;
  decltype(&::pw_stream_set_active) stream_set_active = nullptr;
  decltype(&::pw_stream_disconnect) stream_disconnect = nullptr;
  decltype(&::pw_stream_destroy) stream_destroy = nullptr;
  decltype(&::pw_stream_dequeue_buffer) stream_dequeue_buffer = nullptr;
  decltype(&::pw_stream_queue_buffer) stream_queue_buffer = nullptr;
  decltype(&::pw_core_disconnect) core_disconnect = nullptr;
  decltype(&::pw_proxy_destroy) proxy_destroy = nullptr;
};

template <typename T>
bool LoadPipeWireSymbol(void* library, T* function, const char* symbol_name) {
  *function = reinterpret_cast<T>(dlsym(library, symbol_name));
  if (*function != nullptr) {
    return true;
  }

  LOG_ERROR("Unable to find PipeWire symbol {}", symbol_name);
  return false;
}

void UnloadPipeWireApi(PipeWireDynamicApi* api) {
  if (api->library != nullptr) {
    dlclose(api->library);
  }
  *api = PipeWireDynamicApi{};
}

bool LoadPipeWireApi(PipeWireDynamicApi* api) {
  static constexpr const char* kPipeWireLibraries[] = {
      "libpipewire-0.3.so.0",
      "libpipewire-0.3.so",
  };

  for (const char* library_name : kPipeWireLibraries) {
    api->library = dlopen(library_name, RTLD_LAZY | RTLD_LOCAL);
    if (api->library != nullptr) {
      break;
    }
  }

  if (api->library == nullptr) {
    LOG_WARN("PipeWire 0.3 runtime library is unavailable");
    return false;
  }

  if (!LoadPipeWireSymbol(api->library, &api->init, "pw_init") ||
      !LoadPipeWireSymbol(api->library, &api->deinit, "pw_deinit") ||
      !LoadPipeWireSymbol(api->library, &api->thread_loop_new,
                          "pw_thread_loop_new") ||
      !LoadPipeWireSymbol(api->library, &api->thread_loop_destroy,
                          "pw_thread_loop_destroy") ||
      !LoadPipeWireSymbol(api->library, &api->thread_loop_get_loop,
                          "pw_thread_loop_get_loop") ||
      !LoadPipeWireSymbol(api->library, &api->thread_loop_start,
                          "pw_thread_loop_start") ||
      !LoadPipeWireSymbol(api->library, &api->thread_loop_stop,
                          "pw_thread_loop_stop") ||
      !LoadPipeWireSymbol(api->library, &api->thread_loop_lock,
                          "pw_thread_loop_lock") ||
      !LoadPipeWireSymbol(api->library, &api->thread_loop_unlock,
                          "pw_thread_loop_unlock") ||
      !LoadPipeWireSymbol(api->library, &api->thread_loop_wait,
                          "pw_thread_loop_wait") ||
      !LoadPipeWireSymbol(api->library, &api->thread_loop_signal,
                          "pw_thread_loop_signal") ||
      !LoadPipeWireSymbol(api->library, &api->context_new, "pw_context_new") ||
      !LoadPipeWireSymbol(api->library, &api->context_destroy,
                          "pw_context_destroy") ||
      !LoadPipeWireSymbol(api->library, &api->context_connect_fd,
                          "pw_context_connect_fd") ||
      !LoadPipeWireSymbol(api->library, &api->properties_new,
                          "pw_properties_new") ||
      !LoadPipeWireSymbol(api->library, &api->properties_set,
                          "pw_properties_set") ||
      !LoadPipeWireSymbol(api->library, &api->stream_new, "pw_stream_new") ||
      !LoadPipeWireSymbol(api->library, &api->stream_add_listener,
                          "pw_stream_add_listener") ||
      !LoadPipeWireSymbol(api->library, &api->stream_state_as_string,
                          "pw_stream_state_as_string") ||
      !LoadPipeWireSymbol(api->library, &api->stream_connect,
                          "pw_stream_connect") ||
      !LoadPipeWireSymbol(api->library, &api->stream_update_params,
                          "pw_stream_update_params") ||
      !LoadPipeWireSymbol(api->library, &api->stream_set_active,
                          "pw_stream_set_active") ||
      !LoadPipeWireSymbol(api->library, &api->stream_disconnect,
                          "pw_stream_disconnect") ||
      !LoadPipeWireSymbol(api->library, &api->stream_destroy,
                          "pw_stream_destroy") ||
      !LoadPipeWireSymbol(api->library, &api->stream_dequeue_buffer,
                          "pw_stream_dequeue_buffer") ||
      !LoadPipeWireSymbol(api->library, &api->stream_queue_buffer,
                          "pw_stream_queue_buffer") ||
      !LoadPipeWireSymbol(api->library, &api->core_disconnect,
                          "pw_core_disconnect") ||
      !LoadPipeWireSymbol(api->library, &api->proxy_destroy,
                          "pw_proxy_destroy")) {
    UnloadPipeWireApi(api);
    return false;
  }

  api->available = true;
  return true;
}

const PipeWireDynamicApi* GetPipeWireApi() {
  static PipeWireDynamicApi api;
  static std::once_flag once;
  std::call_once(once, []() { LoadPipeWireApi(&api); });
  return api.available ? &api : nullptr;
}

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

constexpr uint32_t kMaxCursorDimension = 512;
constexpr int kCursorMetadataSize =
    static_cast<int>(sizeof(spa_meta_cursor) + sizeof(spa_meta_bitmap) +
                     64u + kMaxCursorDimension * kMaxCursorDimension * 4u);

enum class CursorBitmapResult { kNoUpdate, kInvisible, kImage, kInvalid };

CursorBitmapResult ReadCursorBitmap(const spa_meta& metadata,
                                    const spa_meta_cursor& cursor,
                                    std::vector<uint32_t>* argb,
                                    LinuxCursorImageView* view) {
  if (!metadata.data || !argb || !view || cursor.bitmap_offset == 0) {
    return CursorBitmapResult::kNoUpdate;
  }

  const size_t bitmap_offset = cursor.bitmap_offset;
  if (bitmap_offset > metadata.size ||
      metadata.size - bitmap_offset < sizeof(spa_meta_bitmap)) {
    return CursorBitmapResult::kInvalid;
  }

  const auto* metadata_bytes = static_cast<const uint8_t*>(metadata.data);
  const auto* bitmap = reinterpret_cast<const spa_meta_bitmap*>(
      metadata_bytes + bitmap_offset);
  if (bitmap->format == 0) {
    return CursorBitmapResult::kNoUpdate;
  }
  if (bitmap->offset == 0) {
    return CursorBitmapResult::kInvisible;
  }

  const uint32_t width = bitmap->size.width;
  const uint32_t height = bitmap->size.height;
  if (width == 0 || height == 0 || width > kMaxCursorDimension ||
      height > kMaxCursorDimension || bitmap->stride < 0 ||
      static_cast<uint32_t>(bitmap->stride) < width * 4u) {
    return CursorBitmapResult::kInvalid;
  }

  const size_t pixel_offset = bitmap_offset + bitmap->offset;
  const size_t required_size =
      static_cast<size_t>(bitmap->stride) * (height - 1u) + width * 4u;
  if (pixel_offset > metadata.size ||
      required_size > metadata.size - pixel_offset) {
    return CursorBitmapResult::kInvalid;
  }

  enum class ChannelOrder { kBgra, kRgba, kArgb, kAbgr, kBgrx, kRgbx };
  ChannelOrder channel_order;
  switch (bitmap->format) {
    case SPA_VIDEO_FORMAT_BGRA:
      channel_order = ChannelOrder::kBgra;
      break;
    case SPA_VIDEO_FORMAT_RGBA:
      channel_order = ChannelOrder::kRgba;
      break;
    case SPA_VIDEO_FORMAT_ARGB:
      channel_order = ChannelOrder::kArgb;
      break;
    case SPA_VIDEO_FORMAT_ABGR:
      channel_order = ChannelOrder::kAbgr;
      break;
    case SPA_VIDEO_FORMAT_BGRx:
      channel_order = ChannelOrder::kBgrx;
      break;
    case SPA_VIDEO_FORMAT_RGBx:
      channel_order = ChannelOrder::kRgbx;
      break;
    default:
      return CursorBitmapResult::kInvalid;
  }

  argb->resize(static_cast<size_t>(width) * height);
  const uint8_t* pixels = metadata_bytes + pixel_offset;
  bool has_visible_pixel = false;
  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t* row = pixels + static_cast<size_t>(y) * bitmap->stride;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t* pixel = row + static_cast<size_t>(x) * 4u;
      uint8_t red = 0;
      uint8_t green = 0;
      uint8_t blue = 0;
      uint8_t alpha = 0xff;
      switch (channel_order) {
        case ChannelOrder::kBgra:
          blue = pixel[0];
          green = pixel[1];
          red = pixel[2];
          alpha = pixel[3];
          break;
        case ChannelOrder::kRgba:
          red = pixel[0];
          green = pixel[1];
          blue = pixel[2];
          alpha = pixel[3];
          break;
        case ChannelOrder::kArgb:
          alpha = pixel[0];
          red = pixel[1];
          green = pixel[2];
          blue = pixel[3];
          break;
        case ChannelOrder::kAbgr:
          alpha = pixel[0];
          blue = pixel[1];
          green = pixel[2];
          red = pixel[3];
          break;
        case ChannelOrder::kBgrx:
          blue = pixel[0];
          green = pixel[1];
          red = pixel[2];
          break;
        case ChannelOrder::kRgbx:
          red = pixel[0];
          green = pixel[1];
          blue = pixel[2];
          break;
      }
      has_visible_pixel = has_visible_pixel || alpha != 0;
      (*argb)[static_cast<size_t>(y) * width + x] =
          (static_cast<uint32_t>(alpha) << 24U) |
          (static_cast<uint32_t>(red) << 16U) |
          (static_cast<uint32_t>(green) << 8U) | blue;
    }
  }

  if (!has_visible_pixel) {
    return CursorBitmapResult::kInvisible;
  }

  view->width = width;
  view->height = height;
  view->xhot = static_cast<uint32_t>(std::max(0, cursor.hotspot.x));
  view->yhot = static_cast<uint32_t>(std::max(0, cursor.hotspot.y));
  view->argb = argb->data();
  return CursorBitmapResult::kImage;
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
  const PipeWireDynamicApi* pipewire = nullptr;
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
  const PipeWireDynamicApi* pipewire = GetPipeWireApi();
  if (!pipewire || !core || !loop || node_id == 0) {
    return "";
  }

  PipeWireTargetLookupState state;
  state.pipewire = pipewire;
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
    state->pipewire->thread_loop_signal(state->loop, false);
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
    state->pipewire->thread_loop_signal(state->loop, false);
  };

  pw_registry_add_listener(registry, &registry_listener, &registry_events,
                           &state);
  pw_core_add_listener(core, &core_listener, &core_events, &state);
  state.sync_seq = pw_core_sync(core, PW_ID_CORE, 0);

  while (!state.done) {
    pipewire->thread_loop_wait(loop);
  }

  spa_hook_remove(&registry_listener);
  spa_hook_remove(&core_listener);
  pipewire->proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
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

bool ScreenCapturerWayland::EnsurePipeWireRuntimeAvailable() const {
  return GetPipeWireApi() != nullptr;
}

bool ScreenCapturerWayland::SetupPipeWireStream(bool relaxed_connect,
                                                PipeWireConnectMode mode) {
  const PipeWireDynamicApi* pipewire = GetPipeWireApi();
  if (!pipewire) {
    LOG_ERROR("PipeWire 0.3 runtime library is unavailable");
    return false;
  }

  if (pipewire_fd_ < 0 || pipewire_node_id_ == 0) {
    return false;
  }

  if (!pipewire_initialized_) {
    pipewire->init(nullptr, nullptr);
    pipewire_initialized_ = true;
  }

  pw_thread_loop_ =
      pipewire->thread_loop_new("crossdesk-wayland-capture", nullptr);
  if (!pw_thread_loop_) {
    LOG_ERROR("Failed to create PipeWire thread loop");
    return false;
  }

  if (pipewire->thread_loop_start(pw_thread_loop_) < 0) {
    LOG_ERROR("Failed to start PipeWire thread loop");
    CleanupPipeWire();
    return false;
  }
  pipewire_thread_loop_started_ = true;

  pipewire->thread_loop_lock(pw_thread_loop_);

  pw_context_ = pipewire->context_new(
      pipewire->thread_loop_get_loop(pw_thread_loop_), nullptr, 0);
  if (!pw_context_) {
    LOG_ERROR("Failed to create PipeWire context");
    pipewire->thread_loop_unlock(pw_thread_loop_);
    CleanupPipeWire();
    return false;
  }

  pw_core_ =
      pipewire->context_connect_fd(pw_context_, pipewire_fd_, nullptr, 0);
  if (!pw_core_) {
    LOG_ERROR("Failed to connect to PipeWire remote");
    pipewire->thread_loop_unlock(pw_thread_loop_);
    CleanupPipeWire();
    return false;
  }
  pipewire_fd_ = -1;

  pw_properties* stream_props = pipewire->properties_new(
      PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
      PW_KEY_MEDIA_ROLE, "Screen", nullptr);
  if (!stream_props) {
    LOG_ERROR("Failed to allocate PipeWire stream properties");
    pipewire->thread_loop_unlock(pw_thread_loop_);
    CleanupPipeWire();
    return false;
  }

  std::string target_object_serial;
  if (mode == PipeWireConnectMode::kTargetObject) {
    target_object_serial = LookupPipeWireTargetObjectSerial(
        pw_core_, pw_thread_loop_, pipewire_node_id_);
    if (!target_object_serial.empty()) {
      pipewire->properties_set(stream_props, PW_KEY_TARGET_OBJECT,
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
      pipewire->stream_new(pw_core_, "CrossDesk Wayland Capture", stream_props);
  if (!pw_stream_) {
    LOG_ERROR("Failed to create PipeWire stream");
    pipewire->thread_loop_unlock(pw_thread_loop_);
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

      const PipeWireDynamicApi* pipewire = GetPipeWireApi();
      LOG_INFO(
          "PipeWire stream state: {} -> {}",
          pipewire ? pipewire->stream_state_as_string(old_state) : "unknown",
          pipewire ? pipewire->stream_state_as_string(state) : "unknown");
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
      const spa_pod* params[3];
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

      if (self->cursor_metadata_enabled_) {
        params[param_count++] =
            reinterpret_cast<const spa_pod*>(spa_pod_builder_add_object(
                &builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
                CROSSDESK_SPA_PARAM_META_TYPE, SPA_POD_Id(SPA_META_Cursor),
                CROSSDESK_SPA_PARAM_META_SIZE,
                SPA_POD_CHOICE_RANGE_Int(
                    kCursorMetadataSize,
                    static_cast<int>(sizeof(struct spa_meta_cursor)),
                    kCursorMetadataSize)));
      }

      if (self->pw_stream_) {
        const PipeWireDynamicApi* pipewire = GetPipeWireApi();
        if (pipewire) {
          const int update_result = pipewire->stream_update_params(
              self->pw_stream_, params, param_count);
          LOG_INFO(
              "PipeWire buffer parameters updated: cursor_metadata={}, "
              "cursor_meta_max_size={}, result={}",
              self->cursor_metadata_enabled_,
              self->cursor_metadata_enabled_ ? kCursorMetadataSize : 0,
              update_result);
        }
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

  pipewire->stream_add_listener(pw_stream_, listener, &stream_events, this);
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
      pipewire->thread_loop_unlock(pw_thread_loop_);
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
  const int ret = pipewire->stream_connect(
      pw_stream_, PW_DIRECTION_INPUT, target_id,
      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                   PW_STREAM_FLAG_MAP_BUFFERS),
      param_count > 0 ? params : nullptr, static_cast<uint32_t>(param_count));
  pipewire->thread_loop_unlock(pw_thread_loop_);

  if (ret < 0) {
    LOG_ERROR("pw_stream_connect failed: {}", spa_strerror(ret));
    CleanupPipeWire();
    return false;
  }

  return true;
}

void ScreenCapturerWayland::CleanupPipeWire() {
  const PipeWireDynamicApi* pipewire = GetPipeWireApi();
  const bool need_lock =
      pipewire && pw_thread_loop_ &&
      (pw_stream_ != nullptr || pw_core_ != nullptr || pw_context_ != nullptr);
  if (need_lock) {
    pipewire->thread_loop_lock(pw_thread_loop_);
  }

  if (pw_stream_ && pipewire) {
    pipewire->stream_set_active(pw_stream_, false);
    pipewire->stream_disconnect(pw_stream_);
  }

  if (stream_listener_) {
    spa_hook_remove(static_cast<spa_hook*>(stream_listener_));
    delete static_cast<spa_hook*>(stream_listener_);
    stream_listener_ = nullptr;
  }

  if (pw_stream_ && pipewire) {
    pipewire->stream_destroy(pw_stream_);
  }
  pw_stream_ = nullptr;

  if (pw_core_ && pipewire) {
    pipewire->core_disconnect(pw_core_);
  }
  pw_core_ = nullptr;

  if (pw_context_ && pipewire) {
    pipewire->context_destroy(pw_context_);
  }
  pw_context_ = nullptr;

  if (need_lock) {
    pipewire->thread_loop_unlock(pw_thread_loop_);
  }

  if (pw_thread_loop_ && pipewire) {
    if (pipewire_thread_loop_started_) {
      pipewire->thread_loop_stop(pw_thread_loop_);
      pipewire_thread_loop_started_ = false;
    }
    pipewire->thread_loop_destroy(pw_thread_loop_);
  }
  pw_thread_loop_ = nullptr;
  pipewire_thread_loop_started_ = false;

  if (pipewire_fd_ >= 0) {
    close(pipewire_fd_);
    pipewire_fd_ = -1;
  }

  pipewire_format_ready_.store(false);
  pipewire_stream_start_ms_.store(0);
  pipewire_last_frame_ms_.store(0);

  if (pipewire_initialized_ && pipewire) {
    pipewire->deinit();
  }
  pipewire_initialized_ = false;
}

void ScreenCapturerWayland::HandlePipeWireBuffer() {
  const PipeWireDynamicApi* pipewire = GetPipeWireApi();
  if (!pw_stream_ || !pipewire) {
    return;
  }

  pw_buffer* buffer = pipewire->stream_dequeue_buffer(pw_stream_);
  if (!buffer) {
    return;
  }

  auto requeue = [&]() { pipewire->stream_queue_buffer(pw_stream_, buffer); };

  if (paused_) {
    requeue();
    return;
  }

  spa_buffer* spa_buffer = buffer->buffer;
  if (!spa_buffer || spa_buffer->n_datas == 0 || !spa_buffer->datas[0].data) {
    requeue();
    return;
  }

  if (cursor_metadata_enabled_) {
    spa_meta* cursor_metadata =
        spa_buffer_find_meta(spa_buffer, SPA_META_Cursor);
    if (!cursor_metadata) {
      ++cursor_metadata_missing_buffers_;
      if (cursor_metadata_missing_buffers_ == 120) {
        LOG_WARN(
            "PipeWire stream has not attached SPA_META_Cursor after 120 "
            "buffers; cursor metadata negotiation was not accepted");
      }
    } else {
      cursor_metadata_missing_buffers_ = 0;
      if (!cursor_metadata_seen_) {
        cursor_metadata_seen_ = true;
        LOG_INFO("PipeWire cursor metadata attached, size={}",
                 cursor_metadata->size);
      }
    }
    if (cursor_metadata && cursor_metadata->data &&
        cursor_metadata->size >= sizeof(spa_meta_cursor)) {
      const auto* cursor =
          static_cast<const spa_meta_cursor*>(cursor_metadata->data);
      if (spa_meta_cursor_is_valid(cursor)) {
        std::vector<uint32_t> cursor_argb;
        LinuxCursorImageView cursor_view;
        const CursorBitmapResult cursor_result = ReadCursorBitmap(
            *cursor_metadata, *cursor, &cursor_argb, &cursor_view);
        if (cursor_result == CursorBitmapResult::kInvisible) {
          PublishSharedCursorState(false, RemoteCursorShape::none);
          LOG_INFO("PipeWire cursor became invisible, id={}", cursor->id);
        } else if (cursor_result == CursorBitmapResult::kImage) {
          const RemoteCursorShape shape =
              cursor_theme_matcher_
                  ? cursor_theme_matcher_->Match(cursor_view)
                  : RemoteCursorShape::default_cursor;
          PublishSharedCursorState(true, shape);
          LOG_INFO(
              "PipeWire cursor shape update: id={}, size={}x{}, "
              "hotspot=({},{}), shape={}",
              cursor->id, cursor_view.width, cursor_view.height,
              cursor_view.xhot, cursor_view.yhot, static_cast<int>(shape));
        } else if (cursor_result == CursorBitmapResult::kInvalid) {
          PublishSharedCursorState(true, RemoteCursorShape::default_cursor);
          LOG_WARN("Using default shape for malformed PipeWire cursor "
                   "metadata, id={}",
                   cursor->id);
        }
      }
    }
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
    const std::string stream_id = MakeDisplayStreamId(0);
    callback_(nv12.data(), static_cast<int>(nv12.size()), even_width,
              even_height, stream_id.c_str(), nullptr);
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
