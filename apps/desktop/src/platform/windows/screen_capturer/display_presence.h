/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-20
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef CROSSDESK_DISPLAY_PRESENCE_H_
#define CROSSDESK_DISPLAY_PRESENCE_H_

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace crossdesk {

// Without any attached monitor, Windows keeps the desktop on a phantom display
// target with an OS-chosen resolution. DXGI has no output to duplicate there,
// but GDI still reads the composed desktop and input injection keeps working.
enum class DisplayPresence { present, headless, unknown };

struct DisplayPresenceProbe {
  bool query_succeeded = false;   // QueryDisplayConfig returned a topology
  size_t active_paths = 0;        // active display paths, phantom included
  size_t available_targets = 0;   // active paths whose monitor is present
  size_t desktop_devices = 0;     // adapters attached to the desktop
  size_t attached_monitors = 0;   // monitor devices reported active
};

// Pure policy, kept separate from Win32 so it can be tested with fixtures.
inline DisplayPresence ClassifyDisplayPresence(
    const DisplayPresenceProbe& probe) {
  if (probe.query_succeeded) {
    if (probe.available_targets > 0) return DisplayPresence::present;
    if (probe.active_paths > 0 || probe.desktop_devices > 0)
      return DisplayPresence::headless;
    return DisplayPresence::unknown;
  }
  if (probe.attached_monitors > 0) return DisplayPresence::present;
  if (probe.desktop_devices > 0) return DisplayPresence::headless;
  return DisplayPresence::unknown;
}

inline DisplayPresenceProbe ProbeDisplayPresence() {
  DisplayPresenceProbe probe;
  // The buffer size can change between the two calls during a topology
  // change; a few retries cover that window.
  for (int attempt = 0; attempt < 4 && !probe.query_succeeded; ++attempt) {
    UINT32 path_count = 0;
    UINT32 mode_count = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count,
                                    &mode_count) != ERROR_SUCCESS) break;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths((std::max)(path_count, 1u));
    std::vector<DISPLAYCONFIG_MODE_INFO> modes((std::max)(mode_count, 1u));
    const LONG status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count,
                                           paths.data(), &mode_count,
                                           modes.data(), nullptr);
    if (status == ERROR_INSUFFICIENT_BUFFER) continue;
    if (status != ERROR_SUCCESS) break;
    probe.query_succeeded = true;
    for (UINT32 index = 0; index < path_count; ++index) {
      if (!(paths[index].flags & DISPLAYCONFIG_PATH_ACTIVE)) continue;
      ++probe.active_paths;
      // A removed monitor can leave its path active while the target is gone.
      if (paths[index].targetInfo.targetAvailable) ++probe.available_targets;
    }
  }
  for (DWORD index = 0;; ++index) {
    DISPLAY_DEVICEW adapter{sizeof(adapter)};
    if (!EnumDisplayDevicesW(nullptr, index, &adapter, 0)) break;
    if (!(adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
    ++probe.desktop_devices;
    for (DWORD child = 0;; ++child) {
      DISPLAY_DEVICEW monitor{sizeof(monitor)};
      if (!EnumDisplayDevicesW(adapter.DeviceName, child, &monitor, 0)) break;
      if (monitor.StateFlags & DISPLAY_DEVICE_ACTIVE) ++probe.attached_monitors;
    }
  }
  return probe;
}

}  // namespace crossdesk

#endif  // CROSSDESK_DISPLAY_PRESENCE_H_
