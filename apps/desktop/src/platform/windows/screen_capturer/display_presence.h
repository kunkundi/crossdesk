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

#include "../virtual_display/usbmmidd_policy.h"

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
  size_t identified_targets = 0; // EDID, monitor interface or built-in/IDD target
  size_t unidentified_targets = 0; // available targets with no monitor identity
  size_t target_query_failures = 0;
  size_t embedded_or_virtual_targets = 0; // independent of an external monitor
  size_t edid_targets = 0;
  size_t monitor_path_targets = 0;
};

// Pure policy, kept separate from Win32 so it can be tested with fixtures.
inline DisplayPresence ClassifyDisplayPresence(
    const DisplayPresenceProbe& probe, bool capture_stalled = false) {
  if (probe.query_succeeded) {
    if (probe.available_targets > 0) {
      // NVIDIA can retain both availability AND EDID/interface identity after
      // the last unplug. Those describe a target, not a connected monitor.
      // Require stalled capture plus a desktop adapter with no active monitor,
      // and exempt built-in/wireless/IDD targets and failed target queries.
      // A remaining physical monitor (even without EDID) prevents takeover.
      if (capture_stalled && probe.desktop_devices > 0 &&
          probe.attached_monitors == 0 &&
          probe.embedded_or_virtual_targets == 0 &&
          probe.target_query_failures == 0) {
        return DisplayPresence::headless;
      }
      return DisplayPresence::present;
    }
    // A successful empty topology is also headless (e.g. hot unplug), not an
    // API failure. The caller suppresses recovery on the secure desktop.
    return DisplayPresence::headless;
  }
  if (probe.attached_monitors > 0) return DisplayPresence::present;
  if (probe.desktop_devices > 0) return DisplayPresence::headless;
  return DisplayPresence::unknown;
}

inline bool IsEmbeddedOrVirtualTarget(
    DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology) {
  switch (technology) {
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL:
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS:
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED:
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED:
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST:
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED:
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL:
      return true;
    default:
      return false;
  }
}

inline bool HasDisplayTargetIdentity(
    const DISPLAYCONFIG_TARGET_DEVICE_NAME& target) {
  return IsEmbeddedOrVirtualTarget(target.outputTechnology) ||
         target.flags.edidIdsValid ||
         (target.monitorDevicePath[0] != L'\0' &&
          !ContainsCaseInsensitive(target.monitorDevicePath, L"Default_Monitor"));
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
      const auto& info = paths[index].targetInfo;
      if (!info.targetAvailable) continue;
      ++probe.available_targets;
      if (IsEmbeddedOrVirtualTarget(info.outputTechnology))
        ++probe.embedded_or_virtual_targets;
      DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
      target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
      target.header.size = sizeof(target);
      target.header.adapterId = info.adapterId;
      target.header.id = info.id;
      if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) {
        ++probe.target_query_failures;
      } else {
        if (target.flags.edidIdsValid) ++probe.edid_targets;
        if (target.monitorDevicePath[0] != L'\0') ++probe.monitor_path_targets;
        if (HasDisplayTargetIdentity(target)) {
          ++probe.identified_targets;
        } else {
          ++probe.unidentified_targets;
        }
      }
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
