/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DISPLAY_RESOLUTION_WIN_H_
#define _DISPLAY_RESOLUTION_WIN_H_

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "display_info.h"
#include "display_label.h"
#include <display_resolution.h>

namespace crossdesk {

// Injectable Win32 calls keep mode selection and failure handling testable
// without changing the developer's desktop.
struct WindowsResolutionApi {
  decltype(&EnumDisplaySettingsW) enumerate = &EnumDisplaySettingsW;
  decltype(&ChangeDisplaySettingsExW) change = &ChangeDisplaySettingsExW;
};

inline std::vector<DEVMODEW>
WindowsResolutionModes(const wchar_t *device, const DEVMODEW &current,
                       const WindowsResolutionApi &api = {}) {
  std::map<std::pair<int, int>, DEVMODEW> modes;
  for (DWORD index = 0; index < 16384; ++index) {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!api.enumerate(device, index, &mode))
      break;
    if (mode.dmBitsPerPel != current.dmBitsPerPel ||
        (mode.dmDisplayFlags & DM_INTERLACED) || mode.dmPelsWidth > 32768 ||
        mode.dmPelsHeight > 32768 ||
        !ValidDisplayResolution(
            {int(mode.dmPelsWidth), int(mode.dmPelsHeight)}))
      continue;
    // Keep orientation, placement and scaling while choosing the nearest
    // supported refresh rate to the existing desktop mode.
    mode.dmPosition = current.dmPosition;
    mode.dmDisplayOrientation = current.dmDisplayOrientation;
    mode.dmDisplayFixedOutput = current.dmDisplayFixedOutput;
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL |
                    DM_DISPLAYFLAGS | DM_DISPLAYFREQUENCY |
                    (current.dmFields & (DM_POSITION | DM_DISPLAYORIENTATION |
                                         DM_DISPLAYFIXEDOUTPUT));
    const auto key =
        std::make_pair(int(mode.dmPelsWidth), int(mode.dmPelsHeight));
    const auto found = modes.find(key);
    if (found == modes.end() ||
        std::abs(double(mode.dmDisplayFrequency) - current.dmDisplayFrequency) <
            std::abs(double(found->second.dmDisplayFrequency) -
                     current.dmDisplayFrequency))
      modes[key] = mode;
  }
  std::vector<DEVMODEW> result;
  for (auto it = modes.rbegin(); it != modes.rend(); ++it) {
    result.push_back(it->second);
    if (result.size() == kMaxDisplayResolutionModes)
      break;
  }
  return result;
}

inline bool ApplyWindowsResolutionMode(const wchar_t *device,
                                       DisplayResolution requested,
                                       const WindowsResolutionApi &api = {}) {
  if (!ValidDisplayResolution(requested))
    return false;
  DEVMODEW current{};
  current.dmSize = sizeof(current);
  if (!api.enumerate(device, ENUM_CURRENT_SETTINGS, &current))
    return false;
  for (auto mode : WindowsResolutionModes(device, current, api)) {
    if (mode.dmPelsWidth != requested.width ||
        mode.dmPelsHeight != requested.height)
      continue;
    if (current.dmPelsWidth == requested.width &&
        current.dmPelsHeight == requested.height)
      return true;
    if (api.change(device, &mode, nullptr, CDS_TEST, nullptr) !=
        DISP_CHANGE_SUCCESSFUL)
      return false;
    // Dynamic change only; never persist remote choices in the user's registry.
    if (api.change(device, &mode, nullptr, 0, nullptr) !=
        DISP_CHANGE_SUCCESSFUL)
      return false;
    DEVMODEW applied{};
    applied.dmSize = sizeof(applied);
    if (api.enumerate(device, ENUM_CURRENT_SETTINGS, &applied) &&
        applied.dmPelsWidth == requested.width &&
        applied.dmPelsHeight == requested.height)
      return true;
    api.change(device, &current, nullptr, 0, nullptr);
    return false;
  }
  return false;
}

inline bool WindowsResolutionDeviceKey(const std::wstring &device,
                                       std::string &key) {
  DISPLAY_DEVICEW target{};
  target.cb = sizeof(target);
  if (!EnumDisplayDevicesW(device.c_str(), 0, &target,
                           EDD_GET_DEVICE_INTERFACE_NAME) ||
      !target.DeviceID[0])
    return false;
  key = WideToUtf8(device) + "|" + WideToUtf8(target.DeviceID);
  return true;
}

inline bool ResolveWindowsResolutionDisplay(const DisplayInfo &display,
                                            std::wstring &device,
                                            std::string &key) {
  if (display.width <= 0 || display.height <= 0)
    return false;
  MONITORINFOEXW monitor{};
  monitor.cbSize = sizeof(monitor);
  if (display.handle &&
      GetMonitorInfoW(static_cast<HMONITOR>(display.handle), &monitor)) {
    if (GetDisplayLabel(monitor.szDevice) != display.name)
      return false;
    device = monitor.szDevice;
  } else {
    // A mode change can invalidate HMONITOR before capture publishes its new
    // geometry. Resolve the same native output; the interface key below still
    // protects change requests against a different monitor occupying the slot.
    for (DWORD index = 0; index < 256; ++index) {
      DISPLAY_DEVICEW adapter{};
      adapter.cb = sizeof(adapter);
      if (!EnumDisplayDevicesW(nullptr, index, &adapter, 0))
        break;
      if ((adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) &&
          GetDisplayLabel(adapter.DeviceName) == display.name) {
        device = adapter.DeviceName;
        break;
      }
    }
    if (device.empty())
      return false;
  }
  return WindowsResolutionDeviceKey(device, key);
}

inline bool SameWindowsResolutionMode(const DEVMODEW &mode,
                                      const DEVMODEW &original) {
  return mode.dmPelsWidth == original.dmPelsWidth &&
         mode.dmPelsHeight == original.dmPelsHeight &&
         mode.dmBitsPerPel == original.dmBitsPerPel &&
         mode.dmDisplayFrequency == original.dmDisplayFrequency &&
         mode.dmDisplayFlags == original.dmDisplayFlags &&
         (!(original.dmFields & DM_DISPLAYORIENTATION) ||
          mode.dmDisplayOrientation == original.dmDisplayOrientation) &&
         (!(original.dmFields & DM_DISPLAYFIXEDOUTPUT) ||
          mode.dmDisplayFixedOutput == original.dmDisplayFixedOutput);
}

inline std::function<bool()> CaptureWindowsDisplayResolution(
    const std::vector<DisplayInfo> &displays,
    const DisplayResolutionChange &change,
    const WindowsResolutionApi &api = {}) {
  if (change.display_id < 0 || change.display_id >= int(displays.size()))
    return {};
  std::wstring device;
  std::string key;
  if (!ResolveWindowsResolutionDisplay(displays[change.display_id], device, key) ||
      key != change.display_key)
    return {};
  DEVMODEW original{};
  original.dmSize = sizeof(original);
  if (!api.enumerate(device.c_str(), ENUM_CURRENT_SETTINGS, &original)) return {};
  return [device, key, original, api] {
    std::string current_key;
    // Resolve the native output, not a capture slot or stale HMONITOR. Never
    // apply a saved mode to a different monitor plugged into the same output.
    if (!WindowsResolutionDeviceKey(device, current_key) || current_key != key)
      return false;
    DEVMODEW current{};
    current.dmSize = sizeof(current);
    if (!api.enumerate(device.c_str(), ENUM_CURRENT_SETTINGS, &current)) return false;
    if (SameWindowsResolutionMode(current, original)) return true;
    auto mode = original;
    if (api.change(device.c_str(), &mode, nullptr, CDS_TEST, nullptr) !=
            DISP_CHANGE_SUCCESSFUL ||
        api.change(device.c_str(), &mode, nullptr, 0, nullptr) !=
            DISP_CHANGE_SUCCESSFUL)
      return false;
    return api.enumerate(device.c_str(), ENUM_CURRENT_SETTINGS, &current) &&
           SameWindowsResolutionMode(current, original);
  };
}

inline std::vector<DisplayResolutionInfo>
QueryWindowsDisplayResolutions(const std::vector<DisplayInfo> &displays) {
  std::vector<DisplayResolutionInfo> result;
  for (int index = 0;
       index < int(displays.size()) && index < kMaxResolutionDisplays;
       ++index) {
    DisplayResolutionInfo info;
    info.display_id = index;
    std::wstring device;
    if (!ResolveWindowsResolutionDisplay(displays[index], device,
                                         info.display_key))
      continue;
    DEVMODEW current{};
    current.dmSize = sizeof(current);
    if (!EnumDisplaySettingsW(device.c_str(), ENUM_CURRENT_SETTINGS, &current))
      continue;
    info.current = {int(current.dmPelsWidth), int(current.dmPelsHeight)};
    if (!ValidDisplayResolution(info.current))
      continue;
    for (const auto &mode : WindowsResolutionModes(device.c_str(), current))
      info.modes.push_back({int(mode.dmPelsWidth), int(mode.dmPelsHeight)});
    result.push_back(std::move(info));
  }
  return result;
}

inline bool
ApplyWindowsDisplayResolution(const std::vector<DisplayInfo> &displays,
                              const DisplayResolutionChange &change) {
  if (change.display_id < 0 || change.display_id >= int(displays.size()))
    return false;
  std::wstring device;
  std::string key;
  if (!ResolveWindowsResolutionDisplay(displays[change.display_id], device,
                                       key) ||
      key != change.display_key)
    return false;
  return ApplyWindowsResolutionMode(device.c_str(), change.resolution);
}

} // namespace crossdesk

#endif
