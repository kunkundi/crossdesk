#include "controller_session.h"
#include <climits>
#include <cstring>

namespace crossdesk {
bool LatestFrame::Submit(const MiniRtcVideoFrame &f, uint64_t generation) {
  std::lock_guard lock(mutex_);
  if (generation != generation_)
    return false;
  const size_t w = f.width, h = f.height;
  if (!w || !h || (w & 1) || (h & 1) || w > 8192 || h > 8192 ||
      w * h > 16777216) {
    ++invalid_;
    return false;
  }
  const size_t size = w * h * 3 / 2;
  const auto *n = f.native_frame;
  const bool native = n && n->struct_size >= sizeof(*n) && n->width == w &&
                      n->height == h && n->owner;
  if (native && n->type == MiniRtcNativeVideoFrameCpuNv12) {
    const auto &p = n->payload.cpu_nv12;
    if (!p.y_plane || !p.uv_plane || p.y_stride < w || p.uv_stride < w ||
        p.y_stride > INT_MAX || p.uv_stride > INT_MAX) {
      ++invalid_;
      return false;
    }
    spare_.resize(size);
    for (size_t y = 0; y < h; ++y)
      std::memcpy(spare_.data() + y * w, p.y_plane + y * p.y_stride, w);
    for (size_t y = 0; y < h / 2; ++y)
      std::memcpy(spare_.data() + w * h + y * w, p.uv_plane + y * p.uv_stride,
                  w);
  } else if (f.data && f.size >= size) {
    spare_.resize(size);
    std::memcpy(spare_.data(), f.data, size);
  } else if (native && n->copy_to_nv12) {
    spare_.resize(size);
    if (n->copy_to_nv12(n->owner, spare_.data(), size) != 0) {
      ++invalid_;
      return false;
    }
  } else {
    ++invalid_;
    return false;
  }
  DecodedFrame frame{f.width,
                     f.height,
                     generation,
                     ++received_,
                     std::chrono::steady_clock::now(),
                     std::move(spare_)};
  // Keep a valid pending frame until the replacement has been copied.
  if (pending_) {
    spare_.swap(pending_->nv12);
    ++dropped_;
  }
  pending_ = std::move(frame);
  return true;
}
std::optional<DecodedFrame> LatestFrame::Take(DecodedFrame *recycle) {
  std::lock_guard lock(mutex_);
  if (!pending_)
    return {};
  auto result = std::move(pending_);
  pending_.reset();
  if (recycle && recycle->nv12.capacity() > spare_.capacity())
    spare_.swap(recycle->nv12);
  return result;
}
void LatestFrame::Reset(uint64_t generation) {
  std::lock_guard lock(mutex_);
  generation_ = generation;
  pending_.reset();
  spare_.clear();
}
nlohmann::json LatestFrame::Stats() const {
  std::lock_guard lock(mutex_);
  return {{"received", received_},
          {"replaced", dropped_},
          {"invalid", invalid_},
          {"pending", pending_ ? 1 : 0}};
}
nlohmann::json ParseBoundedJson(const std::string &text) {
  if (text.size() > 65536)
    return {};
  int depth = 0;
  bool quoted = false, escaped = false;
  for (char c : text) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
    } else if (c == '"')
      quoted = true;
    else if (c == '{' || c == '[') {
      if (++depth > 32)
        return {};
    } else if (c == '}' || c == ']') {
      if (--depth < 0)
        return {};
    }
  }
  return nlohmann::json::parse(text, nullptr, false);
}
std::string SafeText(const std::string &text, size_t limit) {
  // ASCII metadata avoids escape injection, C1 controls and bidi spoofing.
  std::string out;
  for (unsigned char c : text) {
    if (out.size() == limit)
      break;
    out += c >= 32 && c < 127 ? char(c) : '?';
  }
  return out;
}
} // namespace crossdesk
