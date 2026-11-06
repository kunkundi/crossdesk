/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-29
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WEB_CLIPBOARD_PROTOCOL_H_
#define _WEB_CLIPBOARD_PROTOCOL_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace crossdesk::web_clipboard {

// CrossDesk application protocol, carried as opaque data by the RTC transport.
inline constexpr uint32_t kVersion = 1;
inline constexpr char kStream[] = "web-clipboard";
inline constexpr size_t kMaxTextBytes = 128 * 1024;
inline constexpr size_t kHeaderBytes = 16;
inline constexpr size_t kChunkBytes = 16 * 1024;
inline constexpr uint32_t kText = 1, kApplied = 2, kRejected = 3;
inline constexpr uint32_t kHello = 4, kReady = 5;
inline constexpr uint32_t kRead = 6, kEmpty = 7, kClear = 8;

inline uint32_t Read32(const char* data) {
  const auto* p = reinterpret_cast<const unsigned char*>(data);
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline void Write32(char* data, uint32_t value) {
  for (int i = 0; i < 4; ++i) data[i] = char(value >> (8 * i));
}

inline bool ValidText(std::string_view text) {
  if (text.empty() || text.size() > kMaxTextBytes) return false;
  const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
  for (size_t i = 0; i < text.size();) {
    const uint32_t first = bytes[i++];
    if (!first) return false;
    if (first < 0x80) continue;
    const int extra = first >= 0xc2 && first <= 0xdf ? 1
                    : first >= 0xe0 && first <= 0xef ? 2
                    : first >= 0xf0 && first <= 0xf4 ? 3 : -1;
    if (extra < 0 || text.size() - i < size_t(extra)) return false;
    uint32_t code = first & (0x7f >> (extra + 1));
    for (int j = 0; j < extra; ++j) {
      const auto next = bytes[i++];
      if ((next & 0xc0) != 0x80) return false;
      code = (code << 6) | (next & 0x3f);
    }
    if (code < (extra == 1 ? 0x80u : extra == 2 ? 0x800u : 0x10000u) ||
        code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
  }
  return true;
}

// Four little-endian uint32 fields: kind, request ID, total bytes, offset.
// ID zero is a host clipboard update. Browser writes use nonzero IDs; only
// the application may acknowledge them after writing the OS clipboard.
// HELLO/READY carry kVersion in the total field, with zero ID/offset and no payload.
inline std::string Frame(uint32_t kind, uint32_t id, uint32_t total = 0,
                         uint32_t offset = 0, std::string_view bytes = {}) {
  std::string frame(kHeaderBytes, '\0');
  Write32(frame.data(), kind);
  Write32(frame.data() + 4, id);
  Write32(frame.data() + 8, total);
  Write32(frame.data() + 12, offset);
  frame.append(bytes);
  return frame;
}

template <typename Send>
bool SendText(std::string_view text, uint32_t id, size_t message_limit,
              Send send) {
  if (!ValidText(text) ||
      message_limit <= kHeaderBytes) return false;
  const size_t chunk = std::min(kChunkBytes, message_limit - kHeaderBytes);
  for (size_t offset = 0; offset < text.size(); offset += chunk) {
    if (!send(Frame(kText, id, uint32_t(text.size()), uint32_t(offset),
                    text.substr(offset, chunk)))) return false;
  }
  return true;
}

// One bounded in-flight value per ordered reliable channel. A new offset-zero
// frame supersedes an incomplete value, allowing recovery after send failure.
class Assembler {
 public:
  std::string Receive(std::string_view frame) {
    if (frame.size() < kHeaderBytes ||
        frame.size() > kHeaderBytes + kChunkBytes) return Reset();
    const auto kind = Read32(frame.data()), id = Read32(frame.data() + 4);
    const auto total = Read32(frame.data() + 8);
    const auto offset = Read32(frame.data() + 12);
    const auto bytes = frame.substr(kHeaderBytes);
    if (kind != kText || !total || total > kMaxTextBytes || bytes.empty() ||
        offset > total || bytes.size() > total - offset ||
        bytes.find('\0') != std::string_view::npos) return Reset();
    if (offset == 0) {
      text_.clear();
      id_ = id;
      total_ = total;
    }
    if (id != id_ || total != total_ || offset != text_.size()) return Reset();
    text_.append(bytes);
    if (text_.size() != total_) return {};
    if (!ValidText(text_)) return Reset();
    auto result = Frame(kText, id_, total_, 0, text_);
    Reset();
    return result;
  }

 private:
  std::string Reset() { text_.clear(); total_ = id_ = 0; return {}; }
  std::string text_;
  uint32_t id_ = 0, total_ = 0;
};

}  // namespace crossdesk::web_clipboard

#endif
