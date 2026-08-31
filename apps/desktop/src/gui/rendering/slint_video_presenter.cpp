#include "rendering/slint_video_presenter.h"

#include <libyuv.h>
#include <slint.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "nv12_scaler.h"
#include "platform/video_renderer.h"
#include "rd_log.h"

#if defined(__APPLE__)
#include "platform/macos/gui/metal_video_renderer.h"
#else
#if defined(_WIN32)
#include <windows.h>
#endif
#include <GL/gl.h>

#include "platform/common/gui/opengl_video_renderer.h"
#endif

namespace crossdesk {
namespace {

#if !defined(__APPLE__)
constexpr GLint kGlClampToEdge = 0x812F;
constexpr float kStreamWindowCornerRadius = 12.0f;
#else
constexpr int kMetalAttachmentAttemptLimit = 30;
#endif

struct VideoRenderSize {
  int width = 0;
  int height = 0;

  bool operator==(const VideoRenderSize&) const = default;
};

VideoRenderSize FitVideoToRenderArea(int source_width, int source_height,
                                     int area_width, int area_height) {
  if (source_width <= 0 || source_height <= 0 || area_width <= 0 ||
      area_height <= 0) {
    return {};
  }

  const double scale =
      std::min({1.0, static_cast<double>(area_width) / source_width,
                static_cast<double>(area_height) / source_height});
  int width = std::max(2, static_cast<int>(std::floor(source_width * scale)));
  int height = std::max(2, static_cast<int>(std::floor(source_height * scale)));

  width &= ~1;
  height &= ~1;
  return {std::min(width, source_width & ~1),
          std::min(height, source_height & ~1)};
}

}  // namespace

struct SlintVideoPresenter::Impl {
  explicit Impl(VideoRenderer& renderer) : renderer(renderer) {}

  VideoRenderer& renderer;
  slint::ComponentHandle<ui::StreamWindow>* stream = nullptr;
  StateProvider state_provider;
  DirtyCallback dirty_callback;
  std::unordered_map<std::string, uint64_t> displayed_frame_sequence;
  std::unordered_map<std::string, uint64_t> seeded_frame_sequence;
  std::vector<uint8_t> scaled_video_frame;
  std::vector<uint8_t> scaled_video_frame_scratch;

#if defined(__APPLE__)
  int attachment_attempts = 0;
  bool skip_attachment_once = false;
#else
  std::mutex gl_mutex;
  std::vector<uint8_t> gl_conversion_frame;
  std::vector<uint8_t> gl_pending_frame;
  uint32_t gl_texture = 0;
  VideoRenderSize gl_texture_size;
  VideoRenderSize gl_image_size;
  VideoRenderSize gl_pending_size;
  bool gl_pending_dirty = false;
#endif

  SurfaceState CurrentState() const {
    return state_provider ? state_provider() : SurfaceState{};
  }

  void MarkDirty() const {
    if (dirty_callback) {
      dirty_callback();
    }
  }

  bool MarkPresented(const std::string& remote_id, uint64_t sequence) {
    if (remote_id.empty() || sequence == 0 ||
        displayed_frame_sequence[remote_id] == sequence) {
      return false;
    }
    displayed_frame_sequence[remote_id] = sequence;
    return true;
  }

#if !defined(__APPLE__)
  void ConfigureOpenGlNotifier() {
    if (!stream) {
      return;
    }

    const auto error = (*stream)->window().set_rendering_notifier(
        [this](slint::RenderingState state, slint::GraphicsAPI graphics_api) {
          if (graphics_api != slint::GraphicsAPI::NativeOpenGL) {
            return;
          }

          std::lock_guard lock(gl_mutex);
          auto* opengl_renderer =
              dynamic_cast<OpenGlVideoRenderer*>(&renderer);
          if (state == slint::RenderingState::RenderingSetup) {
            GLuint texture = 0;
            glGenTextures(1, &texture);
            if (texture == 0) {
              return;
            }
            GLint previous_texture = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, kGlClampToEdge);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kGlClampToEdge);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
            gl_texture = texture;
            if (opengl_renderer && opengl_renderer->Setup()) {
              const std::string selected_stream =
                  CurrentState().selected_stream;
              opengl_renderer->SetSelectedStream(selected_stream);
              seeded_frame_sequence.erase(selected_stream);
              MarkDirty();
            }
            return;
          }

          if (state == slint::RenderingState::BeforeRendering && stream &&
              opengl_renderer && opengl_renderer->IsReady() &&
              (*stream)->get_native_video_enabled()) {
            const SurfaceState surface = CurrentState();
            const auto window_size = (*stream)->window().size();
            const float scale_factor =
                std::max(1.0f, (*stream)->window().scale_factor());
            const int top_inset =
                !surface.fullscreen && surface.tab_count > 1
                    ? static_cast<int>(std::lround(30.0f * scale_factor))
                    : 0;
            const bool rounded_window = (*stream)->get_custom_titlebar() &&
                                        !surface.fullscreen &&
                                        !(*stream)->window().is_maximized();
            const int corner_radius =
                rounded_window ? static_cast<int>(std::lround(
                                     kStreamWindowCornerRadius * scale_factor))
                               : 0;
            opengl_renderer->RenderLatest(
                surface.selected_stream, static_cast<int>(window_size.width),
                static_cast<int>(window_size.height), top_inset, corner_radius);
            return;
          }

          if (state == slint::RenderingState::BeforeRendering &&
              gl_texture != 0 && gl_pending_dirty &&
              !gl_pending_frame.empty()) {
            GLint previous_texture = 0;
            GLint previous_unpack_alignment = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_unpack_alignment);
            glBindTexture(GL_TEXTURE_2D, gl_texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            if (gl_texture_size != gl_pending_size) {
              glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gl_pending_size.width,
                           gl_pending_size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                           gl_pending_frame.data());
              gl_texture_size = gl_pending_size;
            } else {
              glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gl_pending_size.width,
                              gl_pending_size.height, GL_RGBA, GL_UNSIGNED_BYTE,
                              gl_pending_frame.data());
            }
            glPixelStorei(GL_UNPACK_ALIGNMENT, previous_unpack_alignment);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
            gl_pending_dirty = false;
            return;
          }

          if (state == slint::RenderingState::RenderingTeardown) {
            if (opengl_renderer) {
              opengl_renderer->Teardown();
            }
            if (gl_texture != 0) {
              const GLuint texture = gl_texture;
              glDeleteTextures(1, &texture);
            }
            gl_texture = 0;
            gl_texture_size = {};
            gl_image_size = {};
            gl_pending_size = {};
            gl_pending_dirty = false;
            gl_pending_frame.clear();
          }
        });

    if (error.has_value()) {
      LOG_WARN("Slint OpenGL video renderer unavailable, using pixel buffers");
    } else {
      renderer.SetSelectedStream(CurrentState().selected_stream);
    }
  }
#endif
};

SlintVideoPresenter::SlintVideoPresenter(VideoRenderer& renderer)
    : impl_(std::make_unique<Impl>(renderer)) {}

SlintVideoPresenter::~SlintVideoPresenter() = default;

void SlintVideoPresenter::PrepareWindow(
    slint::ComponentHandle<ui::StreamWindow>& stream,
    StateProvider state_provider, DirtyCallback dirty_callback) {
  impl_->stream = &stream;
  impl_->state_provider = std::move(state_provider);
  impl_->dirty_callback = std::move(dirty_callback);
#if defined(__APPLE__)
  stream->set_native_video_enabled(false);
  impl_->attachment_attempts = kMetalAttachmentAttemptLimit;
  impl_->skip_attachment_once = true;
#else
  impl_->ConfigureOpenGlNotifier();
#endif
}

bool SlintVideoPresenter::EnsureAttached() {
#if defined(__APPLE__)
  if (!impl_->stream || impl_->attachment_attempts <= 0) {
    return false;
  }
  if (impl_->skip_attachment_once) {
    impl_->skip_attachment_once = false;
    return false;
  }

  auto* metal_renderer =
      dynamic_cast<MacMetalVideoRenderer*>(&impl_->renderer);
  if (!metal_renderer) {
    impl_->attachment_attempts = 0;
    (*impl_->stream)->set_native_video_enabled(false);
    return false;
  }
  if (!metal_renderer->IsReady()) {
    impl_->attachment_attempts = 0;
    (*impl_->stream)->set_native_video_enabled(false);
    return false;
  }
  if (metal_renderer->IsAttached()) {
    impl_->attachment_attempts = 0;
    return false;
  }

  if (void* view = (*impl_->stream)->window().appkit_view()) {
    (*impl_->stream)->set_native_video_enabled(true);
    if (metal_renderer->Attach(view)) {
      impl_->attachment_attempts = 0;
      const SurfaceState surface = impl_->CurrentState();
      impl_->displayed_frame_sequence.erase(surface.selected_stream);
      impl_->seeded_frame_sequence.erase(surface.selected_stream);
      if (metal_renderer->SetSelectedStream(surface.selected_stream)) {
        impl_->MarkDirty();
      }
      return true;
    }
    (*impl_->stream)->set_native_video_enabled(false);
  }

  if (--impl_->attachment_attempts == 0) {
    (*impl_->stream)->set_native_video_enabled(false);
    LOG_WARN("Unable to attach the Metal video surface; using CPU rendering");
  }
#endif
  return false;
}

bool SlintVideoPresenter::NeedsRedraw() const {
#if defined(__APPLE__)
  const auto* metal_renderer =
      dynamic_cast<const MacMetalVideoRenderer*>(&impl_->renderer);
  return metal_renderer && metal_renderer->IsActive() &&
         metal_renderer->NeedsSurfaceRedraw();
#else
  return false;
#endif
}

bool SlintVideoPresenter::SelectStream(std::string remote_id) {
  const bool changed = impl_->renderer.SetSelectedStream(remote_id);
  if (changed && !remote_id.empty()) {
    impl_->seeded_frame_sequence.erase(remote_id);
  }
  return changed;
}

SlintVideoPresenter::PresentResult SlintVideoPresenter::Present(
    const Frame& frame) {
  PresentResult result;
  if (!impl_->stream || frame.remote_id.empty()) {
    return result;
  }

  if (impl_->renderer.IsActive() && frame.nv12 && frame.width > 0 &&
      frame.height > 0 && frame.sequence > 0 &&
      impl_->seeded_frame_sequence[frame.remote_id] != frame.sequence) {
    const auto submit_result = impl_->renderer.SubmitCachedNv12(
        frame.remote_id, frame.nv12->data(), frame.nv12->size(), frame.width,
        frame.height);
    if (submit_result == VideoRenderer::SubmitResult::submitted ||
        submit_result == VideoRenderer::SubmitResult::dropped) {
      impl_->seeded_frame_sequence[frame.remote_id] = frame.sequence;
    }
  }

#if defined(__APPLE__)
  auto* metal_renderer =
      dynamic_cast<MacMetalVideoRenderer*>(&impl_->renderer);
  if (metal_renderer && metal_renderer->IsActive()) {
    const SurfaceState surface = impl_->CurrentState();
    const double top_inset =
        !surface.fullscreen && surface.tab_count > 1 ? 30.0 : 0.0;
    const auto outcome = metal_renderer->RenderLatest(
        frame.remote_id, top_inset, !surface.fullscreen);
    if (outcome.result == VideoRenderer::RenderResult::rendered) {
      (*impl_->stream)->set_has_frame(true);
      (*impl_->stream)->set_receiving_text("");
      result.width = outcome.width;
      result.height = outcome.height;
      result.new_frame =
          impl_->MarkPresented(frame.remote_id, outcome.sequence);
    } else if (outcome.result == VideoRenderer::RenderResult::empty) {
      (*impl_->stream)->set_has_frame(false);
    }
    return result;
  }
#else
  auto* opengl_renderer =
      dynamic_cast<OpenGlVideoRenderer*>(&impl_->renderer);
  if (opengl_renderer && opengl_renderer->IsReady()) {
    opengl_renderer->SetSelectedStream(frame.remote_id);
    if (!(*impl_->stream)->get_native_video_enabled()) {
      (*impl_->stream)->set_native_video_enabled(true);
    }

    const bool has_frame =
        frame.width > 0 && frame.height > 0 && frame.sequence > 0;
    (*impl_->stream)->set_has_frame(has_frame);
    if (has_frame) {
      (*impl_->stream)->set_receiving_text("");
      result.width = frame.width;
      result.height = frame.height;
      result.new_frame = impl_->MarkPresented(frame.remote_id, frame.sequence);
    }
    (*impl_->stream)->window().request_redraw();
    return result;
  }
  if ((*impl_->stream)->get_native_video_enabled()) {
    (*impl_->stream)->set_native_video_enabled(false);
  }
#endif

  const size_t nv12_size =
      static_cast<size_t>(frame.width) * frame.height * 3 / 2;
  if (!frame.nv12 || frame.width <= 0 || frame.height <= 0 ||
      frame.nv12->size() < nv12_size) {
    (*impl_->stream)->set_has_frame(false);
    return result;
  }
  if (impl_->displayed_frame_sequence[frame.remote_id] == frame.sequence) {
    return result;
  }

  const auto window_size = (*impl_->stream)->window().size();
  const VideoRenderSize render_size = FitVideoToRenderArea(
      frame.width, frame.height, static_cast<int>(window_size.width),
      static_cast<int>(window_size.height));
  const uint8_t* y_plane = frame.nv12->data();
  const uint8_t* uv_plane =
      frame.nv12->data() + static_cast<size_t>(frame.width) * frame.height;
  int output_width = frame.width;
  int output_height = frame.height;

  if (render_size.width > 0 && render_size.height > 0 &&
      (render_size.width != frame.width ||
       render_size.height != frame.height)) {
    const size_t scaled_nv12_size =
        static_cast<size_t>(render_size.width) * render_size.height * 3 / 2;
    impl_->scaled_video_frame.resize(scaled_nv12_size);
    uint8_t* scaled_y = impl_->scaled_video_frame.data();
    uint8_t* scaled_uv =
        scaled_y + static_cast<size_t>(render_size.width) * render_size.height;
    if (ScaleNv12ViaI420(y_plane, frame.width, uv_plane, frame.width,
                         frame.width, frame.height, scaled_y, render_size.width,
                         scaled_uv, render_size.width, render_size.width,
                         render_size.height, libyuv::kFilterBox,
                         &impl_->scaled_video_frame_scratch) == 0) {
      y_plane = scaled_y;
      uv_plane = scaled_uv;
      output_width = render_size.width;
      output_height = render_size.height;
    }
  }

#if !defined(__APPLE__)
  uint32_t texture_id = 0;
  {
    std::lock_guard lock(impl_->gl_mutex);
    texture_id = impl_->gl_texture;
  }
  if (texture_id != 0) {
    impl_->gl_conversion_frame.resize(static_cast<size_t>(output_width) *
                                      output_height * 4);
    if (libyuv::NV12ToABGR(y_plane, output_width, uv_plane, output_width,
                           impl_->gl_conversion_frame.data(), output_width * 4,
                           output_width, output_height) != 0) {
      return result;
    }
    {
      std::lock_guard lock(impl_->gl_mutex);
      if (impl_->gl_texture == 0) {
        return result;
      }
      texture_id = impl_->gl_texture;
      impl_->gl_pending_frame.swap(impl_->gl_conversion_frame);
      impl_->gl_pending_size = {output_width, output_height};
      impl_->gl_pending_dirty = true;
    }
    const VideoRenderSize output_size{output_width, output_height};
    if (impl_->gl_image_size != output_size) {
      (*impl_->stream)
          ->set_frame(slint::Image::create_from_borrowed_gl_2d_rgba_texture(
              texture_id,
              slint::Size<uint32_t>{static_cast<uint32_t>(output_width),
                                    static_cast<uint32_t>(output_height)}));
      impl_->gl_image_size = output_size;
    }
    (*impl_->stream)->set_has_frame(true);
    (*impl_->stream)->set_receiving_text("");
    (*impl_->stream)->window().request_redraw();
    result.width = frame.width;
    result.height = frame.height;
    result.new_frame = impl_->MarkPresented(frame.remote_id, frame.sequence);
    return result;
  }
#endif

  slint::SharedPixelBuffer<slint::Rgb8Pixel> pixels(output_width,
                                                    output_height);
  if (libyuv::NV12ToRAW(y_plane, output_width, uv_plane, output_width,
                        reinterpret_cast<uint8_t*>(pixels.begin()),
                        output_width * 3, output_width, output_height) != 0) {
    return result;
  }

  (*impl_->stream)->set_frame(slint::Image(std::move(pixels)));
  (*impl_->stream)->set_has_frame(true);
  (*impl_->stream)->set_receiving_text("");
  result.width = frame.width;
  result.height = frame.height;
  result.new_frame = impl_->MarkPresented(frame.remote_id, frame.sequence);
  return result;
}

void SlintVideoPresenter::ForgetStream(const std::string& remote_id) {
  impl_->displayed_frame_sequence.erase(remote_id);
  impl_->seeded_frame_sequence.erase(remote_id);
}

void SlintVideoPresenter::Detach() {
  impl_->renderer.SetSelectedStream({});
#if defined(__APPLE__)
  if (auto* metal_renderer =
          dynamic_cast<MacMetalVideoRenderer*>(&impl_->renderer)) {
    metal_renderer->Detach();
  }
  impl_->attachment_attempts = 0;
  impl_->skip_attachment_once = false;
#endif
  impl_->stream = nullptr;
  impl_->state_provider = {};
  impl_->dirty_callback = {};
}

}  // namespace crossdesk
