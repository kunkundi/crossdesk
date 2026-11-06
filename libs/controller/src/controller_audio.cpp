#include "controller_session.h"

#include <stream_names.h>
#include <string_view>

namespace crossdesk {
void ControllerSession::Audio(const char *data, size_t size, const char *, size_t,
                              const char *stream, size_t stream_size, void *opaque) {
  // MiniRTC's receive decoder produces 48 kHz, mono, signed 16-bit PCM.
  auto &session = *static_cast<ControllerSession *>(opaque);
  if (!data || !size || size > 11520 || (size & 1) || !stream ||
      std::string_view(stream, stream_size) != kAudioStream) return;
  std::lock_guard lock(session.mutex_);
  if (session.stopping_ || !session.audio_enabled_) return;
  while (!session.audio_frames_.empty() && session.audio_bytes_ + size > 96000) {
    session.audio_bytes_ -= session.audio_frames_.front().pcm.size();
    session.audio_frames_.pop_front();
    ++session.audio_dropped_;
  }
  AudioFrame frame;
  frame.generation = session.callback_generation_;
  frame.sequence = ++session.audio_sequence_;
  frame.pcm.assign(data, data + size);
  session.audio_bytes_ += size;
  session.audio_frames_.push_back(std::move(frame));
}
void ControllerSession::SetAudio(bool enabled) {
  if (!Connected()) throw std::invalid_argument("NOT_CONNECTED");
  {
    std::lock_guard lock(mutex_);
    audio_enabled_ = enabled;
    audio_frames_.clear();
    audio_bytes_ = 0;
  }
  RemoteAction action{};
  action.type = audio_capture;
  action.a = enabled;
  Send(kControlStream, action.to_json());
  Preview();
}
std::optional<AudioFrame> ControllerSession::TakeAudio() {
  std::lock_guard lock(mutex_);
  if (audio_frames_.empty()) return std::nullopt;
  auto frame = std::move(audio_frames_.front());
  audio_frames_.pop_front();
  audio_bytes_ -= frame.pcm.size();
  return frame;
}
} // namespace crossdesk
