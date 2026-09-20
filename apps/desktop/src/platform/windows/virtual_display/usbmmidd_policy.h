/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-20
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef CROSSDESK_USBMMIDD_POLICY_H_
#define CROSSDESK_USBMMIDD_POLICY_H_

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cwctype>
#include <string>
#include <vector>

namespace crossdesk {

// Amyuni "USB Mobile Monitor" indirect display driver (usbmmidd_v2). The
// package ships deviceinstaller64.exe, usbmmIdd.inf/.cat and usbmmIdd.dll. It
// is bundled from apps/desktop/resources/windows/usbmmidd_v2 by the installer
// and the portable archive; a user may also drop it next to the executables.
inline constexpr wchar_t kUsbmmiddHardwareId[] = L"usbmmidd";
// Set when CrossDesk itself installed the driver. The NSIS uninstaller reads
// it (64-bit registry view) and removes the driver only in that case.
inline constexpr wchar_t kUsbmmiddInstallMarkerKey[] = L"SOFTWARE\\CrossDesk";
inline constexpr wchar_t kUsbmmiddInstallMarkerValue[] =
    L"UsbmmiddDriverInstalled";
inline constexpr wchar_t kUsbmmiddAdapterDescription[] =
    L"USB Mobile Monitor Virtual Display";
#if defined(_WIN64)
inline constexpr wchar_t kUsbmmiddInstallerName[] = L"deviceinstaller64.exe";
#else
inline constexpr wchar_t kUsbmmiddInstallerName[] = L"deviceinstaller.exe";
#endif
inline constexpr wchar_t kUsbmmiddInfName[] = L"usbmmidd.inf";
inline constexpr const wchar_t* kUsbmmiddPackageDirectories[] = {
    L"usbmmidd_v2", L"usbmmidd", L"drivers\\usbmmidd_v2"};
// The driver accepts up to four monitors; remote capture needs one.
inline constexpr int kUsbmmiddMaxMonitors = 4;

// Session helper IPC sub-protocol (see session_helper_shared.h for the pipe).
// Plug/unplug run asynchronously in the helper; poll the status command.
inline constexpr char kCrossDeskVirtualDisplayStatusCommand[] =
    "virtual-display-status";
inline constexpr char kCrossDeskVirtualDisplayPlugCommandPrefix[] =
    "virtual-display-plug:";
inline constexpr char kCrossDeskVirtualDisplayUnplugCommand[] =
    "virtual-display-unplug";

struct VirtualDisplayMode {
  int width = 0;
  int height = 0;
  bool operator==(const VirtualDisplayMode& other) const {
    return width == other.width && height == other.height;
  }
  bool operator!=(const VirtualDisplayMode& other) const {
    return !(*this == other);
  }
};

inline constexpr VirtualDisplayMode kDefaultVirtualDisplayMode{1920, 1080};

inline bool ContainsCaseInsensitive(const std::wstring& haystack,
                                    const std::wstring& needle) {
  if (needle.empty()) return true;
  if (haystack.size() < needle.size()) return false;
  const auto it = std::search(
      haystack.begin(), haystack.end(), needle.begin(), needle.end(),
      [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); });
  return it != haystack.end();
}

// EnumDisplayDevices reports the adapter description plus a PnP id such as
// "PCI\VEN_..." or "ROOT\DISPLAY\0000". The IDD adapter carries the hardware
// id in its device id; the description is a second signal for older packages.
inline bool IsUsbmmiddAdapter(const std::wstring& device_string,
                              const std::wstring& device_id) {
  return ContainsCaseInsensitive(device_id, kUsbmmiddHardwareId) ||
         ContainsCaseInsensitive(device_string, kUsbmmiddAdapterDescription);
}

// Exact match first; otherwise the largest mode that fits inside the request,
// so a 4K request on a 1080p-only package still yields the best available
// picture; otherwise the smallest mode the driver offers.
inline bool ChooseVirtualDisplayMode(
    const std::vector<VirtualDisplayMode>& supported,
    VirtualDisplayMode requested, VirtualDisplayMode* chosen) {
  if (chosen == nullptr || supported.empty()) return false;
  const VirtualDisplayMode* best = nullptr;
  const VirtualDisplayMode* smallest = nullptr;
  auto area = [](const VirtualDisplayMode& mode) {
    return static_cast<long long>(mode.width) * mode.height;
  };
  for (const auto& mode : supported) {
    if (mode.width <= 0 || mode.height <= 0) continue;
    if (mode == requested) {
      *chosen = mode;
      return true;
    }
    if (mode.width <= requested.width && mode.height <= requested.height &&
        (best == nullptr || area(mode) > area(*best))) {
      best = &mode;
    }
    if (smallest == nullptr || area(mode) < area(*smallest)) smallest = &mode;
  }
  if (best != nullptr) {
    *chosen = *best;
    return true;
  }
  if (smallest != nullptr) {
    *chosen = *smallest;
    return true;
  }
  return false;
}

// "virtual-display-plug:1920x1080"
inline std::string BuildVirtualDisplayPlugCommand(VirtualDisplayMode mode) {
  return std::string(kCrossDeskVirtualDisplayPlugCommandPrefix) +
         std::to_string(mode.width) + "x" + std::to_string(mode.height);
}

inline bool ParseVirtualDisplayPlugCommand(const std::string& command,
                                           VirtualDisplayMode* mode) {
  if (mode == nullptr) return false;
  const std::string prefix(kCrossDeskVirtualDisplayPlugCommandPrefix);
  if (command.rfind(prefix, 0) != 0) return false;
  const std::string payload = command.substr(prefix.size());
  const size_t separator = payload.find('x');
  if (separator == std::string::npos || separator == 0 ||
      separator + 1 >= payload.size()) {
    return false;
  }
  char* end = nullptr;
  const long width = std::strtol(payload.c_str(), &end, 10);
  if (end != payload.c_str() + separator) return false;
  const long height = std::strtol(payload.c_str() + separator + 1, &end, 10);
  if (end == nullptr || *end != '\0') return false;
  if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return false;
  mode->width = static_cast<int>(width);
  mode->height = static_cast<int>(height);
  return true;
}

}  // namespace crossdesk

#endif  // CROSSDESK_USBMMIDD_POLICY_H_
