/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-08
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DESKTOP_TRANSITION_POLICY_H_
#define _DESKTOP_TRANSITION_POLICY_H_

#include <cstdint>
#include <string>

namespace crossdesk {

inline bool PreferUserDesktopInput(bool user_desktop_active,
                                   bool consent_ui_visible) {
  // Windows can display an elevated UAC dialog on Default when secure-desktop
  // prompting is disabled. Preserve its authorized SYSTEM input path.
  return user_desktop_active && !consent_ui_visible;
}

inline bool IsDesktopInputSetupPending(const std::string& error) {
  // These failures explicitly precede injection; timeouts remain ambiguous.
  return error == "secure_input_helper_not_ready" ||
         error == "secure_input_not_active";
}

inline bool IsCurrentDesktopSample(uint64_t sample_started, uint64_t event_tick,
                                   bool lock_known, bool session_locked,
                                   bool sample_locked) {
  // A reply received after a notification can still contain a pre-event sample.
  return (event_tick == 0 || sample_started > event_tick) &&
         (!lock_known || session_locked == sample_locked);
}

struct DesktopInputResult {
  bool delivered = false;
  // True only when the path explicitly refused the input before injection.
  // A pipe timeout may have happened after a click/key was applied: never
  // replay it.
  bool not_injected = false;
};

template <typename ProbeUserDesktop, typename SendUser, typename SendSecure>
bool DispatchDesktopInput(ProbeUserDesktop probe_user, SendUser send_user,
                          SendSecure send_secure) {
  const bool user = probe_user();
  const auto result = user ? send_user() : send_secure();
  if (result.delivered) return true;
  if (!result.not_injected) return false;
  const bool current_user = probe_user();
  if (current_user == user) return false;
  // Handle a switch between the probe and injection exactly once. Ordering is
  // synchronous, so button/key releases cannot overtake their corresponding
  // downs.
  return (current_user ? send_user() : send_secure()).delivered;
}

}  // namespace crossdesk
#endif  // _DESKTOP_TRANSITION_POLICY_H_
