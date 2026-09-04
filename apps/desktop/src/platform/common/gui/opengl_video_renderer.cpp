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
#if defined(_WIN32) && USE_CUDA
constexpr size_t kCudaUploadSlotCount = 3;
#endif

class NativeVideoFrameRef {
public:
  NativeVideoFrameRef() = default;

  explicit NativeVideoFrameRef(const MiniRtcNativeVideoFrame *frame) {
    if (frame && frame->owner && frame->retain && frame->release) {
      frame_ = *frame;
      frame_.struct_size = sizeof(frame_);
      frame_.retain(frame_.owner);
      valid_ = true;
    }
  }

  NativeVideoFrameRef(const NativeVideoFrameRef &other)
      : NativeVideoFrameRef(other.Get()) {}

  NativeVideoFrameRef(NativeVideoFrameRef &&other) noexcept
      : frame_(other.frame_), valid_(std::exchange(other.valid_, false)) {
    other.frame_ = {};
  }

  NativeVideoFrameRef &operator=(NativeVideoFrameRef other) noexcept {
    Swap(other);
    return *this;
  }

  ~NativeVideoFrameRef() { Reset(); }

  void Reset() {
    if (valid_ && frame_.owner && frame_.release) {
      frame_.release(frame_.owner);
    }
    frame_ = {};
    valid_ = false;
  }

  void Swap(NativeVideoFrameRef &other) noexcept {
    std::swap(frame_, other.frame_);
    std::swap(valid_, other.valid_);
  }

  const MiniRtcNativeVideoFrame *Get() const { return valid_ ? &frame_ : nullptr; }
  explicit operator bool() const { return valid_; }

private:
  MiniRtcNativeVideoFrame frame_{};
  bool valid_ = false;
};

const MiniRtcNativeVideoFrame *GetOpenGlNativeFrame(
    const MiniRtcNativeVideoFrame &frame) {
  const MiniRtcNativeVideoFrame *native = &frame;
  if (native->struct_size < static_cast<uint32_t>(sizeof(MiniRtcNativeVideoFrame)) ||
      !native->owner || !native->retain || !native->release ||
      !native->copy_to_nv12 || native->width == 0 || native->height == 0 ||
      (native->width & 1U) != 0 || (native->height & 1U) != 0 ||
      native->width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
      native->height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    return nullptr;
  }
  if (native->type == MiniRtcNativeVideoFrameCpuNv12) {
    return native->payload.cpu_nv12.y_plane &&
                   native->payload.cpu_nv12.uv_plane &&
                   native->payload.cpu_nv12.y_stride >= native->width &&
                   native->payload.cpu_nv12.uv_stride >= native->width
               ? native
               : nullptr;
  }
  if (native->type == MiniRtcNativeVideoFrameCudaNv12) {
    return native->payload.cuda_nv12.y_device_pointer != 0 &&
                   native->payload.cuda_nv12.uv_device_pointer != 0 &&
                   native->payload.cuda_nv12.y_stride >= native->width &&
                   native->payload.cuda_nv12.uv_stride >= native->width &&
                   native->payload.cuda_nv12.context
               ? native
               : nullptr;
  }
  return nullptr;
}

enum class SlotUse {
  available,
  pending,
  uploading,
};

struct FrameSlot {
  std::vector<unsigned char> bytes;
  NativeVideoFrameRef native_frame;
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
#if defined(_WIN32) && USE_CUDA
  PFNGLFENCESYNCPROC fence_sync = nullptr;
  PFNGLCLIENTWAITSYNCPROC client_wait_sync = nullptr;
  PFNGLDELETESYNCPROC delete_sync = nullptr;
#endif

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

#if defined(_WIN32) && USE_CUDA
  bool LoadSyncFunctions() {
    return LoadOpenGlFunction(&fence_sync, "glFenceSync") &&
           LoadOpenGlFunction(&client_wait_sync, "glClientWaitSync") &&
           LoadOpenGlFunction(&delete_sync, "glDeleteSync");
  }
#endif
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

#if defined(_WIN32) && USE_CUDA
  enum class CudaUploadResult {
    ready,
    busy,
    unavailable,
  };

  struct CudaUploadSlot {
    GLuint buffer = 0;
    GLuint y_texture = 0;
    GLuint uv_texture = 0;
    size_t capacity = 0;
    int texture_width = 0;
    int texture_height = 0;
    CUgraphicsResource resource = nullptr;
    GLsync fence = nullptr;
  };

  std::array<CudaUploadSlot, kCudaUploadSlotCount> cuda_upload_slots;
  size_t next_cuda_upload_slot = 0;
  size_t displayed_cuda_upload_slot = kCudaUploadSlotCount;
  CUcontext cuda_context = nullptr;
  CUstream cuda_upload_stream = nullptr;
  NativeVideoFrameRef cuda_context_owner;
  bool cuda_sync_functions_available = false;
  bool cuda_interop_disabled = false;
  std::atomic<bool> cuda_cpu_fallback{false};
  bool cuda_native_logged = false;
  bool cuda_fallback_logged = false;
  uint64_t cuda_busy_drop_count = 0;

  void DisableCudaInterop() {
    cuda_interop_disabled = true;
    cuda_cpu_fallback.store(true, std::memory_order_release);
  }

  bool IsCudaSlotAvailable(CudaUploadSlot &slot) {
    if (!slot.fence) {
      return true;
    }
    const GLenum wait_result =
        gl.client_wait_sync(slot.fence, 0, 0);
    if (wait_result == GL_WAIT_FAILED) {
      DisableCudaInterop();
      return false;
    }
    if (wait_result != GL_ALREADY_SIGNALED &&
        wait_result != GL_CONDITION_SATISFIED) {
      return false;
    }
    gl.delete_sync(slot.fence);
    slot.fence = nullptr;
    return true;
  }

  void ReleaseCudaInterop() {
    bool has_gl_resources = false;
    for (const auto &slot : cuda_upload_slots) {
      has_gl_resources = has_gl_resources || slot.buffer != 0 || slot.fence;
    }
    if (has_gl_resources) {
      glFinish();
    }
    for (auto &slot : cuda_upload_slots) {
      if (slot.fence && gl.delete_sync) {
        gl.delete_sync(slot.fence);
        slot.fence = nullptr;
      }
    }

    if (cuda_context && minirtc::cuCtxPushCurrent_ld &&
        minirtc::cuCtxPushCurrent_ld(cuda_context) == CUDA_SUCCESS) {
      if (cuda_upload_stream && minirtc::cuStreamSynchronize_ld) {
        minirtc::cuStreamSynchronize_ld(cuda_upload_stream);
      }
      for (auto &slot : cuda_upload_slots) {
        if (slot.resource && minirtc::cuGraphicsUnregisterResource_ld) {
          minirtc::cuGraphicsUnregisterResource_ld(slot.resource);
          slot.resource = nullptr;
        }
      }
      if (cuda_upload_stream && minirtc::cuStreamDestroy_ld) {
        minirtc::cuStreamDestroy_ld(cuda_upload_stream);
      }
      CUcontext popped_context = nullptr;
      minirtc::cuCtxPopCurrent_ld(&popped_context);
    }
    cuda_upload_stream = nullptr;
    cuda_context = nullptr;
    cuda_context_owner.Reset();

    for (auto &slot : cuda_upload_slots) {
      if (slot.buffer != 0 && gl.delete_buffers) {
        gl.delete_buffers(1, &slot.buffer);
      }
      if (slot.y_texture != 0) {
        glDeleteTextures(1, &slot.y_texture);
      }
      if (slot.uv_texture != 0) {
        glDeleteTextures(1, &slot.uv_texture);
      }
      slot = {};
    }
    next_cuda_upload_slot = 0;
    displayed_cuda_upload_slot = kCudaUploadSlotCount;
  }

  bool PrepareCudaContext(const NativeVideoFrameRef &native_frame) {
    const MiniRtcNativeVideoFrame *frame = native_frame.Get();
    if (!frame || !frame->payload.cuda_nv12.context ||
        cuda_interop_disabled ||
        !cuda_sync_functions_available ||
        minirtc::LoadCudaGraphicsInterop() != 0) {
      DisableCudaInterop();
      return false;
    }

    auto frame_context =
        static_cast<CUcontext>(frame->payload.cuda_nv12.context);
    if (cuda_context && cuda_context != frame_context) {
      ReleaseCudaInterop();
    }
    if (!cuda_context) {
      cuda_context_owner = native_frame;
      cuda_context = frame_context;
      if (minirtc::cuCtxPushCurrent_ld(cuda_context) != CUDA_SUCCESS) {
        ReleaseCudaInterop();
        DisableCudaInterop();
        return false;
      }
      const CUresult create_result = minirtc::cuStreamCreate_ld(
          &cuda_upload_stream, CU_STREAM_NON_BLOCKING);
      CUcontext popped_context = nullptr;
      const CUresult pop_result =
          minirtc::cuCtxPopCurrent_ld(&popped_context);
      if (create_result != CUDA_SUCCESS || pop_result != CUDA_SUCCESS) {
        ReleaseCudaInterop();
        DisableCudaInterop();
        return false;
      }
    }
    return true;
  }

  bool PrepareCudaSlot(CudaUploadSlot &slot, size_t required_size) {
    if (slot.buffer == 0) {
      gl.gen_buffers(1, &slot.buffer);
    }
    if (slot.buffer == 0) {
      return false;
    }

    if (slot.capacity < required_size) {
      if (slot.resource) {
        if (minirtc::cuGraphicsUnregisterResource_ld(slot.resource) !=
            CUDA_SUCCESS) {
          return false;
        }
        slot.resource = nullptr;
      }
      GLint previous_unpack_buffer = 0;
      glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &previous_unpack_buffer);
      gl.bind_buffer(GL_PIXEL_UNPACK_BUFFER, slot.buffer);
      gl.buffer_data(GL_PIXEL_UNPACK_BUFFER,
                     static_cast<GLsizeiptr>(required_size), nullptr,
                     GL_STREAM_DRAW);
      gl.bind_buffer(GL_PIXEL_UNPACK_BUFFER,
                     static_cast<GLuint>(previous_unpack_buffer));
      if (glGetError() != GL_NO_ERROR) {
        return false;
      }
      slot.capacity = required_size;
    }

    if (!slot.resource) {
      if (minirtc::cuGraphicsGLRegisterBuffer_ld(
              &slot.resource, slot.buffer,
              CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD) != CUDA_SUCCESS) {
        slot.resource = nullptr;
        return false;
      }
    }
    return true;
  }

  bool PrepareCudaTextures(CudaUploadSlot &slot) {
    const bool needs_configuration =
        slot.y_texture == 0 || slot.uv_texture == 0;
    if (slot.y_texture == 0) {
      glGenTextures(1, &slot.y_texture);
    }
    if (slot.uv_texture == 0) {
      glGenTextures(1, &slot.uv_texture);
    }
    if (slot.y_texture == 0 || slot.uv_texture == 0) {
      return false;
    }
    if (!needs_configuration) {
      return true;
    }
    for (const auto [unit, texture] :
         {std::pair{GL_TEXTURE0, slot.y_texture},
          std::pair{GL_TEXTURE1, slot.uv_texture}}) {
      gl.active_texture(unit);
      glBindTexture(GL_TEXTURE_2D, texture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    return glGetError() == GL_NO_ERROR;
  }

  CudaUploadResult CopyCudaFrameToUploadBuffer(
      const NativeVideoFrameRef &native_frame, size_t *upload_slot_index) {
    const MiniRtcNativeVideoFrame *frame = native_frame.Get();
    if (!upload_slot_index || !PrepareCudaContext(native_frame)) {
      return CudaUploadResult::unavailable;
    }

    size_t selected_index = kCudaUploadSlotCount;
    for (size_t offset = 0; offset < cuda_upload_slots.size(); ++offset) {
      const size_t index =
          (next_cuda_upload_slot + offset) % cuda_upload_slots.size();
      if (IsCudaSlotAvailable(cuda_upload_slots[index])) {
        selected_index = index;
        break;
      }
    }
    if (selected_index == kCudaUploadSlotCount) {
      ++cuda_busy_drop_count;
      if (cuda_busy_drop_count == 1 || cuda_busy_drop_count % 300 == 0) {
        LOG_WARN("CUDA/OpenGL upload ring busy; dropped {} display frames",
                 cuda_busy_drop_count);
      }
      return CudaUploadResult::busy;
    }

    if (minirtc::cuCtxPushCurrent_ld(cuda_context) != CUDA_SUCCESS) {
      DisableCudaInterop();
      return CudaUploadResult::unavailable;
    }

    CudaUploadSlot &slot = cuda_upload_slots[selected_index];
    const size_t packed_size =
        static_cast<size_t>(frame->width) * frame->height * 3U / 2U;
    CUresult result = PrepareCudaSlot(slot, packed_size)
                          ? CUDA_SUCCESS
                          : CUDA_ERROR_INVALID_VALUE;
    CUgraphicsResource resource = slot.resource;
    bool mapped = false;
    CUdeviceptr destination = 0;
    size_t mapped_size = 0;
    if (result == CUDA_SUCCESS) {
      result = minirtc::cuGraphicsMapResources_ld(
          1, &resource, cuda_upload_stream);
      mapped = result == CUDA_SUCCESS;
    }
    if (result == CUDA_SUCCESS) {
      result = minirtc::cuGraphicsResourceGetMappedPointer_ld(
          &destination, &mapped_size, slot.resource);
    }
    if (result == CUDA_SUCCESS && mapped_size < packed_size) {
      result = CUDA_ERROR_INVALID_VALUE;
    }
    if (result == CUDA_SUCCESS) {
      CUDA_MEMCPY2D copy{};
      copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
      copy.srcDevice =
          static_cast<CUdeviceptr>(
              frame->payload.cuda_nv12.y_device_pointer);
      copy.srcPitch = frame->payload.cuda_nv12.y_stride;
      copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
      copy.dstDevice = destination;
      copy.dstPitch = frame->width;
      copy.WidthInBytes = frame->width;
      copy.Height = frame->height;
      result = minirtc::cuMemcpy2DAsync_ld(&copy, cuda_upload_stream);
      if (result == CUDA_SUCCESS) {
        copy.srcDevice =
            static_cast<CUdeviceptr>(
                frame->payload.cuda_nv12.uv_device_pointer);
        copy.srcPitch = frame->payload.cuda_nv12.uv_stride;
        copy.dstDevice = destination +
                         static_cast<CUdeviceptr>(frame->width) * frame->height;
        copy.Height = frame->height / 2U;
        result = minirtc::cuMemcpy2DAsync_ld(&copy, cuda_upload_stream);
      }
    }
    if (mapped) {
      const CUresult unmap_result = minirtc::cuGraphicsUnmapResources_ld(
          1, &resource, cuda_upload_stream);
      if (result == CUDA_SUCCESS) {
        result = unmap_result;
      }
    }
    CUcontext popped_context = nullptr;
    const CUresult pop_result =
        minirtc::cuCtxPopCurrent_ld(&popped_context);
    if (result != CUDA_SUCCESS || pop_result != CUDA_SUCCESS) {
      DisableCudaInterop();
      return CudaUploadResult::unavailable;
    }

    *upload_slot_index = selected_index;
    next_cuda_upload_slot = (selected_index + 1) % cuda_upload_slots.size();
    return CudaUploadResult::ready;
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
#if defined(_WIN32) && USE_CUDA
    displayed_cuda_upload_slot = kCudaUploadSlotCount;
#endif
  }
};

OpenGlVideoRenderer::OpenGlVideoRenderer()
    : impl_(std::make_unique<Impl>()) {}

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
#if defined(_WIN32) && USE_CUDA
  impl_->cuda_sync_functions_available = impl_->gl.LoadSyncFunctions();
  impl_->cuda_interop_disabled = !impl_->cuda_sync_functions_available;
  impl_->cuda_cpu_fallback.store(!impl_->cuda_sync_functions_available,
                                 std::memory_order_release);
  impl_->cuda_native_logged = false;
  impl_->cuda_fallback_logged = false;
  impl_->cuda_busy_drop_count = 0;
#endif

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
      impl_->corner_radius_location < 0 ||
      impl_->video_enabled_location < 0) {
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
  impl_->ReleaseCudaInterop();
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
      slot.native_frame.Reset();
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
      slot.valid = false;
      slot.native_frame.Reset();
    }
  }
  return true;
}

void OpenGlVideoRenderer::DiscardStream(std::string_view remote_id) {
  std::lock_guard lock(impl_->frames->mutex);
  for (auto &slot : impl_->frames->slots) {
    if (slot.remote_id == remote_id && slot.use != SlotUse::uploading) {
      slot.valid = false;
      slot.use = SlotUse::available;
      slot.remote_id.clear();
      slot.native_frame.Reset();
    }
  }
  if (impl_->frames->selected_stream == remote_id) {
    impl_->frames->selected_stream.clear();
  }
}

OpenGlVideoRenderer::SubmitResult
OpenGlVideoRenderer::SubmitNv12(std::string_view remote_id,
                                       const uint8_t *data, size_t size,
                                       int width, int height) {
  return SubmitNv12Internal(remote_id, data, size, width, height, true);
}

OpenGlVideoRenderer::SubmitResult
OpenGlVideoRenderer::SubmitCachedNv12(std::string_view remote_id,
                                             const uint8_t *data, size_t size,
                                             int width, int height) {
  return SubmitNv12Internal(remote_id, data, size, width, height, false);
}

OpenGlVideoRenderer::SubmitResult
OpenGlVideoRenderer::SubmitNv12Internal(std::string_view remote_id,
                                               const uint8_t *data, size_t size,
                                               int width, int height,
                                               bool replace_pending) {
  if (!IsReady()) {
    return SubmitResult::failed;
  }
  if (data == nullptr || width <= 0 || height <= 0 || (width & 1) != 0 ||
      (height & 1) != 0) {
    return SubmitResult::failed;
  }
  const size_t y_size = static_cast<size_t>(width) * height;
  const size_t required_size = y_size + y_size / 2;
  if (size < required_size) {
    return SubmitResult::failed;
  }

  auto &frames = *impl_->frames;
  std::lock_guard lock(frames.mutex);
  if (frames.selected_stream != remote_id) {
    return SubmitResult::not_selected;
  }

  FrameSlot *target = nullptr;
  if (replace_pending) {
    for (auto &slot : frames.slots) {
      if (slot.use == SlotUse::pending) {
        target = &slot;
        break;
      }
    }
  } else {
    for (const auto &slot : frames.slots) {
      if (slot.valid && slot.remote_id == remote_id &&
          (slot.use == SlotUse::pending || slot.use == SlotUse::uploading)) {
        return SubmitResult::dropped;
      }
    }
  }
  if (target == nullptr) {
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
  if (target == nullptr) {
    return SubmitResult::dropped;
  }

  target->bytes.resize(required_size);
  std::memcpy(target->bytes.data(), data, required_size);
  target->native_frame.Reset();
  target->width = width;
  target->height = height;
  target->remote_id.assign(remote_id);
  target->sequence = frames.next_sequence++;
  target->use = SlotUse::pending;
  target->valid = true;
  return SubmitResult::submitted;
}

OpenGlVideoRenderer::SubmitResult
OpenGlVideoRenderer::SubmitNativeFrame(std::string_view remote_id,
                                       const MiniRtcNativeVideoFrame &frame) {
  if (!IsReady()) {
    return SubmitResult::failed;
  }
  const MiniRtcNativeVideoFrame *native = GetOpenGlNativeFrame(frame);
  if (!native) {
    return SubmitResult::failed;
  }

  auto &frames = *impl_->frames;
  std::lock_guard lock(frames.mutex);
  if (!IsReady()) {
    return SubmitResult::failed;
  }
  if (frames.selected_stream != remote_id) {
    return SubmitResult::not_selected;
  }

  // Latest-frame-only queue: replacing an unconsumed decoded frame bounds
  // latency without dropping compressed delta frames before decode.
  FrameSlot *target = nullptr;
  for (auto &slot : frames.slots) {
    if (slot.use == SlotUse::pending) {
      target = &slot;
      break;
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
  if (!target) {
    return SubmitResult::dropped;
  }

#if defined(_WIN32) && USE_CUDA
  if (native->type == MiniRtcNativeVideoFrameCudaNv12 &&
      impl_->cuda_cpu_fallback.load(std::memory_order_acquire)) {
    const size_t required_size =
        static_cast<size_t>(native->width) * native->height * 3U / 2U;
    target->bytes.resize(required_size);
    if (native->copy_to_nv12(native->owner, target->bytes.data(),
                             target->bytes.size()) != 0) {
      target->bytes.clear();
      target->valid = false;
      target->use = SlotUse::available;
      return SubmitResult::failed;
    }
    target->native_frame.Reset();
    target->width = static_cast<int>(native->width);
    target->height = static_cast<int>(native->height);
    target->remote_id.assign(remote_id);
    target->sequence = frames.next_sequence++;
    target->use = SlotUse::pending;
    target->valid = true;
    return SubmitResult::submitted;
  }
#endif

  NativeVideoFrameRef retained(native);
  if (!retained) {
    return SubmitResult::failed;
  }
  target->bytes.clear();
  target->native_frame = std::move(retained);
  target->width = static_cast<int>(native->width);
  target->height = static_cast<int>(native->height);
  target->remote_id.assign(remote_id);
  target->sequence = frames.next_sequence++;
  target->use = SlotUse::pending;
  target->valid = true;
  return SubmitResult::submitted;
}

OpenGlVideoRenderer::RenderOutcome
OpenGlVideoRenderer::RenderLatest(std::string_view remote_id,
                                  int target_width, int target_height,
                                  int top_inset_pixels,
                                  int corner_radius_pixels) {
  if (!IsReady() || target_width <= 0 || target_height <= 0) {
    return {RenderResult::failed};
  }

  size_t slot_index = kFrameSlotCount;
  uint64_t sequence = 0;
  int source_width = 0;
  int source_height = 0;
  const unsigned char *y_data = nullptr;
  const unsigned char *uv_data = nullptr;
  NativeVideoFrameRef active_native_frame;
  {
    std::lock_guard lock(impl_->frames->mutex);
    for (size_t index = 0; index < impl_->frames->slots.size(); ++index) {
      const auto &slot = impl_->frames->slots[index];
      if (slot.valid && slot.use == SlotUse::pending &&
          slot.remote_id == remote_id &&
          (slot_index == kFrameSlotCount || slot.sequence > sequence)) {
        slot_index = index;
        sequence = slot.sequence;
      }
    }
    if (slot_index != kFrameSlotCount) {
      auto &selected = impl_->frames->slots[slot_index];
      selected.use = SlotUse::uploading;
      source_width = selected.width;
      source_height = selected.height;
      active_native_frame = selected.native_frame;
      if (!selected.bytes.empty()) {
        const size_t y_size = static_cast<size_t>(source_width) * source_height;
        y_data = selected.bytes.data();
        uv_data = selected.bytes.data() + y_size;
      }
      for (size_t index = 0; index < impl_->frames->slots.size(); ++index) {
        auto &slot = impl_->frames->slots[index];
        if (index != slot_index && slot.use == SlotUse::pending &&
            slot.remote_id == remote_id) {
          slot.use = SlotUse::available;
          slot.valid = false;
          slot.native_frame.Reset();
        }
      }
    }
  }

  GLint previous_program = 0;
  GLint previous_active_texture = GL_TEXTURE0;
  GLint previous_array_buffer = 0;
#if defined(_WIN32) && USE_CUDA
  GLint previous_unpack_buffer = 0;
#endif
  GLint previous_unpack_alignment = 4;
  GLint previous_viewport[4] = {};
  GLint previous_scissor_box[4] = {};
  GLfloat previous_clear_color[4] = {};
  GLboolean previous_color_mask[4] = {};
  glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &previous_active_texture);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_array_buffer);
#if defined(_WIN32) && USE_CUDA
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &previous_unpack_buffer);
#endif
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
  if (slot_index != kFrameSlotCount) {
    while (glGetError() != GL_NO_ERROR) {
    }
    const size_t y_size = static_cast<size_t>(source_width) * source_height;
    const size_t required_size = y_size + y_size / 2U;
    bool should_upload = true;
    bool use_pixel_unpack_buffer = false;
    size_t cuda_upload_slot = 0;
    std::vector<unsigned char> cpu_fallback;
    if (const MiniRtcNativeVideoFrame *native = active_native_frame.Get()) {
      if (native->type == MiniRtcNativeVideoFrameCpuNv12 &&
          native->payload.cpu_nv12.y_stride == native->width &&
          native->payload.cpu_nv12.uv_stride == native->width) {
        y_data = native->payload.cpu_nv12.y_plane;
        uv_data = native->payload.cpu_nv12.uv_plane;
      } else if (native->type == MiniRtcNativeVideoFrameCudaNv12) {
#if defined(_WIN32) && USE_CUDA
        const auto cuda_result = impl_->CopyCudaFrameToUploadBuffer(
            active_native_frame, &cuda_upload_slot);
        use_pixel_unpack_buffer =
            cuda_result == OpenGlVideoRenderer::Impl::CudaUploadResult::ready;
        should_upload =
            cuda_result != OpenGlVideoRenderer::Impl::CudaUploadResult::busy;
        if (use_pixel_unpack_buffer && !impl_->cuda_native_logged) {
          LOG_INFO("Windows native NV12 using asynchronous CUDA/OpenGL ring");
          impl_->cuda_native_logged = true;
        }
        if (should_upload && !use_pixel_unpack_buffer &&
            !impl_->cuda_fallback_logged) {
          LOG_WARN("CUDA/OpenGL native upload unavailable; using CPU fallback");
          impl_->cuda_fallback_logged = true;
        }
#endif
      }

      if (should_upload && !use_pixel_unpack_buffer && (!y_data || !uv_data)) {
        cpu_fallback.resize(required_size);
        if (native->copy_to_nv12(native->owner, cpu_fallback.data(),
                                 cpu_fallback.size()) == 0) {
          y_data = cpu_fallback.data();
          uv_data = cpu_fallback.data() + y_size;
        } else {
          upload_succeeded = false;
        }
      }
    }
    if (should_upload && !use_pixel_unpack_buffer && (!y_data || !uv_data)) {
      upload_succeeded = false;
    }

    GLuint upload_y_texture = impl_->y_texture;
    GLuint upload_uv_texture = impl_->uv_texture;
#if defined(_WIN32) && USE_CUDA
    if (should_upload && upload_succeeded && use_pixel_unpack_buffer) {
      auto &cuda_slot = impl_->cuda_upload_slots[cuda_upload_slot];
      upload_succeeded = impl_->PrepareCudaTextures(cuda_slot);
      upload_y_texture = cuda_slot.y_texture;
      upload_uv_texture = cuda_slot.uv_texture;
      if (!upload_succeeded) {
        impl_->DisableCudaInterop();
      }
    }
#endif
    if (should_upload && upload_succeeded) {
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      impl_->gl.active_texture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, upload_y_texture);
      impl_->gl.active_texture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, upload_uv_texture);
      if (use_pixel_unpack_buffer) {
#if defined(_WIN32) && USE_CUDA
        impl_->gl.bind_buffer(
            GL_PIXEL_UNPACK_BUFFER,
            impl_->cuda_upload_slots[cuda_upload_slot].buffer);
#endif
      }
      const void *y_pixels = use_pixel_unpack_buffer
                                 ? nullptr
                                 : static_cast<const void *>(y_data);
      const void *uv_pixels =
          use_pixel_unpack_buffer
              ? reinterpret_cast<const void *>(static_cast<uintptr_t>(y_size))
              : static_cast<const void *>(uv_data);
      int uploaded_texture_width = impl_->texture_width;
      int uploaded_texture_height = impl_->texture_height;
#if defined(_WIN32) && USE_CUDA
      if (use_pixel_unpack_buffer) {
        uploaded_texture_width =
            impl_->cuda_upload_slots[cuda_upload_slot].texture_width;
        uploaded_texture_height =
            impl_->cuda_upload_slots[cuda_upload_slot].texture_height;
      }
#endif
      if (uploaded_texture_width != source_width ||
          uploaded_texture_height != source_height) {
        impl_->gl.active_texture(GL_TEXTURE0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, source_width,
                     source_height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                     y_pixels);
        impl_->gl.active_texture(GL_TEXTURE1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, source_width / 2,
                     source_height / 2, 0, GL_LUMINANCE_ALPHA,
                     GL_UNSIGNED_BYTE, uv_pixels);
      } else {
        impl_->gl.active_texture(GL_TEXTURE0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, source_width, source_height,
                        GL_LUMINANCE, GL_UNSIGNED_BYTE, y_pixels);
        impl_->gl.active_texture(GL_TEXTURE1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, source_width / 2,
                        source_height / 2, GL_LUMINANCE_ALPHA,
                        GL_UNSIGNED_BYTE, uv_pixels);
      }
      if (use_pixel_unpack_buffer) {
#if defined(_WIN32) && USE_CUDA
        impl_->gl.bind_buffer(GL_PIXEL_UNPACK_BUFFER,
                              static_cast<GLuint>(previous_unpack_buffer));
#endif
      }
      const GLenum upload_error = glGetError();
      upload_succeeded = upload_error == GL_NO_ERROR;
      if (upload_succeeded) {
        impl_->texture_width = source_width;
        impl_->texture_height = source_height;
        impl_->uploaded_stream.assign(remote_id);
        impl_->uploaded_sequence = sequence;
#if defined(_WIN32) && USE_CUDA
        if (use_pixel_unpack_buffer) {
          auto &cuda_slot = impl_->cuda_upload_slots[cuda_upload_slot];
          cuda_slot.texture_width = source_width;
          cuda_slot.texture_height = source_height;
          impl_->displayed_cuda_upload_slot = cuda_upload_slot;
        } else {
          impl_->displayed_cuda_upload_slot = kCudaUploadSlotCount;
        }
#endif
      } else {
        LOG_WARN("OpenGL NV12 texture upload failed, error={}", upload_error);
      }
    }
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
  const bool has_video =
      upload_succeeded && impl_->uploaded_stream == remote_id &&
      impl_->texture_width > 0 && impl_->texture_height > 0 &&
      content_height > 0;
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
  GLuint displayed_y_texture = impl_->y_texture;
  GLuint displayed_uv_texture = impl_->uv_texture;
#if defined(_WIN32) && USE_CUDA
  if (impl_->displayed_cuda_upload_slot < impl_->cuda_upload_slots.size()) {
    const auto &cuda_slot =
        impl_->cuda_upload_slots[impl_->displayed_cuda_upload_slot];
    displayed_y_texture = cuda_slot.y_texture;
    displayed_uv_texture = cuda_slot.uv_texture;
  }
#endif
  impl_->gl.use_program(impl_->program);
  impl_->gl.active_texture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, displayed_y_texture);
  impl_->gl.uniform_1i(impl_->y_texture_location, 0);
  impl_->gl.active_texture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, displayed_uv_texture);
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
#if defined(_WIN32) && USE_CUDA
  if (impl_->displayed_cuda_upload_slot < impl_->cuda_upload_slots.size()) {
    auto &cuda_slot =
        impl_->cuda_upload_slots[impl_->displayed_cuda_upload_slot];
    if (cuda_slot.fence) {
      impl_->gl.delete_sync(cuda_slot.fence);
    }
    cuda_slot.fence =
        impl_->gl.fence_sync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!cuda_slot.fence) {
      glFinish();
      impl_->DisableCudaInterop();
    }
  }
#endif
  const GLenum draw_error = glGetError();
  RenderOutcome outcome{
      !upload_succeeded || draw_error != GL_NO_ERROR
          ? RenderResult::failed
          : has_video ? RenderResult::rendered : RenderResult::empty,
      has_video ? source_width : 0, has_video ? source_height : 0,
      has_video ? sequence : 0};

  RestoreVertexAttrib(impl_->gl, static_cast<GLuint>(impl_->position_location),
                      position_state);
  RestoreVertexAttrib(impl_->gl, static_cast<GLuint>(impl_->texcoord_location),
                      texcoord_state);
  impl_->gl.bind_buffer(GL_ARRAY_BUFFER,
                        static_cast<GLuint>(previous_array_buffer));
#if defined(_WIN32) && USE_CUDA
  impl_->gl.bind_buffer(GL_PIXEL_UNPACK_BUFFER,
                        static_cast<GLuint>(previous_unpack_buffer));
#endif
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
  return outcome;
}

bool OpenGlVideoRenderer::CopyLatestNv12(
    std::string_view remote_id, std::vector<unsigned char> *output, int *width,
    int *height) const {
  if (!output || !width || !height) {
    return false;
  }
  std::vector<unsigned char> cpu_frame;
  NativeVideoFrameRef native_frame;
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
    native_frame = newest->native_frame;
  }

  if (!cpu_frame.empty()) {
    *output = std::move(cpu_frame);
    return true;
  }
  if (const MiniRtcNativeVideoFrame *native = native_frame.Get()) {
    const size_t required_size =
        static_cast<size_t>(native->width) * native->height * 3U / 2U;
    output->resize(required_size);
    return native->copy_to_nv12(native->owner, output->data(),
                                output->size()) == 0;
  }
  return false;
}

} // namespace crossdesk
