#include "platform/windows_opengl_video_renderer.h"

#if !defined(_WIN32)
#error "WindowsOpenGlVideoRenderer is only available on Windows"
#endif

#include <windows.h>

#include <GL/gl.h>
#include <GL/glext.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

#include "rd_log.h"

namespace crossdesk {
namespace {

constexpr size_t kFrameSlotCount = 3;

enum class SlotUse {
  available,
  pending,
  uploading,
};

struct FrameSlot {
  std::vector<unsigned char> bytes;
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
  PROC address = wglGetProcAddress(name);
  if (address == nullptr || address == reinterpret_cast<PROC>(1) ||
      address == reinterpret_cast<PROC>(2) ||
      address == reinterpret_cast<PROC>(3) ||
      address == reinterpret_cast<PROC>(-1)) {
    HMODULE module = GetModuleHandleW(L"opengl32.dll");
    address = module ? GetProcAddress(module, name) : nullptr;
  }
  *function = reinterpret_cast<T>(address);
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
  PFNGLUNIFORM1IPROC uniform_1i = nullptr;
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
           LoadOpenGlFunction(&uniform_1i, "glUniform1i") &&
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

struct WindowsOpenGlVideoRenderer::Impl {
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
  int texture_width = 0;
  int texture_height = 0;
  std::string uploaded_stream;
  uint64_t uploaded_sequence = 0;

  void ResetGlHandles() {
    program = 0;
    vertex_buffer = 0;
    y_texture = 0;
    uv_texture = 0;
    position_location = -1;
    texcoord_location = -1;
    y_texture_location = -1;
    uv_texture_location = -1;
    texture_width = 0;
    texture_height = 0;
    uploaded_stream.clear();
    uploaded_sequence = 0;
  }
};

WindowsOpenGlVideoRenderer::WindowsOpenGlVideoRenderer()
    : impl_(std::make_unique<Impl>()) {}

WindowsOpenGlVideoRenderer::~WindowsOpenGlVideoRenderer() = default;

bool WindowsOpenGlVideoRenderer::Setup() {
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
varying vec2 video_texcoord;
void main() {
  float y = 1.16438356 *
            (texture2D(y_texture, video_texcoord).r - 16.0 / 255.0);
  vec2 uv = texture2D(uv_texture, video_texcoord).ra - vec2(0.5, 0.5);
  vec3 rgb = vec3(y + 1.59602678 * uv.y,
                  y - 0.39176229 * uv.x - 0.81296764 * uv.y,
                  y + 2.01723214 * uv.x);
  gl_FragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
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
varying vec2 video_texcoord;
void main() {
  float y = 1.16438356 *
            (texture2D(y_texture, video_texcoord).r - 16.0 / 255.0);
  vec2 uv = texture2D(uv_texture, video_texcoord).ra - vec2(0.5, 0.5);
  vec3 rgb = vec3(y + 1.59602678 * uv.y,
                  y - 0.39176229 * uv.x - 0.81296764 * uv.y,
                  y + 2.01723214 * uv.x);
  gl_FragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
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
  if (impl_->position_location < 0 || impl_->texcoord_location < 0 ||
      impl_->y_texture_location < 0 || impl_->uv_texture_location < 0) {
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

void WindowsOpenGlVideoRenderer::Teardown() {
  impl_->ready.store(false, std::memory_order_release);
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
  impl_->ResetGlHandles();
}

bool WindowsOpenGlVideoRenderer::IsReady() const {
  return impl_->ready.load(std::memory_order_acquire);
}

bool WindowsOpenGlVideoRenderer::SetSelectedStream(std::string remote_id) {
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
  return true;
}

void WindowsOpenGlVideoRenderer::DiscardStream(std::string_view remote_id) {
  std::lock_guard lock(impl_->frames->mutex);
  for (auto &slot : impl_->frames->slots) {
    if (slot.remote_id == remote_id && slot.use != SlotUse::uploading) {
      slot.valid = false;
      slot.use = SlotUse::available;
      slot.remote_id.clear();
    }
  }
  if (impl_->frames->selected_stream == remote_id) {
    impl_->frames->selected_stream.clear();
  }
}

WindowsOpenGlVideoRenderer::SubmitResult
WindowsOpenGlVideoRenderer::SubmitNv12(std::string_view remote_id,
                                       const uint8_t *data, size_t size,
                                       int width, int height) {
  return SubmitNv12Internal(remote_id, data, size, width, height, true);
}

WindowsOpenGlVideoRenderer::SubmitResult
WindowsOpenGlVideoRenderer::SubmitCachedNv12(std::string_view remote_id,
                                             const uint8_t *data, size_t size,
                                             int width, int height) {
  return SubmitNv12Internal(remote_id, data, size, width, height, false);
}

WindowsOpenGlVideoRenderer::SubmitResult
WindowsOpenGlVideoRenderer::SubmitNv12Internal(std::string_view remote_id,
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
  target->width = width;
  target->height = height;
  target->remote_id.assign(remote_id);
  target->sequence = frames.next_sequence++;
  target->use = SlotUse::pending;
  target->valid = true;
  return SubmitResult::submitted;
}

WindowsOpenGlVideoRenderer::RenderOutcome
WindowsOpenGlVideoRenderer::RenderLatest(std::string_view remote_id,
                                         int target_width, int target_height,
                                         int top_inset_pixels) {
  if (!IsReady() || target_width <= 0 || target_height <= 0) {
    return {RenderResult::failed};
  }

  size_t slot_index = kFrameSlotCount;
  uint64_t sequence = 0;
  int source_width = 0;
  int source_height = 0;
  const unsigned char *frame_data = nullptr;
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
      frame_data = selected.bytes.data();
      for (size_t index = 0; index < impl_->frames->slots.size(); ++index) {
        auto &slot = impl_->frames->slots[index];
        if (index != slot_index && slot.use == SlotUse::pending &&
            slot.remote_id == remote_id) {
          slot.use = SlotUse::available;
        }
      }
    }
  }

  GLint previous_program = 0;
  GLint previous_active_texture = GL_TEXTURE0;
  GLint previous_array_buffer = 0;
  GLint previous_unpack_alignment = 4;
  GLint previous_viewport[4] = {};
  GLint previous_scissor_box[4] = {};
  GLfloat previous_clear_color[4] = {};
  GLboolean previous_color_mask[4] = {};
  glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &previous_active_texture);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_array_buffer);
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
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  bool upload_succeeded = true;
  if (slot_index != kFrameSlotCount) {
    while (glGetError() != GL_NO_ERROR) {
    }
    const size_t y_size = static_cast<size_t>(source_width) * source_height;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    impl_->gl.active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, impl_->y_texture);
    impl_->gl.active_texture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, impl_->uv_texture);
    if (impl_->texture_width != source_width ||
        impl_->texture_height != source_height) {
      impl_->gl.active_texture(GL_TEXTURE0);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, source_width, source_height,
                   0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame_data);
      impl_->gl.active_texture(GL_TEXTURE1);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, source_width / 2,
                   source_height / 2, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                   frame_data + y_size);
    } else {
      impl_->gl.active_texture(GL_TEXTURE0);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, source_width, source_height,
                      GL_LUMINANCE, GL_UNSIGNED_BYTE, frame_data);
      impl_->gl.active_texture(GL_TEXTURE1);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, source_width / 2,
                      source_height / 2, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                      frame_data + y_size);
    }
    const GLenum upload_error = glGetError();
    upload_succeeded = upload_error == GL_NO_ERROR;
    if (upload_succeeded) {
      impl_->texture_width = source_width;
      impl_->texture_height = source_height;
      impl_->uploaded_stream.assign(remote_id);
      impl_->uploaded_sequence = sequence;
    } else {
      LOG_WARN("OpenGL NV12 texture upload failed, error={}", upload_error);
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

  RenderOutcome outcome{RenderResult::empty};
  if (upload_succeeded && impl_->uploaded_stream == remote_id &&
      impl_->texture_width > 0 && impl_->texture_height > 0) {
    source_width = impl_->texture_width;
    source_height = impl_->texture_height;
    sequence = impl_->uploaded_sequence;
    const int video_height = std::max(
        0, target_height - std::clamp(top_inset_pixels, 0, target_height));
    if (video_height > 0) {
      const double source_aspect =
          static_cast<double>(source_width) / source_height;
      const double target_aspect =
          static_cast<double>(target_width) / video_height;
      int viewport_width = target_width;
      int viewport_height = video_height;
      int viewport_x = 0;
      int viewport_y = 0;
      if (source_aspect > target_aspect) {
        viewport_height =
            std::max(1, static_cast<int>(target_width / source_aspect + 0.5));
        viewport_y = (video_height - viewport_height) / 2;
      } else {
        viewport_width =
            std::max(1, static_cast<int>(video_height * source_aspect + 0.5));
        viewport_x = (target_width - viewport_width) / 2;
      }

      glViewport(viewport_x, viewport_y, viewport_width, viewport_height);
      impl_->gl.use_program(impl_->program);
      impl_->gl.active_texture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, impl_->y_texture);
      impl_->gl.uniform_1i(impl_->y_texture_location, 0);
      impl_->gl.active_texture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, impl_->uv_texture);
      impl_->gl.uniform_1i(impl_->uv_texture_location, 1);
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
      outcome = {glGetError() == GL_NO_ERROR ? RenderResult::rendered
                                             : RenderResult::failed,
                 source_width, source_height, sequence};
    }
  }

  RestoreVertexAttrib(impl_->gl, static_cast<GLuint>(impl_->position_location),
                      position_state);
  RestoreVertexAttrib(impl_->gl, static_cast<GLuint>(impl_->texcoord_location),
                      texcoord_state);
  impl_->gl.bind_buffer(GL_ARRAY_BUFFER,
                        static_cast<GLuint>(previous_array_buffer));
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

bool WindowsOpenGlVideoRenderer::CopyLatestNv12(
    std::string_view remote_id, std::vector<unsigned char> *output, int *width,
    int *height) const {
  if (!output || !width || !height) {
    return false;
  }
  std::lock_guard lock(impl_->frames->mutex);
  const FrameSlot *newest = nullptr;
  for (const auto &slot : impl_->frames->slots) {
    if (slot.valid && slot.remote_id == remote_id &&
        (!newest || slot.sequence > newest->sequence)) {
      newest = &slot;
    }
  }
  if (!newest || newest->bytes.empty()) {
    return false;
  }
  *output = newest->bytes;
  *width = newest->width;
  *height = newest->height;
  return true;
}

} // namespace crossdesk
