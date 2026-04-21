#ifndef _CROSSDESK_INTERACTIVE_STATE_H_
#define _CROSSDESK_INTERACTIVE_STATE_H_

#include <string>

namespace crossdesk {

inline bool IsSecureDesktopInteractionRequired(
    const std::string& interactive_stage) {
  return interactive_stage == "credential-ui" ||
         interactive_stage == "secure-desktop";
}

inline bool ShouldNormalizeUnlockToUserDesktop(
    bool interactive_lock_screen_visible, const std::string& interactive_stage,
    bool session_locked, bool interactive_logon_ui_visible,
    bool interactive_secure_desktop_active, bool credential_ui_visible,
    bool password_box_visible, bool unlock_ui_visible,
    const std::string& last_session_event) {
  if (!interactive_lock_screen_visible && interactive_stage != "lock-screen") {
    return false;
  }

  if (session_locked || interactive_logon_ui_visible ||
      interactive_secure_desktop_active || credential_ui_visible ||
      password_box_visible || unlock_ui_visible) {
    return false;
  }

  return last_session_event != "session-lock";
}

}  // namespace crossdesk

#endif