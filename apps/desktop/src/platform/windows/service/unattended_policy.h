#pragma once

#include <cstdint>

namespace crossdesk {
enum class UnattendedProcessAction { keep, start, stop };

// No logged-in-user condition: a console at the sign-in screen is a valid
// target. Lock/unlock and GUI presence must not interrupt an existing host.
inline UnattendedProcessAction UnattendedAction(bool enabled,
                                                uint32_t console_session,
                                                bool running,
                                                uint32_t process_session) {
  constexpr uint32_t unavailable = 0xFFFFFFFFu;
  const bool target =
      enabled && console_session != unavailable && console_session != 0;
  if (running)
    return target && process_session == console_session
               ? UnattendedProcessAction::keep
               : UnattendedProcessAction::stop;
  return target ? UnattendedProcessAction::start
                : UnattendedProcessAction::keep;
}
}  // namespace crossdesk
