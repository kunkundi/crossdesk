/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-20
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef CROSSDESK_USBMMIDD_VIRTUAL_DISPLAY_H_
#define CROSSDESK_USBMMIDD_VIRTUAL_DISPLAY_H_

#include <Windows.h>

#include <filesystem>
#include <string>

#include "usbmmidd_policy.h"

namespace crossdesk {

// Plug/unplug talks to the driver through Amyuni's installer and needs
// administrator rights inside the interactive session. The session helper
// (SYSTEM, console session) is the intended caller; the GUI only calls these
// directly when it is elevated and no helper is reachable.

struct UsbmmiddPackage {
  std::filesystem::path directory;
  std::filesystem::path installer;
  std::filesystem::path inf;  // empty when only the installer is present
  bool Available() const { return !installer.empty(); }
};

// Looks next to the running executable (usbmmidd_v2\, usbmmidd\,
// drivers\usbmmidd_v2\). Both the GUI and the helper live in the same folder.
UsbmmiddPackage LocateUsbmmiddPackage();

struct UsbmmiddStatus {
  bool package_available = false;
  bool driver_installed = false;
  int plugged_monitors = 0;
  std::string package_directory;
};
UsbmmiddStatus QueryUsbmmiddStatus();

// Adapters attached to the desktop, i.e. monitors currently plugged in.
int CountUsbmmiddMonitors();
// Root-enumerated device node present, plugged or not.
bool IsUsbmmiddDriverInstalled();

// Each step is idempotent so a retry after a partial failure converges.
bool InstallUsbmmiddDriver(const UsbmmiddPackage& package, std::string* error);
bool PlugUsbmmiddMonitor(const UsbmmiddPackage& package, DWORD timeout_ms,
                         std::string* error);
bool UnplugUsbmmiddMonitor(const UsbmmiddPackage& package, DWORD timeout_ms,
                           std::string* error);
// Applies the closest supported mode to the first plugged usbmmidd monitor.
bool ApplyUsbmmiddDisplayMode(VirtualDisplayMode requested, std::string* error);

// Installs the driver on first use, plugs one monitor when none is attached
// and applies |mode|. A mode failure is logged but keeps the display.
// |plugged| receives how many monitors this call plugged (0 when an existing
// usbmmidd monitor was reused) so the caller can release exactly those.
bool ProvisionUsbmmiddDisplay(VirtualDisplayMode mode, int* plugged,
                              std::string* error);
// Unplugs up to |count| usbmmidd monitors, never more than are attached. A
// monitor plugged by the user or another program is left alone.
bool ReleaseUsbmmiddDisplays(int count, std::string* error);

}  // namespace crossdesk

#endif  // CROSSDESK_USBMMIDD_VIRTUAL_DISPLAY_H_
