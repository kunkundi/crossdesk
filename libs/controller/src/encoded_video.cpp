#include "controller_session.h"
#include <algorithm>
#include <display_stream_id.h>

namespace crossdesk {
namespace {
constexpr size_t kMaxAccessUnit = 4 * 1024 * 1024;
// NAL spans include their Annex B start code, so cached parameter sets can be
// prepended to an IDR without converting or re-encoding the original stream.
size_t StartCode(const uint8_t *data, size_t size, size_t at) {
  if (at + 3 <= size && !data[at] && !data[at + 1]) {
    if (data[at + 2] == 1)
      return 3;
    if (at + 4 <= size && !data[at + 2] && data[at + 3] == 1)
      return 4;
  }
  return 0;
}
} // namespace
void ControllerSession::SetVideoOutput(bool enabled, bool request_keyframe) {
  std::lock_guard lock(mutex_);
  if (video_output_ != enabled || request_keyframe) {
    video_frames_.clear();
    video_bytes_ = 0;
    video_wait_key_ = true;
    video_request_key_ = enabled;
  }
  video_output_ = enabled;
}
std::optional<EncodedVideoFrame> ControllerSession::TakeEncodedVideo() {
  std::lock_guard lock(mutex_);
  if (video_frames_.empty())
    return {};
  auto frame = std::move(video_frames_.front());
  video_frames_.pop_front();
  video_bytes_ -= frame.data.size();
  return frame;
}
int ControllerSession::EncodedVideo(const uint8_t *data, size_t size, int64_t,
                                    const char *, size_t, const char *stream,
                                    size_t stream_size, void *context) {
  auto &s = *static_cast<ControllerSession *>(context);
  try {
    std::lock_guard lock(s.mutex_);
    if (s.stopping_ || !s.video_output_ || !data || !size ||
        size > kMaxAccessUnit || !stream || stream_size >= 64 ||
        std::string(stream, stream_size) !=
            MakeDisplayStreamId(s.callback_display_))
      return 0;
    const auto now = std::chrono::steady_clock::now();
    if (s.video_generation_ != s.callback_generation_) {
      s.video_generation_ = s.callback_generation_;
      s.video_frames_.clear();
      s.video_bytes_ = 0;
      s.video_sps_.clear();
      s.video_pps_.clear();
      s.video_wait_key_ = s.video_request_key_ = true;
    }
    bool key = false;
    for (size_t at = 0; at + 3 < size;) {
      const auto prefix = StartCode(data, size, at);
      if (!prefix) {
        ++at;
        continue;
      }
      const auto body = at + prefix;
      if (body >= size)
        break;
      size_t end = body + 1;
      while (end < size && !StartCode(data, size, end))
        ++end;
      const auto type = data[body] & 31;
      key |= type == 5;
      if ((type == 7 || type == 8) && end - at <= 65536) {
        auto &parameter = type == 7 ? s.video_sps_ : s.video_pps_;
        const std::vector<uint8_t> next(data + at, data + end);
        if (parameter != next) {
          parameter = next;
          s.video_wait_key_ = s.video_request_key_ = true;
          s.video_frames_.clear();
          s.video_bytes_ = 0;
        }
      }
      at = end;
    }
    ++s.video_sequence_;
    const auto output_size =
        size + (key ? s.video_sps_.size() + s.video_pps_.size() : 0);
    if (s.video_frames_.size() >= 60 ||
        s.video_bytes_ + output_size > 8 * 1024 * 1024) {
      s.video_frames_.clear();
      s.video_bytes_ = 0;
      s.video_wait_key_ = s.video_request_key_ = true;
    }
    if (key && !s.video_sps_.empty() && !s.video_pps_.empty()) {
      s.video_wait_key_ = s.video_request_key_ = false;
    }
    if (!s.video_wait_key_) {
      EncodedVideoFrame frame;
      frame.key = key;
      frame.generation = s.callback_generation_;
      frame.sequence = s.video_sequence_;
      frame.timestamp_us =
          std::chrono::duration_cast<std::chrono::microseconds>(
              now.time_since_epoch())
              .count();
      if (key) {
        frame.data = s.video_sps_;
        frame.data.insert(frame.data.end(), s.video_pps_.begin(),
                          s.video_pps_.end());
      }
      frame.data.insert(frame.data.end(), data, data + size);
      if (frame.data.size() <= kMaxAccessUnit) {
        s.video_bytes_ += frame.data.size();
        s.video_frames_.push_back(std::move(frame));
      } else {
        s.video_wait_key_ = s.video_request_key_ = true;
      }
    }
    if (s.video_request_key_ &&
        now - s.video_key_requested_ >= std::chrono::seconds(1)) {
      s.video_key_requested_ = now;
      return 1;
    }
  } catch (...) { /* The receive callback must not unwind into MiniRTC. */
  }
  return 0;
}
} // namespace crossdesk
