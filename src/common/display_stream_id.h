#ifndef CROSSDESK_DISPLAY_STREAM_ID_H_
#define CROSSDESK_DISPLAY_STREAM_ID_H_

#include <cstddef>
#include <string>

namespace crossdesk {

// MiniRTC stream identifiers are protocol-facing logical identifiers. Keep
// them independent from platform display handles and user-visible names, both
// of which may change or contain duplicate/Unicode text.
inline std::string MakeDisplayStreamId(size_t display_index) {
  return "Display" + std::to_string(display_index + 1);
}

inline bool IsRegisteredDisplayStreamId(const std::string& stream_id,
                                        size_t display_count) {
  if (stream_id.empty()) {
    return false;
  }

  for (size_t index = 0; index < display_count; ++index) {
    if (stream_id == MakeDisplayStreamId(index)) {
      return true;
    }
  }
  return false;
}

// Resolves a backend-reported identifier to a stream registered with MiniRTC.
// Backends should report MakeDisplayStreamId(index); the fallbacks keep older
// capture plugins and hotplug transitions safe.
inline std::string ResolveDisplayStreamId(
    const char* reported_id, size_t display_count, int preferred_index = -1,
    const std::string& previous_id = {}) {
  const std::string candidate = reported_id ? reported_id : "";
  if (IsRegisteredDisplayStreamId(candidate, display_count)) {
    return candidate;
  }

  if (preferred_index >= 0 &&
      preferred_index < static_cast<int>(display_count)) {
    return MakeDisplayStreamId(static_cast<size_t>(preferred_index));
  }

  if (IsRegisteredDisplayStreamId(previous_id, display_count)) {
    return previous_id;
  }

  if (display_count == 1) {
    return MakeDisplayStreamId(0);
  }

  return {};
}

}  // namespace crossdesk

#endif  // CROSSDESK_DISPLAY_STREAM_ID_H_
