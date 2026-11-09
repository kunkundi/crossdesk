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
bool SupportsServiceAutostart();
bool IsServiceAutostartEnabled();
// Shared GUI/CLI entry point; serializes service changes and preserves a
// running host when the installed service already matches this executable.
bool SetServiceAutostart(bool enabled, std::string& error);

// Keep a legacy Run entry from bypassing the elevated GUI handoff task.
bool CleanupLegacyServiceAutostart();

}  // namespace crossdesk

#endif