#ifndef CROSSDESK_COMMON_WINDOWS_INPUT_MARKER_H_
#define CROSSDESK_COMMON_WINDOWS_INPUT_MARKER_H_

#include <cstdint>

namespace crossdesk {

// SendInput copies dwExtraInfo into KBDLLHOOKSTRUCT. Tag CrossDesk-generated
// keyboard input so the controller hook can ignore only its own injections
// while still accepting input from accessibility tools or remote sessions.
inline constexpr std::uintptr_t kInjectedKeyboardInputMarker = 0x4353444B;

}  // namespace crossdesk

#endif  // CROSSDESK_COMMON_WINDOWS_INPUT_MARKER_H_
