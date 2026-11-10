/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-08
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _AUTOSTART_CONTROL_H_
#define _AUTOSTART_CONTROL_H_

#include <string>

namespace crossdesk {

// Windows startup preferences control the machine host and its GUI handoff.
// Portable and Debug builds keep the existing unattended-mode restriction.
// Evaluate in the application target: callers such as config_center do not
// carry the application's CROSSDESK_PORTABLE define.
bool SupportsServiceAutostart();
bool IsServiceAutostartEnabled();
// True when the registration already matches `enabled`, so saving settings
// does not stop, reinstall or re-register anything.
bool ServiceAutostartMatches(bool enabled);
// Shared GUI/CLI entry point; serializes service changes and preserves a
// running host when the installed service already matches this executable.
bool SetServiceAutostart(bool enabled, std::string& error);

// Logon startup only exists as the GUI handoff of an enabled machine host.
// Remove registrations left by versions before the service-based startup.
bool CleanupLegacyServiceAutostart();

}  // namespace crossdesk

#endif
