/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-07
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SLINT_BACKEND_H_
#define _SLINT_BACKEND_H_

#include <cstdint>
#include <string>

namespace crossdesk {

struct SlintBackendSelection {
  bool success = true;
  std::string backend;
  std::string diagnostic;
};

inline constexpr char kSlintRendererProbeArgument[] = "--slint-renderer-probe";

// Call before creating any Slint components. The executable's main() must
// dispatch kSlintRendererProbeArgument before services, settings or the daemon.
SlintBackendSelection ConfigureWindowsSlintBackend(
    uint32_t probe_timeout_ms = 10000);

// Internal child-process entry point; never call in the main application.
int RunSlintRendererProbe();

}  // namespace crossdesk

#endif