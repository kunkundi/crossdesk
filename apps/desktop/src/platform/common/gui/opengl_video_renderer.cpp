#include "platform/common/gui/opengl_video_renderer.h"

#if !defined(_WIN32) && !defined(__linux__)
#error "OpenGlVideoRenderer is only available on Windows and Linux"
#endif

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

#include <GL/gl.h>
#include <GL/glext.h>
#if defined(__linux__)
#include <GL/glx.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

#include "rd_log.h"
#if defined(_WIN32) && USE_CUDA
#include "nvcodec_api.h"
#endif

namespace crossdesk {
namespace {

constexpr size_t kFrameSlotCount = 3;
constexpr size_t kMaxPendingFrames = 2;
constexpr auto kActiveRenderWindow = std::chrono::milliseconds(250);

#if defined(_WIN32)
class WindowsNativeFrameRef {
public:
  WindowsNativeFrameRef() = default;

  explicit WindowsNativeFrameRef(const XWindowsVideoFrame *frame)
      : frame_(frame) {
    if (frame_ && frame_->owner && frame_->retain) {
      frame_->retain(frame_->owner);
    } else {
      frame_ = nullptr;
    }
  }

  WindowsNativeFrameRef(const WindowsNativeFrameRef &other)
      : WindowsNativeFrameRef(other.frame_) {}

  WindowsNativeFrameRef(WindowsNativeFrameRef &&other) noexcept
      : frame_(std::exchange(other.frame_, nullptr)) {}

  WindowsNativeFrameRef &operator=(WindowsNativeFrameRef other) noexcept {
    Swap(other);
    return *this;
  }

  ~WindowsNativeFrameRef() { Reset(); }

  void Reset() {
    if (frame_ && frame_->owner && frame_->release) {
      frame_->release(frame_->owner);
    }
    frame_ = nullptr;
  }

  void Swap(WindowsNativeFrameRef &other) noexcept {
    std::swap(frame_, other.frame_);
  }

  const XWindowsVideoFrame *Get() const { return frame_; }
  explicit operator bool() const { return frame_ != nullptr; }

private:
  const XWindowsVideoFrame *frame_ = nullptr;
};

const XWindowsVideoFrame *GetWindowsNativeFrame(const XVideoFrame &frame) {
  if (frame.native_handle_type != XVideoFrameNativeHandleWindowsNv12 ||
      !frame.native_handle) {
    return nullptr;
  }
  const auto *native =
      static_cast<const XWindowsVideoFrame *>(frame.native_handle);
  if (native->struct_size < static_cast<uint32_t>(sizeof(XWindowsVideoFrame)) ||
      !native->owner || !native->retain || !native->release ||
      !native->copy_to_cpu || native->width == 0 || native->height == 0 ||
      (native->width & 1U) != 0 || (native->height & 1U) != 0 ||
      native->y_stride < native->width || native->uv_stride < native->width) {
    return nullptr;
  }
  if (native->memory_type == XWindowsVideoFrameMemoryCpu) {
    return native->y_plane && native->uv_plane ? native : nullptr;
  }
  if (native->memory_type == XWindowsVideoFrameMemoryCuda) {
    return native->y_device_pointer != 0 && native->uv_device_pointer != 0 &&
                   native->device_context
               ? native
               : nullptr;
  }
  return nullptr;
}
#endif

enum class SlotUse {
  available,
  pending,
  uploading,
};

struct FrameSlot {
  std::vector<unsigned char> bytes;
#if defined(_WIN32)
  WindowsNativeFrameRef native_frame;
#endif
  std::chrono::steady_clock::time_point submitted_at;
  int width = 0;
  int height = 0;
  std::string remote_id;
  uint64_t sequence = 0;
  SlotUse use = SlotUse::available;
  bool valid = false;
};

struct SharedFrameState {
  mutable std::mutex mutex;
  std::array<FrameSlot, kFrameSlotCount> slots;
  std::string selected_stream;
  uint64_t next_sequence = 1;
};

FrameSlot *SelectSubmissionSlot(SharedFrameState &frames,
                                std::string_view remote_id,
                                bool replace_pending,
                                VideoRenderer::SubmitResult *result) {
  if (frames.selected_stream != remote_id) {
    *result = VideoRenderer::SubmitResult::not_selected;
    return nullptr;
  }

  FrameSlot *target = nullptr;
  if (replace_pending) {
    size_t pending_count = 0;
    FrameSlot *oldest_pending = nullptr;
    for (auto &slot : frames.slots) {
      if (slot.use == SlotUse::pending && slot.remote_id == remote_id) {
        ++pending_count;
        if (!oldest_pending || slot.sequence < oldest_pending->sequence) {
          oldest_pending = &slot;
        }
      }
    }
    // Keep a two-frame jitter queue. A second frame that arrives before the
    // next display callback is preserved instead of replacing the first one.
    // Once the queue is full, replace its oldest frame to keep latency bounded.
    if (pending_count >= kMaxPendingFrames) {
      target = oldest_pending;
    }
  } else {
    for (const auto &slot : frames.slots) {
      if (slot.valid && slot.remote_id == remote_id &&
          (slot.use == SlotUse::pending || slot.use == SlotUse::uploading)) {
        *result = VideoRenderer::SubmitResult::dropped;
        return nullptr;
      }
    }
  }
  if (!target) {
    uint64_t oldest_sequence = std::numeric_limits<uint64_t>::max();
    for (auto &slot : frames.slots) {
      if (slot.use != SlotUse::available) {
        continue;
      }
      if (!slot.valid) {
        target = &slot;
        break;
      }
      if (slot.sequence < oldest_sequence) {
        oldest_sequence = slot.sequence;
        target = &slot;
      }
    }
  }
  if (!target && replace_pending) {
    // An upload can temporarily own one slot. If no available slot remains,
    // prefer replacing the oldest queued frame over blocking the decode thread.
    for (auto &slot : frames.slots) {
      if (slot.use == SlotUse::pending && slot.remote_id == remote_id &&
          (!target || slot.sequence < target->sequence)) {
        target = &slot;
      }
    }
  }
  if (!target) {
    *result = VideoRenderer::SubmitResult::dropped;
    return nullptr;
  }
  *result = VideoRenderer::SubmitResult::submitted;
  return target;
}

template <typename T> bool LoadOpenGlFunction(T *function, const char *name) {
#if defined(_WIN32)
  PROC address = wglGetProcAddress(name);
  if (address == nullptr || address == reinterpret_cast<PROC>(1) ||
      address == reinterpret_cast<PROC>(2) ||
      address == reinterpret_cast<PROC>(3) ||
      address == reinterpret_cast<PROC>(-1)) {
    HMODULE module = GetModuleHandleW(L"opengl32.dll");
    address = module ? GetProcAddress(module, name) : nullptr;
  }
  *function = reinterpret_cast<T>(address);
#elif defined(__linux__)
  void *address = dlsym(RTLD_DEFAULT, name);
  if (address == nullptr) {
    address = reinterpret_cast<void *>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte *>(name)));
  }
  *function = reinterpret_cast<T>(address);
#endif
  return *function != nullptr;
}

struct OpenGlFunctions {
  PFNGLACTIVETEXTUREPROC active_texture = nullptr;
  PFNGLATTACHSHADERPROC attach_shader = nullptr;
  PFNGLBINDBUFFERPROC bind_buffer = nullptr;
  PFNGLBUFFERDATAPROC buffer_data = nullptr;
  PFNGLCOMPILESHADERPROC compile_shader = nullptr;
  PFNGLCREATEPROGRAMPROC create_program = nullptr;
  PFNGLCREATESHADERPROC create_shader = nullptr;
  PFNGLDELETEBUFFERSPROC delete_buffers = nullptr;
  PFNGLDELETEPROGRAMPROC delete_program = nullptr;
  PFNGLDELETESHADERPROC delete_shader = nullptr;
  PFNGLDISABLEVERTEXATTRIBARRAYPROC disable_vertex_attrib_array = nullptr;
  PFNGLENABLEVERTEXATTRIBARRAYPROC enable_vertex_attrib_array = nullptr;
  PFNGLGENBUFFERSPROC gen_buffers = nullptr;
  PFNGLGETATTRIBLOCATIONPROC get_attrib_location = nullptr;
  PFNGLGETPROGRAMINFOLOGPROC get_program_info_log = nullptr;
  PFNGLGETPROGRAMIVPROC get_program_iv = nullptr;
  PFNGLGETSHADERINFOLOGPROC get_shader_info_log = nullptr;
  PFNGLGETSHADERIVPROC get_shader_iv = nullptr;
  PFNGLGETUNIFORMLOCATIONPROC get_uniform_location = nullptr;
  PFNGLGETVERTEXATTRIBIVPROC get_vertex_attrib_iv = nullptr;
  PFNGLGETVERTEXATTRIBPOINTERVPROC get_vertex_attrib_pointer_v = nullptr;
  PFNGLLINKPROGRAMPROC link_program = nullptr;
  PFNGLSHADERSOURCEPROC shader_source = nullptr;
  PFNGLUNIFORM1FPROC uniform_1f = nullptr;
  PFNGLUNIFORM1IPROC uniform_1i = nullptr;
  PFNGLUNIFORM2FPROC uniform_2f = nullptr;
  PFNGLUNIFORM4FPROC uniform_4f = nullptr;
  PFNGLUSEPROGRAMPROC use_program = nullptr;
  PFNGLVERTEXATTRIBPOINTERPROC vertex_attrib_pointer = nullptr;

  bool Load() {
    return LoadOpenGlFunction(&active_texture, "glActiveTexture") &&
           LoadOpenGlFunction(&attach_shader, "glAttachShader") &&
           LoadOpenGlFunction(&bind_buffer, "glBindBuffer") &&
           LoadOpenGlFunction(&buffer_data, "glBufferData") &&
           LoadOpenGlFunction(&compile_shader, "glCompileShader") &&
           LoadOpenGlFunction(&create_program, "glCreateProgram") &&
           LoadOpenGlFunction(&create_shader, "glCreateShader") &&
           LoadOpenGlFunction(&delete_buffers, "glDeleteBuffers") &&
           LoadOpenGlFunction(&delete_program, "glDeleteProgram") &&
           LoadOpenGlFunction(&delete_shader, "glDeleteShader") &&
           LoadOpenGlFunction(&disable_vertex_attrib_array,
                              "glDisableVertexAttribArray") &&
           LoadOpenGlFunction(&enable_vertex_attrib_array,
                              "glEnableVertexAttribArray") &&
           LoadOpenGlFunction(&gen_buffers, "glGenBuffers") &&
           LoadOpenGlFunction(&get_attrib_location, "glGetAttribLocation") &&
           LoadOpenGlFunction(&get_program_info_log, "glGetProgramInfoLog") &&
           LoadOpenGlFunction(&get_program_iv, "glGetProgramiv") &&
           LoadOpenGlFunction(&get_shader_info_log, "glGetShaderInfoLog") &&
           LoadOpenGlFunction(&get_shader_iv, "glGetShaderiv") &&
           LoadOpenGlFunction(&get_uniform_location, "glGetUniformLocation") &&
           LoadOpenGlFunction(&get_vertex_attrib_iv, "glGetVertexAttribiv") &&
           LoadOpenGlFunction(&get_vertex_attrib_pointer_v,
                              "glGetVertexAttribPointerv") &&
           LoadOpenGlFunction(&link_program, "glLinkProgram") &&
           LoadOpenGlFunction(&shader_source, "glShaderSource") &&
           LoadOpenGlFunction(&uniform_1f, "glUniform1f") &&
           LoadOpenGlFunction(&uniform_1i, "glUniform1i") &&
           LoadOpenGlFunction(&uniform_2f, "glUniform2f") &&
           LoadOpenGlFunction(&uniform_4f, "glUniform4f") &&
           LoadOpenGlFunction(&use_program, "glUseProgram") &&
           LoadOpenGlFunction(&vertex_attrib_pointer, "glVertexAttribPointer");
  }
};

GLuint CompileShader(const OpenGlFunctions &gl, GLenum type,
                     const char *source) {
  const GLuint shader = gl.create_shader(type);
  if (shader == 0) {
    return 0;
  }
  gl.shader_source(shader, 1, &source, nullptr);
  gl.compile_shader(shader);

  GLint compiled = GL_FALSE;
  gl.get_shader_iv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == GL_TRUE) {
    return shader;
  }

  GLint log_length = 0;
  gl.get_shader_iv(shader, GL_INFO_LOG_LENGTH, &log_length);
  std::string log(static_cast<size_t>(std::max(log_length, 1)), '\0');
  GLsizei written = 0;
  gl.get_shader_info_log(shader, static_cast<GLsizei>(log.size()), &written,
                         log.data());
  if (written >= 0 && static_cast<size_t>(written) < log.size()) {
    log.resize(static_cast<size_t>(written));
  }
  LOG_ERROR("OpenGL NV12 {} shader compilation failed: {}",
            type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
  gl.delete_shader(shader);
  return 0;
}

bool IsEnabled(GLenum capability) { return glIsEnabled(capability) == GL_TRUE; }

void RestoreCapability(GLenum capability, bool enabled) {
  if (enabled) {
    glEnable(capability);
  } else {
    glDisable(capability);
  }
}

struct VertexAttribState {
  GLint enabled = GL_FALSE;
  GLint size = 4;
  GLint stride = 0;
  GLint type = GL_FLOAT;
  GLint normalized = GL_FALSE;
  GLint buffer = 0;
  void *pointer = nullptr;
};

VertexAttribState CaptureVertexAttrib(const OpenGlFunctions &gl, GLuint index) {
  VertexAttribState state;
  gl.get_vertex_attrib_iv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                          &state.enabled);
  gl.get_vertex_attrib_iv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &state.size);
  gl.get_vertex_attrib_iv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &state.stride);
  gl.get_vertex_attrib_iv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &state.type);
  gl.get_vertex_attrib_iv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,
                          &state.normalized);
  gl.get_vertex_attrib_iv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,
                          &state.buffer);
  gl.get_vertex_attrib_pointer_v(index, GL_VERTEX_ATTRIB_ARRAY_POINTER,
                                 &state.pointer);
  return state;
}

void RestoreVertexAttrib(const OpenGlFunctions &gl, GLuint index,
                         const VertexAttribState &state) {
  if (state.buffer != 0 || state.pointer != nullptr) {
    gl.bind_buffer(GL_ARRAY_BUFFER, static_cast<GLuint>(state.buffer));
    gl.vertex_attrib_pointer(index, state.size, static_cast<GLenum>(state.type),
                             static_cast<GLboolean>(state.normalized),
                             state.stride, state.pointer);
  }
  if (state.enabled == GL_TRUE) {
    gl.enable_vertex_attrib_array(index);
  } else {
    gl.disable_vertex_attrib_array(index);
  }
}

} // namespace

struct OpenGlVideoRenderer::Impl {
  OpenGlFunctions gl;
  std::shared_ptr<SharedFrameState> frames =
      std::make_shared<SharedFrameState>();
  std::atomic<bool> ready{false};
  GLuint program = 0;
  GLuint vertex_buffer = 0;
  GLuint y_texture = 0;
  GLuint uv_texture = 0;
  GLint position_location = -1;
  GLint texcoord_location = -1;
  GLint y_texture_location = -1;
  GLint uv_texture_location = -1;
  GLint target_size_location = -1;
  GLint video_rect_location = -1;
  GLint corner_radius_location = -1;
  GLint video_enabled_location = -1;
  int texture_width = 0;
  int texture_height = 0;
  std::string uploaded_stream;
  uint64_t uploaded_sequence = 0;
  GLuint cuda_upload_buffer = 0;
  std::atomic<uint64_t> submitted_frame_total{0};
  std::atomic<uint64_t> presented_frame_total{0};
  std::atomic<uint64_t> superseded_frame_total{0};
  std::atomic<uint64_t> dropped_frame_total{0};
  std::atomic<uint64_t> failed_submit_total{0};
  std::atomic<uint64_t> render_failed_total{0};
  std::atomic<uint64_t> not_selected_total{0};
  std::atomic<int64_t> last_submit_time_ns{0};
  uint64_t render_callback_total = 0;
  uint64_t render_time_total_us = 0;
  uint64_t upload_sample_total = 0;
  uint64_t upload_time_total_us = 0;
  uint64_t queue_wait_sample_total = 0;
  uint64_t queue_wait_time_total_us = 0;
  uint64_t render_gap_sample_total = 0;
  uint64_t render_gap_time_total_us = 0;
  uint64_t render_time_window_max_us = 0;
  uint64_t upload_time_window_max_us = 0;
  uint64_t queue_wait_window_max_us = 0;
  size_t queue_depth_window_max = 0;
  uint64_t render_gap_window_min_us = std::numeric_limits<uint64_t>::max();
  uint64_t render_gap_window_max_us = 0;
  std::chrono::steady_clock::time_point last_render_started;
  std::chrono::steady_clock::time_point pipeline_window_started;
  uint64_t pipeline_window_submit_start = 0;
  uint64_t pipeline_window_present_start = 0;
  uint64_t pipeline_window_render_start = 0;
  uint64_t pipeline_window_render_time_start_us = 0;
  uint64_t pipeline_window_upload_sample_start = 0;
  uint64_t pipeline_window_upload_time_start_us = 0;
  uint64_t pipeline_window_queue_wait_sample_start = 0;
  uint64_t pipeline_window_queue_wait_time_start_us = 0;
  uint64_t pipeline_window_render_gap_sample_start = 0;
  uint64_t pipeline_window_render_gap_time_start_us = 0;

  void ResetVideoPipeline() {
    submitted_frame_total.store(0, std::memory_order_relaxed);
    presented_frame_total.store(0, std::memory_order_relaxed);
    superseded_frame_total.store(0, std::memory_order_relaxed);
    dropped_frame_total.store(0, std::memory_order_relaxed);
    failed_submit_total.store(0, std::memory_order_relaxed);
    render_failed_total.store(0, std::memory_order_relaxed);
    not_selected_total.store(0, std::memory_order_relaxed);
    last_submit_time_ns.store(0, std::memory_order_relaxed);
    render_callback_total = 0;
    render_time_total_us = 0;
    upload_sample_total = 0;
    upload_time_total_us = 0;
    queue_wait_sample_total = 0;
    queue_wait_time_total_us = 0;
    render_gap_sample_total = 0;
    render_gap_time_total_us = 0;
    render_time_window_max_us = 0;
    upload_time_window_max_us = 0;
    queue_wait_window_max_us = 0;
    queue_depth_window_max = 0;
    render_gap_window_min_us = std::numeric_limits<uint64_t>::max();
    render_gap_window_max_us = 0;
    last_render_started = {};
    pipeline_window_started = {};
    pipeline_window_submit_start = 0;
    pipeline_window_present_start = 0;
    pipeline_window_render_start = 0;
    pipeline_window_render_time_start_us = 0;
    pipeline_window_upload_sample_start = 0;
    pipeline_window_upload_time_start_us = 0;
    pipeline_window_queue_wait_sample_start = 0;
    pipeline_window_queue_wait_time_start_us = 0;
    pipeline_window_render_gap_sample_start = 0;
    pipeline_window_render_gap_time_start_us = 0;
  }

  void RecordRenderStart(std::chrono::steady_clock::time_point now) {
    if (last_render_started != std::chrono::steady_clock::time_point{}) {
      const uint64_t gap_us = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(
              now - last_render_started)
              .count());
      ++render_gap_sample_total;
      render_gap_time_total_us += gap_us;
      render_gap_window_min_us = std::min(render_gap_window_min_us, gap_us);
      render_gap_window_max_us = std::max(render_gap_window_max_us, gap_us);
    }
    last_render_started = now;
  }

  void MaybeLogVideoPipeline(std::string_view remote_id) {
    const auto now = std::chrono::steady_clock::now();
    const uint64_t submitted =
        submitted_frame_total.load(std::memory_order_relaxed);
    const uint64_t presented =
        presented_frame_total.load(std::memory_order_relaxed);
    if (pipeline_window_started == std::chrono::steady_clock::time_point{}) {
      pipeline_window_started = now;
      pipeline_window_submit_start = 0;
      pipeline_window_present_start = 0;
      return;
    }

    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - pipeline_window_started)
            .count();
    if (elapsed_ms < 1000) {
      return;
    }

    const uint64_t submit_delta = submitted - pipeline_window_submit_start;
    const uint64_t present_delta = presented - pipeline_window_present_start;
    const uint64_t render_delta =
        render_callback_total - pipeline_window_render_start;
    const uint64_t render_time_delta_us =
        render_time_total_us - pipeline_window_render_time_start_us;
    const uint64_t upload_sample_delta =
        upload_sample_total - pipeline_window_upload_sample_start;
    const uint64_t upload_time_delta_us =
        upload_time_total_us - pipeline_window_upload_time_start_us;
    const uint64_t queue_wait_sample_delta =
        queue_wait_sample_total - pipeline_window_queue_wait_sample_start;
    const uint64_t queue_wait_time_delta_us =
        queue_wait_time_total_us - pipeline_window_queue_wait_time_start_us;
    const uint64_t render_gap_sample_delta =
        render_gap_sample_total - pipeline_window_render_gap_sample_start;
    const uint64_t render_gap_time_delta_us =
        render_gap_time_total_us - pipeline_window_render_gap_time_start_us;
    const uint64_t submit_fps = submit_delta * 1000 / elapsed_ms;
    const uint64_t present_fps = present_delta * 1000 / elapsed_ms;
    const uint64_t render_fps = render_delta * 1000 / elapsed_ms;
    const uint64_t render_avg_us =
        render_delta > 0 ? render_time_delta_us / render_delta : 0;
    const uint64_t upload_avg_us = upload_sample_delta > 0
                                       ? upload_time_delta_us / upload_sample_delta
                                       : 0;
    const uint64_t queue_wait_avg_us =
        queue_wait_sample_delta > 0
            ? queue_wait_time_delta_us / queue_wait_sample_delta
            : 0;
    const uint64_t render_gap_avg_us =
        render_gap_sample_delta > 0
            ? render_gap_time_delta_us / render_gap_sample_delta
            : 0;
    const uint64_t render_gap_min_us =
        render_gap_window_min_us == std::numeric_limits<uint64_t>::max()
            ? 0
            : render_gap_window_min_us;
    LOG_INFO("VIDEO_PIPELINE_RENDER stream=[{}] interval_ms={} submit_fps={} "
             "present_fps={} submit_total={} present_total={} "
             "superseded_total={} dropped_total={} failed_submit_total={} "
             "render_failed_total={} not_selected_total={} render_fps={} "
             "render_avg_us={} render_max_us={} upload_avg_us={} "
             "upload_max_us={} queue_wait_avg_us={} queue_wait_max_us={} "
             "queue_depth_max={} render_gap_avg_us={} render_gap_min_us={} "
             "render_gap_max_us={}",
             remote_id, elapsed_ms, submit_fps, present_fps, submitted,
             presented, superseded_frame_total.load(std::memory_order_relaxed),
             dropped_frame_total.load(std::memory_order_relaxed),
             failed_submit_total.load(std::memory_order_relaxed),
             render_failed_total.load(std::memory_order_relaxed),
             not_selected_total.load(std::memory_order_relaxed), render_fps,
             render_avg_us, render_time_window_max_us, upload_avg_us,
             upload_time_window_max_us, queue_wait_avg_us,
             queue_wait_window_max_us, queue_depth_window_max,
             render_gap_avg_us, render_gap_min_us, render_gap_window_max_us);

    pipeline_window_started = now;
    pipeline_window_submit_start = submitted;
    pipeline_window_present_start = presented;
    pipeline_window_render_start = render_callback_total;
    pipeline_window_render_time_start_us = render_time_total_us;
    pipeline_window_upload_sample_start = upload_sample_total;
    pipeline_window_upload_time_start_us = upload_time_total_us;
    pipeline_window_queue_wait_sample_start = queue_wait_sample_total;
    pipeline_window_queue_wait_time_start_us = queue_wait_time_total_us;
    pipeline_window_render_gap_sample_start = render_gap_sample_total;
    pipeline_window_render_gap_time_start_us = render_gap_time_total_us;
    render_time_window_max_us = 0;
    upload_time_window_max_us = 0;
    queue_wait_window_max_us = 0;
    queue_depth_window_max = 0;
    render_gap_window_min_us = std::numeric_limits<uint64_t>::max();
    render_gap_window_max_us = 0;
  }

  VideoRenderer::SubmitResult
  RecordSubmitResult(std::string_view remote_id,
                     VideoRenderer::SubmitResult result,
                     bool superseded = false) {
    switch (result) {
    case VideoRenderer::SubmitResult::submitted:
      submitted_frame_total.fetch_add(1, std::memory_order_relaxed);
      last_submit_time_ns.store(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count(),
          std::memory_order_release);
      if (superseded) {
        superseded_frame_total.fetch_add(1, std::memory_order_relaxed);
      }
      break;
    case VideoRenderer::SubmitResult::dropped:
      dropped_frame_total.fetch_add(1, std::memory_order_relaxed);
      break;
    case VideoRenderer::SubmitResult::failed:
      failed_submit_total.fetch_add(1, std::memory_order_relaxed);
      break;
    case VideoRenderer::SubmitResult::not_selected:
      not_selected_total.fetch_add(1, std::memory_order_relaxed);
      break;
    }
    return result;
  }

  void RecordRenderResult(std::string_view remote_id, bool presented_new_frame,
                          bool failed_new_frame, uint64_t render_time_us,
                          uint64_t upload_time_us, uint64_t queue_wait_us,
                          size_t queue_depth, bool sampled_upload) {
    ++render_callback_total;
    render_time_total_us += render_time_us;
    render_time_window_max_us =
        std::max(render_time_window_max_us, render_time_us);
    if (sampled_upload) {
      ++upload_sample_total;
      upload_time_total_us += upload_time_us;
      upload_time_window_max_us =
          std::max(upload_time_window_max_us, upload_time_us);
      ++queue_wait_sample_total;
      queue_wait_time_total_us += queue_wait_us;
      queue_wait_window_max_us =
          std::max(queue_wait_window_max_us, queue_wait_us);
      queue_depth_window_max = std::max(queue_depth_window_max, queue_depth);
    }
    if (presented_new_frame) {
      presented_frame_total.fetch_add(1, std::memory_order_relaxed);
    }
    if (failed_new_frame) {
      render_failed_total.fetch_add(1, std::memory_order_relaxed);
    }
    MaybeLogVideoPipeline(remote_id);
  }

#if defined(_WIN32) && USE_CUDA
  size_t cuda_upload_capacity = 0;
  CUgraphicsResource cuda_upload_resource = nullptr;
  CUcontext cuda_context = nullptr;
  WindowsNativeFrameRef cuda_context_owner;
  bool cuda_interop_disabled = false;
  bool cuda_fallback_logged = false;
  bool cuda_native_logged = false;

  void ReleaseCudaRegistration() {
    if (cuda_upload_resource && cuda_context &&
        minirtc::cuCtxPushCurrent_ld(cuda_context) == CUDA_SUCCESS) {
      const CUresult unregister_result =
          minirtc::cuGraphicsUnregisterResource_ld(cuda_upload_resource);
      CUcontext popped_context = nullptr;
      const CUresult pop_result = minirtc::cuCtxPopCurrent_ld(&popped_context);
      if (unregister_result != CUDA_SUCCESS || pop_result != CUDA_SUCCESS) {
        LOG_WARN("CUDA/OpenGL unregister failed, cuda={}, pop={}",
                 static_cast<int>(unregister_result),
                 static_cast<int>(pop_result));
      }
    }
    cuda_upload_resource = nullptr;
    cuda_context = nullptr;
    cuda_context_owner.Reset();
  }

  bool EnsureCudaUploadBuffer(const XWindowsVideoFrame &frame) {
    if (cuda_interop_disabled || !frame.device_context || frame.size == 0 ||
        minirtc::LoadCudaGraphicsInterop() != 0) {
      cuda_interop_disabled = true;
      return false;
    }

    auto frame_context = static_cast<CUcontext>(frame.device_context);
    if (cuda_upload_resource && cuda_context != frame_context) {
      ReleaseCudaRegistration();
    }

    if (cuda_upload_buffer == 0) {
      gl.gen_buffers(1, &cuda_upload_buffer);
    }
    if (cuda_upload_buffer == 0) {
      cuda_interop_disabled = true;
      return false;
    }

    if (cuda_upload_capacity < frame.size) {
      ReleaseCudaRegistration();
      GLint previous_unpack_buffer = 0;
      glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &previous_unpack_buffer);
      gl.bind_buffer(GL_PIXEL_UNPACK_BUFFER, cuda_upload_buffer);
      gl.buffer_data(GL_PIXEL_UNPACK_BUFFER,
                     static_cast<GLsizeiptr>(frame.size), nullptr,
                     GL_STREAM_DRAW);
      gl.bind_buffer(GL_PIXEL_UNPACK_BUFFER,
                     static_cast<GLuint>(previous_unpack_buffer));
      if (glGetError() != GL_NO_ERROR) {
        cuda_interop_disabled = true;
        return false;
      }
      cuda_upload_capacity = frame.size;
    }

    if (!cuda_upload_resource) {
      if (minirtc::cuCtxPushCurrent_ld(frame_context) != CUDA_SUCCESS) {
        return false;
      }
      const CUresult register_result = minirtc::cuGraphicsGLRegisterBuffer_ld(
          &cuda_upload_resource, cuda_upload_buffer,
          CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD);
      CUcontext popped_context = nullptr;
      const CUresult pop_result = minirtc::cuCtxPopCurrent_ld(&popped_context);
      if (register_result != CUDA_SUCCESS || pop_result != CUDA_SUCCESS) {
        cuda_upload_resource = nullptr;
        cuda_interop_disabled = true;
        LOG_WARN("CUDA/OpenGL buffer registration failed, cuda={}, pop={}",
                 static_cast<int>(register_result),
                 static_cast<int>(pop_result));
        return false;
      }
      cuda_context = frame_context;
    }
    return true;
  }

  bool CopyCudaFrameToUploadBuffer(const WindowsNativeFrameRef &native_frame) {
    const XWindowsVideoFrame *frame = native_frame.Get();
    if (!frame || !EnsureCudaUploadBuffer(*frame)) {
      return false;
    }
    cuda_context_owner = native_frame;

    if (minirtc::cuCtxPushCurrent_ld(cuda_context) != CUDA_SUCCESS) {
      return false;
    }

    CUgraphicsResource resource = cuda_upload_resource;
    CUresult result = minirtc::cuGraphicsMapResources_ld(1, &resource, nullptr);
    bool mapped = result == CUDA_SUCCESS;
    CUdeviceptr destination = 0;
    size_t mapped_size = 0;
    if (mapped) {
      result = minirtc::cuGraphicsResourceGetMappedPointer_ld(
          &destination, &mapped_size, cuda_upload_resource);
    }
    if (result == CUDA_SUCCESS && mapped_size < frame->size) {
      result = CUDA_ERROR_INVALID_VALUE;
    }
    if (result == CUDA_SUCCESS) {
      CUDA_MEMCPY2D copy{};
      copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
      copy.srcDevice = static_cast<CUdeviceptr>(frame->y_device_pointer);
      copy.srcPitch = frame->y_stride;
      copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
      copy.dstDevice = destination;
      copy.dstPitch = frame->width;
      copy.WidthInBytes = frame->width;
      copy.Height = frame->height;
      result = minirtc::cuMemcpy2DAsync_ld(&copy, nullptr);
      if (result == CUDA_SUCCESS) {
        copy.srcDevice = static_cast<CUdeviceptr>(frame->uv_device_pointer);
        copy.srcPitch = frame->uv_stride;
        copy.dstDevice = destination +
                         static_cast<CUdeviceptr>(frame->width) * frame->height;
        copy.Height = frame->height / 2U;
        result = minirtc::cuMemcpy2DAsync_ld(&copy, nullptr);
      }
    }
    if (mapped) {
      const CUresult unmap_result =
          minirtc::cuGraphicsUnmapResources_ld(1, &resource, nullptr);
      if (result == CUDA_SUCCESS) {
        result = unmap_result;
      }
    }
    CUcontext popped_context = nullptr;
    const CUresult pop_result = minirtc::cuCtxPopCurrent_ld(&popped_context);
    return result == CUDA_SUCCESS && pop_result == CUDA_SUCCESS;
  }
#endif

  void ResetGlHandles() {
    program = 0;
    vertex_buffer = 0;
    y_texture = 0;
    uv_texture = 0;
    position_location = -1;
    texcoord_location = -1;
    y_texture_location = -1;
    uv_texture_location = -1;
    target_size_location = -1;
    video_rect_location = -1;
    corner_radius_location = -1;
    video_enabled_location = -1;
    texture_width = 0;
    texture_height = 0;
    uploaded_stream.clear();
    uploaded_sequence = 0;
  }
};

OpenGlVideoRenderer::OpenGlVideoRenderer() : impl_(std::make_unique<Impl>()) {}

OpenGlVideoRenderer::~OpenGlVideoRenderer() = default;

bool OpenGlVideoRenderer::Setup() {
  if (IsReady()) {
    return true;
  }
  impl_->ready.store(false, std::memory_order_release);
  impl_->ResetGlHandles();
  if (!impl_->gl.Load()) {
    LOG_WARN("OpenGL NV12 underlay unavailable: required functions missing");
    return false;
  }

  static constexpr char kGlesVertexShader[] = R"glsl(
#version 100
attribute vec2 position;
attribute vec2 texcoord;
varying vec2 video_texcoord;
void main() {
  video_texcoord = texcoord;
  gl_Position = vec4(position, 0.0, 1.0);
}
)glsl";
  static constexpr char kGlesFragmentShader[] = R"glsl(
#version 100
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D y_texture;
uniform sampler2D uv_texture;
uniform vec2 target_size;
uniform vec4 video_rect;
uniform float corner_radius;
uniform float video_enabled;
varying vec2 video_texcoord;
void main() {
  vec2 surface_point = video_texcoord * target_size;
  vec3 rgb = vec3(0.0);
  if (video_enabled > 0.5 &&
      surface_point.x >= video_rect.x &&
      surface_point.x <= video_rect.x + video_rect.z &&
      surface_point.y >= video_rect.y &&
      surface_point.y <= video_rect.y + video_rect.w) {
    vec2 frame_texcoord =
        (surface_point - video_rect.xy) / video_rect.zw;
    float y = 1.16438356 *
              (texture2D(y_texture, frame_texcoord).r - 16.0 / 255.0);
    vec2 uv =
        texture2D(uv_texture, frame_texcoord).ra - vec2(0.5, 0.5);
    rgb = clamp(vec3(y + 1.59602678 * uv.y,
                     y - 0.39176229 * uv.x - 0.81296764 * uv.y,
                     y + 2.01723214 * uv.x),
                0.0, 1.0);
  }
  float coverage = 1.0;
  if (corner_radius > 0.0) {
    vec2 half_size = target_size * 0.5;
    vec2 corner = abs(surface_point - half_size) -
                  (half_size - vec2(corner_radius));
    float distance_to_edge =
        length(max(corner, vec2(0.0))) +
        min(max(corner.x, corner.y), 0.0) - corner_radius;
    coverage = clamp(0.5 - distance_to_edge, 0.0, 1.0);
  }
  gl_FragColor = vec4(rgb * coverage, coverage);
}
)glsl";
  static constexpr char kDesktopVertexShader[] = R"glsl(
#version 110
attribute vec2 position;
attribute vec2 texcoord;
varying vec2 video_texcoord;
void main() {
  video_texcoord = texcoord;
  gl_Position = vec4(position, 0.0, 1.0);
}
)glsl";
  static constexpr char kDesktopFragmentShader[] = R"glsl(
#version 110
uniform sampler2D y_texture;
uniform sampler2D uv_texture;
uniform vec2 target_size;
uniform vec4 video_rect;
uniform float corner_radius;
uniform float video_enabled;
varying vec2 video_texcoord;
void main() {
  vec2 surface_point = video_texcoord * target_size;
  vec3 rgb = vec3(0.0);
  if (video_enabled > 0.5 &&
      surface_point.x >= video_rect.x &&
      surface_point.x <= video_rect.x + video_rect.z &&
      surface_point.y >= video_rect.y &&
      surface_point.y <= video_rect.y + video_rect.w) {
    vec2 frame_texcoord =
        (surface_point - video_rect.xy) / video_rect.zw;
    float y = 1.16438356 *
              (texture2D(y_texture, frame_texcoord).r - 16.0 / 255.0);
    vec2 uv =
        texture2D(uv_texture, frame_texcoord).ra - vec2(0.5, 0.5);
    rgb = clamp(vec3(y + 1.59602678 * uv.y,
                     y - 0.39176229 * uv.x - 0.81296764 * uv.y,
                     y + 2.01723214 * uv.x),
                0.0, 1.0);
  }
  float coverage = 1.0;
  if (corner_radius > 0.0) {
    vec2 half_size = target_size * 0.5;
    vec2 corner = abs(surface_point - half_size) -
                  (half_size - vec2(corner_radius));
    float distance_to_edge =
        length(max(corner, vec2(0.0))) +
        min(max(corner.x, corner.y), 0.0) - corner_radius;
    coverage = clamp(0.5 - distance_to_edge, 0.0, 1.0);
  }
  gl_FragColor = vec4(rgb * coverage, coverage);
}
)glsl";

  const auto *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
  const bool is_gles = version && std::strstr(version, "OpenGL ES") != nullptr;
  const char *vertex_source =
      is_gles ? kGlesVertexShader : kDesktopVertexShader;
  const char *fragment_source =
      is_gles ? kGlesFragmentShader : kDesktopFragmentShader;

  const GLuint vertex_shader =
      CompileShader(impl_->gl, GL_VERTEX_SHADER, vertex_source);
  const GLuint fragment_shader =
      CompileShader(impl_->gl, GL_FRAGMENT_SHADER, fragment_source);
  if (vertex_shader == 0 || fragment_shader == 0) {
    if (vertex_shader != 0)
      impl_->gl.delete_shader(vertex_shader);
    if (fragment_shader != 0)
      impl_->gl.delete_shader(fragment_shader);
    return false;
  }

  impl_->program = impl_->gl.create_program();
  impl_->gl.attach_shader(impl_->program, vertex_shader);
  impl_->gl.attach_shader(impl_->program, fragment_shader);
  impl_->gl.link_program(impl_->program);
  impl_->gl.delete_shader(vertex_shader);
  impl_->gl.delete_shader(fragment_shader);

  GLint linked = GL_FALSE;
  impl_->gl.get_program_iv(impl_->program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    GLint log_length = 0;
    impl_->gl.get_program_iv(impl_->program, GL_INFO_LOG_LENGTH, &log_length);
    std::string log(static_cast<size_t>(std::max(log_length, 1)), '\0');
    GLsizei written = 0;
    impl_->gl.get_program_info_log(
        impl_->program, static_cast<GLsizei>(log.size()), &written, log.data());
    if (written >= 0 && static_cast<size_t>(written) < log.size()) {
      log.resize(static_cast<size_t>(written));
    }
    LOG_ERROR("OpenGL NV12 program link failed: {}", log);
    impl_->gl.delete_program(impl_->program);
    impl_->ResetGlHandles();
    return false;
  }

  impl_->position_location =
      impl_->gl.get_attrib_location(impl_->program, "position");
  impl_->texcoord_location =
      impl_->gl.get_attrib_location(impl_->program, "texcoord");
  impl_->y_texture_location =
      impl_->gl.get_uniform_location(impl_->program, "y_texture");
  impl_->uv_texture_location =
      impl_->gl.get_uniform_location(impl_->program, "uv_texture");
  impl_->target_size_location =
      impl_->gl.get_uniform_location(impl_->program, "target_size");
  impl_->video_rect_location =
      impl_->gl.get_uniform_location(impl_->program, "video_rect");
  impl_->corner_radius_location =
      impl_->gl.get_uniform_location(impl_->program, "corner_radius");
  impl_->video_enabled_location =
      impl_->gl.get_uniform_location(impl_->program, "video_enabled");
  if (impl_->position_location < 0 || impl_->texcoord_location < 0 ||
      impl_->y_texture_location < 0 || impl_->uv_texture_location < 0 ||
      impl_->target_size_location < 0 || impl_->video_rect_location < 0 ||
      impl_->corner_radius_location < 0 || impl_->video_enabled_location < 0) {
    LOG_ERROR("OpenGL NV12 program is missing required shader bindings");
    impl_->gl.delete_program(impl_->program);
    impl_->ResetGlHandles();
    return false;
  }

  static constexpr GLfloat kVertices[] = {
      -1.0f, 1.0f,  0.0f, 0.0f, // top-left
      -1.0f, -1.0f, 0.0f, 1.0f, // bottom-left
      1.0f,  1.0f,  1.0f, 0.0f, // top-right
      1.0f,  -1.0f, 1.0f, 1.0f, // bottom-right
  };
  GLint previous_array_buffer = 0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_array_buffer);
  impl_->gl.gen_buffers(1, &impl_->vertex_buffer);
  impl_->gl.bind_buffer(GL_ARRAY_BUFFER, impl_->vertex_buffer);
  impl_->gl.buffer_data(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices,
                        GL_STATIC_DRAW);
  impl_->gl.bind_buffer(GL_ARRAY_BUFFER,
                        static_cast<GLuint>(previous_array_buffer));

  GLint previous_active_texture = GL_TEXTURE0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &previous_active_texture);
  impl_->gl.active_texture(GL_TEXTURE0);
  GLint previous_texture_0 = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture_0);
  impl_->gl.active_texture(GL_TEXTURE1);
  GLint previous_texture_1 = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture_1);
  while (glGetError() != GL_NO_ERROR) {
  }
  glGenTextures(1, &impl_->y_texture);
  glGenTextures(1, &impl_->uv_texture);
  for (const auto [unit, texture] :
       {std::pair{GL_TEXTURE0, impl_->y_texture},
        std::pair{GL_TEXTURE1, impl_->uv_texture}}) {
    impl_->gl.active_texture(unit);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  impl_->gl.active_texture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture_0));
  impl_->gl.active_texture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture_1));
  impl_->gl.active_texture(static_cast<GLenum>(previous_active_texture));

  const GLenum setup_error = glGetError();
  if (impl_->program == 0 || impl_->vertex_buffer == 0 ||
      impl_->y_texture == 0 || impl_->uv_texture == 0 ||
      setup_error != GL_NO_ERROR) {
    LOG_ERROR("OpenGL NV12 resource setup failed, error={}", setup_error);
    Teardown();
    return false;
  }

  impl_->ready.store(true, std::memory_order_release);
  LOG_INFO("OpenGL NV12 underlay initialized ({})",
           version ? version : "unknown OpenGL version");
  return true;
}

void OpenGlVideoRenderer::Teardown() {
  impl_->ready.store(false, std::memory_order_release);
#if defined(_WIN32) && USE_CUDA
  impl_->ReleaseCudaRegistration();
  if (impl_->cuda_upload_buffer != 0 && impl_->gl.delete_buffers) {
    impl_->gl.delete_buffers(1, &impl_->cuda_upload_buffer);
  }
  impl_->cuda_upload_buffer = 0;
  impl_->cuda_upload_capacity = 0;
  impl_->cuda_interop_disabled = false;
  impl_->cuda_fallback_logged = false;
  impl_->cuda_native_logged = false;
#endif
  if (impl_->y_texture != 0)
    glDeleteTextures(1, &impl_->y_texture);
  if (impl_->uv_texture != 0)
    glDeleteTextures(1, &impl_->uv_texture);
  if (impl_->vertex_buffer != 0 && impl_->gl.delete_buffers) {
    impl_->gl.delete_buffers(1, &impl_->vertex_buffer);
  }
  if (impl_->program != 0 && impl_->gl.delete_program) {
    impl_->gl.delete_program(impl_->program);
  }
  {
    std::lock_guard lock(impl_->frames->mutex);
    for (auto &slot : impl_->frames->slots) {
      slot.bytes.clear();
#if defined(_WIN32)
      slot.native_frame.Reset();
#endif
      slot.valid = false;
      slot.use = SlotUse::available;
    }
  }
  impl_->ResetGlHandles();
}

bool OpenGlVideoRenderer::IsReady() const {
  return impl_->ready.load(std::memory_order_acquire);
}

bool OpenGlVideoRenderer::IsActive() const { return IsReady(); }

bool OpenGlVideoRenderer::ShouldContinueRendering() const {
  if (!IsReady()) {
    return false;
  }
  const int64_t last_submit_ns =
      impl_->last_submit_time_ns.load(std::memory_order_acquire);
  if (last_submit_ns == 0) {
    return false;
  }
  const int64_t now_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();
  return now_ns - last_submit_ns <=
         std::chrono::duration_cast<std::chrono::nanoseconds>(
             kActiveRenderWindow)
             .count();
}

bool OpenGlVideoRenderer::SetSelectedStream(std::string remote_id) {
  std::lock_guard lock(impl_->frames->mutex);
  if (impl_->frames->selected_stream == remote_id) {
    return false;
  }
  impl_->frames->selected_stream = std::move(remote_id);
  for (auto &slot : impl_->frames->slots) {
    if (slot.use == SlotUse::pending &&
        slot.remote_id != impl_->frames->selected_stream) {
      slot.use = SlotUse::available;
    }
  }
  impl_->ResetVideoPipeline();
  return true;
}

void OpenGlVideoRenderer::DiscardStream(std::string_view remote_id) {
  std::lock_guard lock(impl_->frames->mutex);
  for (auto &slot : impl_->frames->slots) {
    if (slot.remote_id == remote_id && slot.use != SlotUse::uploading) {
      slot.valid = false;
      slot.use = SlotUse::available;
      slot.remote_id.clear();
      slot.bytes.clear();
#if defined(_WIN32)
      slot.native_frame.Reset();
#endif
    }
  }
  if (impl_->frames->selected_stream == remote_id) {
    impl_->frames->selected_stream.clear();
  }
}

OpenGlVideoRenderer::SubmitResult
OpenGlVideoRenderer::SubmitNv12(std::string_view remote_id, const uint8_t *data,
                                size_t size, int width, int height) {
  return SubmitNv12Internal(remote_id, data, size, width, height, true);
}

OpenGlVideoRenderer::SubmitResult
OpenGlVideoRenderer::SubmitCachedNv12(std::string_view remote_id,
                                      const uint8_t *data, size_t size,
                                      int width, int height) {
  return SubmitNv12Internal(remote_id, data, size, width, height, false);
}

OpenGlVideoRenderer::SubmitResult OpenGlVideoRenderer::SubmitNv12Internal(
    std::string_view remote_id, const uint8_t *data, size_t size, int width,
    int height, bool replace_pending) {
  if (!IsReady()) {
    return impl_->RecordSubmitResult(remote_id, SubmitResult::failed);
  }
  if (data == nullptr || width <= 0 || height <= 0 || (width & 1) != 0 ||
      (height & 1) != 0) {
    return impl_->RecordSubmitResult(remote_id, SubmitResult::failed);
  }
  const size_t y_size = static_cast<size_t>(width) * height;
  const size_t required_size = y_size + y_size / 2;
  if (size < required_size) {
    return impl_->RecordSubmitResult(remote_id, SubmitResult::failed);
  }

  auto &frames = *impl_->frames;
  std::lock_guard lock(frames.mutex);
  if (!IsReady()) {
    return impl_->RecordSubmitResult(remote_id, SubmitResult::failed);
  }
  SubmitResult selection_result = SubmitResult::failed;
  FrameSlot *target = SelectSubmissionSlot(frames, remote_id, replace_pending,
                                           &selection_result);
  if (!target) {
    return impl_->RecordSubmitResult(remote_id, selection_result);
  }
  const bool superseded = target->use == SlotUse::pending;

  target->bytes.resize(required_size);
  std::memcpy(target->bytes.data(), data, required_size);
#if defined(_WIN32)
  target->native_frame.Reset();
#endif
  target->width = width;
  target->height = height;
  target->remote_id.assign(remote_id);
  target->sequence = frames.next_sequence++;
  target->submitted_at = std::chrono::steady_clock::now();
  target->use = SlotUse::pending;
  target->valid = true;
  return impl_->RecordSubmitResult(remote_id, SubmitResult::submitted,
                                   superseded);
}

OpenGlVideoRenderer::SubmitResult
OpenGlVideoRenderer::SubmitNativeFrame(std::string_view remote_id,
                                       const XVideoFrame &frame) {
#if defined(_WIN32)
  if (!IsReady()) {
    return impl_->RecordSubmitResult(remote_id, SubmitResult::failed);
  }
  const XWindowsVideoFrame *native = GetWindowsNativeFrame(frame);
  if (!native || native->width > static_cast<uint32_t>(INT_MAX) ||
      native->height > static_cast<uint32_t>(INT_MAX)) {
    return impl_->RecordSubmitResult(remote_id, SubmitResult::failed);
  }

  auto &frames = *impl_->frames;
  std::lock_guard lock(frames.mutex);
  if (!IsReady()) {
    return impl_->RecordSubmitResult(remote_id, SubmitResult::failed);
  }
  SubmitResult selection_result = SubmitResult::failed;
  FrameSlot *target =
      SelectSubmissionSlot(frames, remote_id, true, &selection_result);
  if (!target) {
    return impl_->RecordSubmitResult(remote_id, selection_result);
  }
  const bool superseded = target->use == SlotUse::pending;

  WindowsNativeFrameRef retained(native);
  if (!retained) {
    return impl_->RecordSubmitResult(remote_id, SubmitResult::failed);
  }
  target->bytes.clear();
  target->native_frame = std::move(retained);
  target->width = static_cast<int>(native->width);
  target->height = static_cast<int>(native->height);
  target->remote_id.assign(remote_id);
  target->sequence = frames.next_sequence++;
  target->submitted_at = std::chrono::steady_clock::now();
  target->use = SlotUse::pending;
  target->valid = true;
  return impl_->RecordSubmitResult(remote_id, SubmitResult::submitted,
                                   superseded);
#else
  (void)remote_id;
  (void)frame;
  return SubmitResult::failed;
#endif
}

OpenGlVideoRenderer::RenderOutcome
OpenGlVideoRenderer::RenderLatest(std::string_view remote_id, int target_width,
                                  int target_height, int top_inset_pixels,
                                  int corner_radius_pixels) {
  if (!IsReady() || target_width <= 0 || target_height <= 0) {
    return {RenderResult::failed};
  }
  const auto render_started = std::chrono::steady_clock::now();
  impl_->RecordRenderStart(render_started);

  size_t slot_index = kFrameSlotCount;
  uint64_t sequence = 0;
  uint64_t queue_wait_us = 0;
  size_t queue_depth = 0;
  int source_width = 0;
  int source_height = 0;
  const unsigned char *y_data = nullptr;
  const unsigned char *uv_data = nullptr;
#if defined(_WIN32)
  WindowsNativeFrameRef active_native_frame;
#endif
  {
    std::lock_guard lock(impl_->frames->mutex);
    for (size_t index = 0; index < impl_->frames->slots.size(); ++index) {
      const auto &slot = impl_->frames->slots[index];
      if (slot.valid && slot.use == SlotUse::pending &&
          slot.remote_id == remote_id) {
        ++queue_depth;
        if (slot_index == kFrameSlotCount || slot.sequence < sequence) {
          slot_index = index;
          sequence = slot.sequence;
        }
      }
    }
    if (slot_index != kFrameSlotCount) {
      auto &selected = impl_->frames->slots[slot_index];
      selected.use = SlotUse::uploading;
      source_width = selected.width;
      source_height = selected.height;
      if (selected.submitted_at !=
          std::chrono::steady_clock::time_point{}) {
        queue_wait_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - selected.submitted_at)
                .count());
      }
#if defined(_WIN32)
      active_native_frame = selected.native_frame;
#endif
      if (!selected.bytes.empty()) {
        const size_t y_size = static_cast<size_t>(source_width) * source_height;
        y_data = selected.bytes.data();
        uv_data = selected.bytes.data() + y_size;
      }
    }
  }

  GLint previous_program = 0;
  GLint previous_active_texture = GL_TEXTURE0;
  GLint previous_array_buffer = 0;
  GLint previous_unpack_buffer = 0;
  GLint previous_unpack_alignment = 4;
  GLint previous_viewport[4] = {};
  GLint previous_scissor_box[4] = {};
  GLfloat previous_clear_color[4] = {};
  GLboolean previous_color_mask[4] = {};
  glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &previous_active_texture);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_array_buffer);
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &previous_unpack_buffer);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_unpack_alignment);
  glGetIntegerv(GL_VIEWPORT, previous_viewport);
  glGetIntegerv(GL_SCISSOR_BOX, previous_scissor_box);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, previous_clear_color);
  glGetBooleanv(GL_COLOR_WRITEMASK, previous_color_mask);
  const bool blend_enabled = IsEnabled(GL_BLEND);
  const bool cull_enabled = IsEnabled(GL_CULL_FACE);
  const bool depth_enabled = IsEnabled(GL_DEPTH_TEST);
  const bool scissor_enabled = IsEnabled(GL_SCISSOR_TEST);
  const bool stencil_enabled = IsEnabled(GL_STENCIL_TEST);

  impl_->gl.active_texture(GL_TEXTURE0);
  GLint previous_texture_0 = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture_0);
  impl_->gl.active_texture(GL_TEXTURE1);
  GLint previous_texture_1 = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture_1);

  const auto position_state = CaptureVertexAttrib(
      impl_->gl, static_cast<GLuint>(impl_->position_location));
  const auto texcoord_state = CaptureVertexAttrib(
      impl_->gl, static_cast<GLuint>(impl_->texcoord_location));

  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glViewport(0, 0, target_width, target_height);
  // The Slint items above this underlay are transparent around the custom
  // window corners. Keep those pixels transparent so the compositor can show
  // the desktop instead of an opaque black framebuffer clear.
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  bool upload_succeeded = true;
  uint64_t upload_time_us = 0;
  if (slot_index != kFrameSlotCount) {
    const auto upload_started = std::chrono::steady_clock::now();
    while (glGetError() != GL_NO_ERROR) {
    }
    const size_t y_size = static_cast<size_t>(source_width) * source_height;
    const size_t required_size = y_size + y_size / 2U;
    bool use_pixel_unpack_buffer = false;
    std::vector<unsigned char> cpu_fallback;
#if defined(_WIN32)
    if (const XWindowsVideoFrame *native = active_native_frame.Get()) {
      if (native->memory_type == XWindowsVideoFrameMemoryCpu &&
          native->y_stride == native->width &&
          native->uv_stride == native->width) {
        y_data = native->y_plane;
        uv_data = native->uv_plane;
      } else if (native->memory_type == XWindowsVideoFrameMemoryCuda) {
#if USE_CUDA
        use_pixel_unpack_buffer =
            impl_->CopyCudaFrameToUploadBuffer(active_native_frame);
        if (use_pixel_unpack_buffer && !impl_->cuda_native_logged) {
          LOG_INFO("Windows native NV12 using CUDA/OpenGL device upload");
          impl_->cuda_native_logged = true;
        }
        if (!use_pixel_unpack_buffer && !impl_->cuda_fallback_logged) {
          LOG_WARN("CUDA/OpenGL native upload unavailable; falling back to "
                   "CUDA-to-CPU copy");
          impl_->cuda_fallback_logged = true;
        }
#endif
      }

      if (!use_pixel_unpack_buffer && (!y_data || !uv_data)) {
        cpu_fallback.resize(required_size);
        if (native->copy_to_cpu(native->owner, cpu_fallback.data(),
                                cpu_fallback.size()) == 0) {
          y_data = cpu_fallback.data();
          uv_data = cpu_fallback.data() + y_size;
        } else {
          upload_succeeded = false;
          LOG_WARN("Windows native NV12 CPU fallback failed");
        }
      }
    }
#endif
    if (!use_pixel_unpack_buffer && (!y_data || !uv_data)) {
      upload_succeeded = false;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    impl_->gl.active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, impl_->y_texture);
    impl_->gl.active_texture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, impl_->uv_texture);
    if (use_pixel_unpack_buffer) {
      impl_->gl.bind_buffer(GL_PIXEL_UNPACK_BUFFER, impl_->cuda_upload_buffer);
    }
    const void *y_pixels =
        use_pixel_unpack_buffer ? nullptr : static_cast<const void *>(y_data);
    const void *uv_pixels =
        use_pixel_unpack_buffer
            ? reinterpret_cast<const void *>(static_cast<uintptr_t>(y_size))
            : static_cast<const void *>(uv_data);
    if (upload_succeeded && (impl_->texture_width != source_width ||
                             impl_->texture_height != source_height)) {
      impl_->gl.active_texture(GL_TEXTURE0);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, source_width, source_height,
                   0, GL_LUMINANCE, GL_UNSIGNED_BYTE, y_pixels);
      impl_->gl.active_texture(GL_TEXTURE1);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, source_width / 2,
                   source_height / 2, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                   uv_pixels);
    } else if (upload_succeeded) {
      impl_->gl.active_texture(GL_TEXTURE0);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, source_width, source_height,
                      GL_LUMINANCE, GL_UNSIGNED_BYTE, y_pixels);
      impl_->gl.active_texture(GL_TEXTURE1);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, source_width / 2,
                      source_height / 2, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                      uv_pixels);
    }
    if (use_pixel_unpack_buffer) {
      impl_->gl.bind_buffer(GL_PIXEL_UNPACK_BUFFER,
                            static_cast<GLuint>(previous_unpack_buffer));
    }
    const GLenum upload_error = glGetError();
    upload_succeeded = upload_succeeded && upload_error == GL_NO_ERROR;
    if (upload_succeeded) {
      impl_->texture_width = source_width;
      impl_->texture_height = source_height;
      impl_->uploaded_stream.assign(remote_id);
      impl_->uploaded_sequence = sequence;
    } else {
      LOG_WARN("OpenGL NV12 texture upload failed, error={}", upload_error);
    }
    upload_time_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - upload_started)
            .count());
  }

  {
    std::lock_guard lock(impl_->frames->mutex);
    if (slot_index != kFrameSlotCount) {
      auto &slot = impl_->frames->slots[slot_index];
      if (slot.sequence == sequence && slot.use == SlotUse::uploading) {
        slot.use = SlotUse::available;
      }
    }
  }

  const int content_y = std::clamp(top_inset_pixels, 0, target_height);
  const int content_height = target_height - content_y;
  const bool has_video = upload_succeeded &&
                         impl_->uploaded_stream == remote_id &&
                         impl_->texture_width > 0 &&
                         impl_->texture_height > 0 && content_height > 0;
  int video_x = 0;
  int video_y = content_y;
  int video_width = target_width;
  int video_height = content_height;
  if (has_video) {
    source_width = impl_->texture_width;
    source_height = impl_->texture_height;
    sequence = impl_->uploaded_sequence;
    const double source_aspect =
        static_cast<double>(source_width) / source_height;
    const double target_aspect =
        static_cast<double>(target_width) / content_height;
    if (source_aspect > target_aspect) {
      video_height =
          std::max(1, static_cast<int>(target_width / source_aspect + 0.5));
      video_y = content_y + (content_height - video_height) / 2;
    } else {
      video_width =
          std::max(1, static_cast<int>(content_height * source_aspect + 0.5));
      video_x = (target_width - video_width) / 2;
    }
  }

  while (glGetError() != GL_NO_ERROR) {
  }
  impl_->gl.use_program(impl_->program);
  impl_->gl.active_texture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, impl_->y_texture);
  impl_->gl.uniform_1i(impl_->y_texture_location, 0);
  impl_->gl.active_texture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, impl_->uv_texture);
  impl_->gl.uniform_1i(impl_->uv_texture_location, 1);
  impl_->gl.uniform_2f(impl_->target_size_location,
                       static_cast<GLfloat>(target_width),
                       static_cast<GLfloat>(target_height));
  impl_->gl.uniform_4f(
      impl_->video_rect_location, static_cast<GLfloat>(video_x),
      static_cast<GLfloat>(video_y), static_cast<GLfloat>(video_width),
      static_cast<GLfloat>(video_height));
  impl_->gl.uniform_1f(
      impl_->corner_radius_location,
      static_cast<GLfloat>(std::clamp(
          corner_radius_pixels, 0, std::min(target_width, target_height) / 2)));
  impl_->gl.uniform_1f(impl_->video_enabled_location, has_video ? 1.0f : 0.0f);
  impl_->gl.bind_buffer(GL_ARRAY_BUFFER, impl_->vertex_buffer);
  impl_->gl.vertex_attrib_pointer(
      static_cast<GLuint>(impl_->position_location), 2, GL_FLOAT, GL_FALSE,
      4 * static_cast<GLsizei>(sizeof(GLfloat)), nullptr);
  impl_->gl.vertex_attrib_pointer(
      static_cast<GLuint>(impl_->texcoord_location), 2, GL_FLOAT, GL_FALSE,
      4 * static_cast<GLsizei>(sizeof(GLfloat)),
      reinterpret_cast<const void *>(2 * sizeof(GLfloat)));
  impl_->gl.enable_vertex_attrib_array(
      static_cast<GLuint>(impl_->position_location));
  impl_->gl.enable_vertex_attrib_array(
      static_cast<GLuint>(impl_->texcoord_location));
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  const GLenum draw_error = glGetError();
  RenderOutcome outcome{
      !upload_succeeded || draw_error != GL_NO_ERROR ? RenderResult::failed
      : has_video                                    ? RenderResult::rendered
                                                     : RenderResult::empty,
      has_video ? source_width : 0, has_video ? source_height : 0,
      has_video ? sequence : 0};

  RestoreVertexAttrib(impl_->gl, static_cast<GLuint>(impl_->position_location),
                      position_state);
  RestoreVertexAttrib(impl_->gl, static_cast<GLuint>(impl_->texcoord_location),
                      texcoord_state);
  impl_->gl.bind_buffer(GL_ARRAY_BUFFER,
                        static_cast<GLuint>(previous_array_buffer));
  impl_->gl.bind_buffer(GL_PIXEL_UNPACK_BUFFER,
                        static_cast<GLuint>(previous_unpack_buffer));
  impl_->gl.active_texture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture_0));
  impl_->gl.active_texture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture_1));
  impl_->gl.active_texture(static_cast<GLenum>(previous_active_texture));
  impl_->gl.use_program(static_cast<GLuint>(previous_program));
  glPixelStorei(GL_UNPACK_ALIGNMENT, previous_unpack_alignment);
  glViewport(previous_viewport[0], previous_viewport[1], previous_viewport[2],
             previous_viewport[3]);
  glScissor(previous_scissor_box[0], previous_scissor_box[1],
            previous_scissor_box[2], previous_scissor_box[3]);
  glClearColor(previous_clear_color[0], previous_clear_color[1],
               previous_clear_color[2], previous_clear_color[3]);
  glColorMask(previous_color_mask[0], previous_color_mask[1],
              previous_color_mask[2], previous_color_mask[3]);
  RestoreCapability(GL_BLEND, blend_enabled);
  RestoreCapability(GL_CULL_FACE, cull_enabled);
  RestoreCapability(GL_DEPTH_TEST, depth_enabled);
  RestoreCapability(GL_SCISSOR_TEST, scissor_enabled);
  RestoreCapability(GL_STENCIL_TEST, stencil_enabled);
  const GLenum restore_error = glGetError();
  if (restore_error != GL_NO_ERROR) {
    LOG_WARN("OpenGL NV12 state restoration failed, error={}", restore_error);
    if (outcome.result == RenderResult::rendered) {
      outcome.result = RenderResult::failed;
    }
  }
  const bool had_new_frame = slot_index != kFrameSlotCount;
  const uint64_t render_time_us = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - render_started)
          .count());
  impl_->RecordRenderResult(
      remote_id, had_new_frame && outcome.result == RenderResult::rendered,
      had_new_frame && outcome.result == RenderResult::failed, render_time_us,
      upload_time_us, queue_wait_us, queue_depth, had_new_frame);
  return outcome;
}

bool OpenGlVideoRenderer::CopyLatestNv12(std::string_view remote_id,
                                         std::vector<unsigned char> *output,
                                         int *width, int *height) const {
  if (!output || !width || !height) {
    return false;
  }
  std::vector<unsigned char> cpu_frame;
#if defined(_WIN32)
  WindowsNativeFrameRef native_frame;
#endif
  {
    std::lock_guard lock(impl_->frames->mutex);
    const FrameSlot *newest = nullptr;
    for (const auto &slot : impl_->frames->slots) {
      if (slot.valid && slot.remote_id == remote_id &&
          (!newest || slot.sequence > newest->sequence)) {
        newest = &slot;
      }
    }
    if (!newest) {
      return false;
    }
    *width = newest->width;
    *height = newest->height;
    cpu_frame = newest->bytes;
#if defined(_WIN32)
    native_frame = newest->native_frame;
#endif
  }

  if (!cpu_frame.empty()) {
    *output = std::move(cpu_frame);
    return true;
  }
#if defined(_WIN32)
  if (const XWindowsVideoFrame *native = native_frame.Get()) {
    const size_t required_size =
        static_cast<size_t>(native->width) * native->height * 3U / 2U;
    output->resize(required_size);
    return native->copy_to_cpu(native->owner, output->data(), output->size()) ==
           0;
  }
#endif
  return false;
}

} // namespace crossdesk
