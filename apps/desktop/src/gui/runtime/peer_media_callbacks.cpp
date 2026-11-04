#include <libyuv.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "nv12_scaler.h"
#include "platform/video_renderer.h"
#include "runtime/gui_runtime.h"
#include "runtime/peer_event_handler.h"

namespace crossdesk {
namespace {

constexpr auto kBackgroundSnapshotInterval = std::chrono::seconds(1);

const MiniRtcNativeVideoFrame* GetNativeVideoFrame(const MiniRtcVideoFrame* frame) {
  if (!frame || !frame->native_frame) {
    return nullptr;
  }
  const auto* native = frame->native_frame;
  return native->struct_size >=
                     static_cast<uint32_t>(sizeof(MiniRtcNativeVideoFrame)) &&
                 native->owner && native->copy_to_nv12
             ? native
             : nullptr;
}

}  // namespace

void PeerEventHandler::OnReceiveVideoBuffer(
    const MiniRtcVideoFrame* video_frame, const char* user_id, size_t user_id_size,
    const char* src_id, size_t src_id_size, void* user_data) {
  auto* handler = static_cast<PeerEventHandler*>(user_data);
  if (!handler) return;
  auto callback = handler->EnterCallback();
  if (!callback || !handler->DataAllowed()) return;
  GuiRuntime* runtime = &handler->owner_;
  if (!video_frame) {
    return;
  }

  std::string remote_id = handler->ConnectionKey(user_id, user_id_size);
  auto props = runtime->FindRemoteSession(remote_id);
  if (!props) return;

  if (props->connection_established_) {
    const auto timing_now = VideoLatencyFrame::Clock::now();
    // Pair the clocks before preview conversion so its cost is included.
    const auto local_now_us = GetSystemTimeMicros(
        handler->IsDirect() ? handler->DirectPeer() : props->peer_);
    const auto* received_native = GetNativeVideoFrame(video_frame);
    props->received_video_stats_.Record(
        received_native ? received_native->width : video_frame->width,
        received_native ? received_native->height : video_frame->height,
        timing_now);
    // Convert only visible thumbnails, off the UI thread. A session retains
    // one latest RGBA image, not an unbounded queue of frames.
    const int preview_width = props->gallery_preview_width_.load();
    // Multiple video streams may decode concurrently for the same session.
    // Serialize access to the shared conversion buffers. Visible previews
    // consume incoming frames at the negotiated interactive cadence.
    std::unique_lock conversion_lock(props->gallery_conversion_mutex_,
                                     std::defer_lock);
    if (preview_width > 0) conversion_lock.lock();
    if (conversion_lock.owns_lock()) {
      const auto gallery_timing = props->gallery_video_latency_->Frame(
          video_frame->captured_timestamp, local_now_us, timing_now);
      const auto* native = GetNativeVideoFrame(video_frame);
      const int w = native ? native->width : video_frame->width;
      const int h = native ? native->height : video_frame->height;
      if (w > 0 && h > 0 && w <= 16384 && h <= 16384 && !(w % 2) && !(h % 2)) {
        const size_t bytes = static_cast<size_t>(w) * h * 3 / 2;
        const uint8_t* data =
            reinterpret_cast<const uint8_t*>(video_frame->data);
        if (native) {
          props->gallery_nv12_.resize(bytes);
          data = native->copy_to_nv12(native->owner,
                                      props->gallery_nv12_.data(), bytes) == 0
                     ? props->gallery_nv12_.data()
                     : nullptr;
        } else if (video_frame->size < bytes)
          data = nullptr;
        if (data) {
          const double scale = std::min(
              {1.0, double(preview_width) / w, double(preview_width) / h});
          const int dw = std::max(2, int(w * scale) & ~1);
          const int dh = std::max(2, int(h * scale) & ~1);
          auto& scaled = props->gallery_scaled_;
          scaled.resize(static_cast<size_t>(dw) * dh * 3 / 2);
          if (ScaleNv12ViaI420(data, w, data + w * h, w, w, h, scaled.data(),
                               dw, scaled.data() + dw * dh, dw, dw, dh,
                               libyuv::kFilterBilinear,
                               &props->gallery_scratch_) == 0) {
            auto rgba = std::make_shared<std::vector<unsigned char>>(
                static_cast<size_t>(dw) * dh * 4);
            if (libyuv::NV12ToABGR(scaled.data(), dw, scaled.data() + dw * dh,
                                   dw, rgba->data(), dw * 4, dw, dh) == 0) {
              std::lock_guard lock(props->gallery_frame_mutex_);
              props->gallery_rgba_ = std::move(rgba);
              props->gallery_width_ = dw;
              props->gallery_height_ = dh;
              props->gallery_frame_at_ = timing_now;
              props->gallery_frame_timing_ = gallery_timing;
              ++props->gallery_sequence_;
            }
          }
        }
      }
    }
    if (conversion_lock.owns_lock()) conversion_lock.unlock();
    // A wall-only session does not need a second full-resolution CPU buffer.
    if (props->wall_managed_ && !props->tab_opened_) {
      {
        std::lock_guard lock(props->video_frame_mutex_);
        props->video_width_ = video_frame->width;
        props->video_height_ = video_frame->height;
      }
      props->streaming_ = true;
      return;
    }
    const auto timing = props->video_latency_->Frame(
        video_frame->captured_timestamp, local_now_us, timing_now);
    bool background_snapshot_only = false;
    auto* native_renderer = runtime->video_renderer_.get();
    if (native_renderer && native_renderer->IsActive()) {
      VideoRenderer::SubmitResult submit_result =
          VideoRenderer::SubmitResult::failed;
      if (const auto* native = GetNativeVideoFrame(video_frame)) {
        submit_result =
            native_renderer->SubmitNativeFrame(remote_id, *native, timing);
        // A CPU NV12 native frame is valid MiniRTC input, but the macOS Metal
        // fast path only accepts CVPixelBuffer. Materialize it when that path
        // rejects a software decoder's descriptor. This also supports peers
        // using CPU decoding when their platform hardware decoder is unavailable.
        if (submit_result == VideoRenderer::SubmitResult::failed &&
            native->type == MiniRtcNativeVideoFrameCpuNv12) {
          const size_t frame_size =
              static_cast<size_t>(native->width) * native->height * 3U / 2U;
          // One decode callback thread per session; keep the buffer across
          // frames instead of a full-frame allocation per rejected frame.
          auto& fallback = props->native_cpu_fallback_;
          if (fallback.size() != frame_size) fallback.resize(frame_size);
          if (native->copy_to_nv12(native->owner, fallback.data(),
                                   fallback.size()) == 0) {
            submit_result = native_renderer->SubmitNv12(
                remote_id, fallback.data(), fallback.size(),
                static_cast<int>(native->width),
                static_cast<int>(native->height), timing);
          }
        }
      } else
      {
        submit_result = native_renderer->SubmitNv12(
            remote_id, reinterpret_cast<const uint8_t*>(video_frame->data),
            video_frame->size, video_frame->width, video_frame->height, timing);
      }
      if (submit_result == VideoRenderer::SubmitResult::submitted) {
        std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
        const bool size_changed = (props->video_width_ != video_frame->width) ||
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
      if (submit_result == VideoRenderer::SubmitResult::dropped ||
          (submit_result == VideoRenderer::SubmitResult::failed &&
           native_renderer->IsActive())) {
        // Keep presenting the last native frame. A later decoded frame can
        // reuse an active renderer. If rendering disabled it concurrently,
        // continue below to materialize this frame for the CPU fallback.
        props->streaming_ = true;
        return;
      }
      if (submit_result == VideoRenderer::SubmitResult::not_selected) {
        background_snapshot_only = true;
      }
    }
    {
      std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
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

      size_t frame_size = video_frame->size;
      const MiniRtcNativeVideoFrame* native_frame =
          GetNativeVideoFrame(video_frame);
      if (native_frame) {
        frame_size = static_cast<size_t>(native_frame->width) *
                     native_frame->height * 3U / 2U;
      }
      if (frame_size == 0) {
        return;
      }

      // Allocate a third buffer only while the UI still owns the old snapshot.
      if (!props->back_frame_ || props->back_frame_.use_count() != 1) {
        props->back_frame_ =
            std::make_shared<std::vector<unsigned char>>(frame_size);
      }
      if (props->back_frame_->size() != frame_size) {
        props->back_frame_->resize(frame_size);
      }

      if (native_frame) {
        if (native_frame->copy_to_nv12(
                native_frame->owner, props->back_frame_->data(),
                props->back_frame_->size()) != 0) {
          return;
        }
      } else
      {
        if (!video_frame->data || video_frame->size < frame_size) {
          return;
        }
        std::memcpy(props->back_frame_->data(), video_frame->data, frame_size);
      }

      const bool size_changed = (props->video_width_ != video_frame->width) ||
                                (props->video_height_ != video_frame->height);
      if (size_changed) {
        props->render_rect_dirty_ = true;
      }

      props->video_width_ = video_frame->width;
      props->video_height_ = video_frame->height;
      props->video_size_ = frame_size;

      props->front_frame_.swap(props->back_frame_);
      props->video_frame_timing_ = timing;
      props->thumbnail_frame_ = props->front_frame_;
      props->thumbnail_width_ = video_frame->width;
      props->thumbnail_height_ = video_frame->height;
      if (background_snapshot_only) {
        props->background_snapshot_time_ = now;
      }
      ++props->video_frame_sequence_;
    }

    props->streaming_ = true;
    if (background_snapshot_only) {
      return;
    }
    runtime->video_frame_dirty_.store(true, std::memory_order_release);
  }
}

void PeerEventHandler::OnReceiveAudioBuffer(
    const char* data, size_t size, const char* user_id, size_t user_id_size,
    const char* src_id, size_t src_id_size, void* user_data) {
  auto* handler = static_cast<PeerEventHandler*>(user_data);
  if (!handler) return;
  auto callback = handler->EnterCallback();
  if (!callback || !handler->DataAllowed()) return;
  GuiRuntime* runtime = &handler->owner_;
  if (runtime->unattended_host_) {
    return;
  }

  std::lock_guard delivery_lock(runtime->audio_delivery_mutex_);
  const auto session =
      runtime->FindRemoteSession(handler->ConnectionKey(user_id, user_id_size));
  if (!session || session->closing_ || !session->audio_allowed_.load()) return;
  runtime->audio_buffer_fresh_ = true;

  runtime->devices_.PushAudio(session->remote_id_, data, size);
}

}  // namespace crossdesk
