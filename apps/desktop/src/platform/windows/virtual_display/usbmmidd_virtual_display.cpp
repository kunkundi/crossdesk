/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-20
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#include "usbmmidd_virtual_display.h"

// clang-format off
#include <Windows.h>
#include <initguid.h>  // defines GUID_DEVCLASS_DISPLAY in this unit
#include <SetupAPI.h>
#include <devguid.h>
// clang-format on

#include <algorithm>
#include <string>
#include <vector>

#include "rd_log.h"

namespace crossdesk {

namespace {

constexpr DWORD kInstallerPollIntervalMs = 250;
constexpr DWORD kDriverInstallTimeoutMs = 90 * 1000;
constexpr DWORD kInstallerCommandTimeoutMs = 20 * 1000;

std::string Narrow(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string result(static_cast<size_t>((std::max)(size, 0)), '\0');
  if (size > 0) {
    WideCharToMultiByte(CP_UTF8, 0, value.data(),
                        static_cast<int>(value.size()), result.data(), size,
                        nullptr, nullptr);
  }
  return result;
}

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) *error = message;
}

std::filesystem::path CurrentExecutableDirectory() {
  wchar_t module_path[MAX_PATH] = {0};
  const DWORD length = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return {};
  return std::filesystem::path(module_path).parent_path();
}

struct UsbmmiddAdapter {
  std::wstring device_name;  // \\.\DISPLAYn
  bool attached_to_desktop = false;
};

std::vector<UsbmmiddAdapter> EnumerateUsbmmiddAdapters() {
  std::vector<UsbmmiddAdapter> adapters;
  for (DWORD index = 0;; ++index) {
    DISPLAY_DEVICEW device{sizeof(device)};
    if (!EnumDisplayDevicesW(nullptr, index, &device, 0)) break;
    if (!IsUsbmmiddAdapter(device.DeviceString, device.DeviceID)) continue;
    UsbmmiddAdapter adapter;
    adapter.device_name = device.DeviceName;
    adapter.attached_to_desktop =
        (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0;
    adapters.push_back(std::move(adapter));
  }
  return adapters;
}

// Runs "<installer> <arguments>" hidden with the package folder as the
// working directory (the installer resolves usbmmIdd.dll relative to it).
bool RunUsbmmiddInstaller(const UsbmmiddPackage& package,
                          const std::wstring& arguments, DWORD timeout_ms,
                          std::string* error) {
  if (!package.Available()) {
    SetError(error, "usbmmidd_package_missing");
    return false;
  }
  std::wstring command_line =
      L"\"" + package.installer.wstring() + L"\" " + arguments;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  const std::wstring working_directory = package.directory.wstring();
  if (!CreateProcessW(package.installer.c_str(), command_line.data(), nullptr,
                      nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      working_directory.c_str(), &startup, &process)) {
    const DWORD code = GetLastError();
    // 740 (ERROR_ELEVATION_REQUIRED) is the non-elevated GUI fallback path.
    SetError(error, "usbmmidd_installer_launch_failed:" + std::to_string(code));
    return false;
  }
  CloseHandle(process.hThread);
  const DWORD wait = WaitForSingleObject(process.hProcess, timeout_ms);
  DWORD exit_code = 0;
  if (wait != WAIT_OBJECT_0) {
    TerminateProcess(process.hProcess, 1);
    CloseHandle(process.hProcess);
    SetError(error, wait == WAIT_TIMEOUT ? "usbmmidd_installer_timeout"
                                         : "usbmmidd_installer_wait_failed");
    return false;
  }
  GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hProcess);
  if (exit_code != 0) {
    SetError(error, "usbmmidd_installer_exit:" + std::to_string(exit_code));
    return false;
  }
  return true;
}

// Waits until |predicate| holds or the deadline passes.
template <typename Predicate>
bool WaitUntil(DWORD timeout_ms, Predicate predicate) {
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  while (true) {
    if (predicate()) return true;
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline) return false;
    Sleep(static_cast<DWORD>(
        (std::min)(static_cast<ULONGLONG>(kInstallerPollIntervalMs),
                   deadline - now)));
  }
}

// The uninstaller removes the driver only when CrossDesk installed it, so a
// driver the user set up for other software is left alone. The helper runs as
// SYSTEM and the in-process path is elevated, so HKLM is writable here; a
// failure only costs a stale device node after uninstall.
void MarkUsbmmiddDriverInstalledByCrossDesk() {
  HKEY key = nullptr;
  const LONG open = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kUsbmmiddInstallMarkerKey,
                                    0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                                    nullptr);
  if (open != ERROR_SUCCESS) {
    LOG_WARN("usbmmidd: failed to record the driver install marker, error={}",
             open);
    return;
  }
  const DWORD value = 1;
  const LONG set = RegSetValueExW(key, kUsbmmiddInstallMarkerValue, 0, REG_DWORD,
                                  reinterpret_cast<const BYTE*>(&value),
                                  sizeof(value));
  RegCloseKey(key);
  if (set != ERROR_SUCCESS) {
    LOG_WARN("usbmmidd: failed to record the driver install marker, error={}",
             set);
  }
}

bool DeviceHasUsbmmiddHardwareId(HDEVINFO device_info,
                                 SP_DEVINFO_DATA* device_data) {
  DWORD required = 0;
  SetupDiGetDeviceRegistryPropertyW(device_info, device_data, SPDRP_HARDWAREID,
                                    nullptr, nullptr, 0, &required);
  if (required == 0) return false;
  std::vector<wchar_t> buffer(required / sizeof(wchar_t) + 2, L'\0');
  if (!SetupDiGetDeviceRegistryPropertyW(
          device_info, device_data, SPDRP_HARDWAREID, nullptr,
          reinterpret_cast<BYTE*>(buffer.data()),
          static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), nullptr)) {
    return false;
  }
  // REG_MULTI_SZ: hardware ids separated by NUL, terminated by a double NUL.
  for (const wchar_t* id = buffer.data(); *id != L'\0'; id += wcslen(id) + 1) {
    if (ContainsCaseInsensitive(id, kUsbmmiddHardwareId)) return true;
  }
  return false;
}

std::vector<VirtualDisplayMode> EnumerateModes(const std::wstring& device) {
  std::vector<VirtualDisplayMode> modes;
  for (DWORD index = 0;; ++index) {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsExW(device.c_str(), index, &mode, 0)) break;
    if (mode.dmBitsPerPel != 32) continue;
    const VirtualDisplayMode candidate{static_cast<int>(mode.dmPelsWidth),
                                       static_cast<int>(mode.dmPelsHeight)};
    if (std::find(modes.begin(), modes.end(), candidate) == modes.end()) {
      modes.push_back(candidate);
    }
  }
  return modes;
}

}  // namespace

UsbmmiddPackage LocateUsbmmiddPackage() {
  UsbmmiddPackage package;
  const std::filesystem::path base = CurrentExecutableDirectory();
  if (base.empty()) return package;
  std::error_code ignored;
  for (const wchar_t* relative : kUsbmmiddPackageDirectories) {
    const std::filesystem::path directory = base / relative;
    const std::filesystem::path installer = directory / kUsbmmiddInstallerName;
    if (!std::filesystem::is_regular_file(installer, ignored)) continue;
    package.directory = directory;
    package.installer = installer;
    const std::filesystem::path inf = directory / kUsbmmiddInfName;
    if (std::filesystem::is_regular_file(inf, ignored)) package.inf = inf;
    break;
  }
  return package;
}

int CountUsbmmiddMonitors() {
  int count = 0;
  for (const auto& adapter : EnumerateUsbmmiddAdapters()) {
    if (adapter.attached_to_desktop) ++count;
  }
  return count;
}

bool IsUsbmmiddDriverInstalled() {
  const HDEVINFO device_info = SetupDiGetClassDevsW(
      &GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
  if (device_info == INVALID_HANDLE_VALUE) {
    // Fall back to the adapter list; it misses an installed-but-idle driver.
    return !EnumerateUsbmmiddAdapters().empty();
  }
  bool installed = false;
  SP_DEVINFO_DATA device_data{};
  device_data.cbSize = sizeof(device_data);
  for (DWORD index = 0;
       !installed && SetupDiEnumDeviceInfo(device_info, index, &device_data);
       ++index) {
    installed = DeviceHasUsbmmiddHardwareId(device_info, &device_data);
  }
  SetupDiDestroyDeviceInfoList(device_info);
  return installed || !EnumerateUsbmmiddAdapters().empty();
}

UsbmmiddStatus QueryUsbmmiddStatus() {
  UsbmmiddStatus status;
  const UsbmmiddPackage package = LocateUsbmmiddPackage();
  status.package_available = package.Available();
  status.package_directory = Narrow(package.directory.wstring());
  status.driver_installed = IsUsbmmiddDriverInstalled();
  status.plugged_monitors = CountUsbmmiddMonitors();
  return status;
}

bool InstallUsbmmiddDriver(const UsbmmiddPackage& package, std::string* error) {
  if (IsUsbmmiddDriverInstalled()) return true;
  if (package.inf.empty()) {
    SetError(error, "usbmmidd_inf_missing");
    return false;
  }
  LOG_INFO("usbmmidd: installing driver from '{}'",
           Narrow(package.directory.wstring()));
  const std::wstring arguments = L"install \"" + package.inf.wstring() +
                                 L"\" " + kUsbmmiddHardwareId;
  if (!RunUsbmmiddInstaller(package, arguments, kDriverInstallTimeoutMs,
                            error)) {
    return false;
  }
  if (!WaitUntil(5000, IsUsbmmiddDriverInstalled)) {
    SetError(error, "usbmmidd_driver_not_present_after_install");
    return false;
  }
  MarkUsbmmiddDriverInstalledByCrossDesk();
  return true;
}

bool PlugUsbmmiddMonitor(const UsbmmiddPackage& package, DWORD timeout_ms,
                         std::string* error) {
  const int before = CountUsbmmiddMonitors();
  if (before >= kUsbmmiddMaxMonitors) {
    SetError(error, "usbmmidd_monitor_limit");
    return false;
  }
  if (!RunUsbmmiddInstaller(package, L"enableidd 1",
                            kInstallerCommandTimeoutMs, error)) {
    return false;
  }
  if (!WaitUntil(timeout_ms,
                 [before] { return CountUsbmmiddMonitors() > before; })) {
    SetError(error, "usbmmidd_monitor_not_attached");
    return false;
  }
  return true;
}

bool UnplugUsbmmiddMonitor(const UsbmmiddPackage& package, DWORD timeout_ms,
                           std::string* error) {
  const int before = CountUsbmmiddMonitors();
  if (before == 0) return true;
  if (!RunUsbmmiddInstaller(package, L"enableidd 0",
                            kInstallerCommandTimeoutMs, error)) {
    return false;
  }
  if (!WaitUntil(timeout_ms,
                 [before] { return CountUsbmmiddMonitors() < before; })) {
    SetError(error, "usbmmidd_monitor_still_attached");
    return false;
  }
  return true;
}

bool ApplyUsbmmiddDisplayMode(VirtualDisplayMode requested,
                              std::string* error) {
  std::wstring device;
  for (const auto& adapter : EnumerateUsbmmiddAdapters()) {
    if (adapter.attached_to_desktop) {
      device = adapter.device_name;
      break;
    }
  }
  if (device.empty()) {
    SetError(error, "usbmmidd_monitor_not_attached");
    return false;
  }
  VirtualDisplayMode chosen;
  if (!ChooseVirtualDisplayMode(EnumerateModes(device), requested, &chosen)) {
    SetError(error, "usbmmidd_no_display_modes");
    return false;
  }
  DEVMODEW current{};
  current.dmSize = sizeof(current);
  if (EnumDisplaySettingsExW(device.c_str(), ENUM_CURRENT_SETTINGS, &current,
                             0) &&
      static_cast<int>(current.dmPelsWidth) == chosen.width &&
      static_cast<int>(current.dmPelsHeight) == chosen.height) {
    return true;
  }
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
  mode.dmPelsWidth = static_cast<DWORD>(chosen.width);
  mode.dmPelsHeight = static_cast<DWORD>(chosen.height);
  mode.dmBitsPerPel = 32;
  const LONG result = ChangeDisplaySettingsExW(device.c_str(), &mode, nullptr,
                                               CDS_UPDATEREGISTRY, nullptr);
  if (result != DISP_CHANGE_SUCCESSFUL) {
    SetError(error, "usbmmidd_mode_change_failed:" + std::to_string(result));
    return false;
  }
  LOG_INFO("usbmmidd: display '{}' set to {}x{} (requested {}x{})",
           Narrow(device), chosen.width, chosen.height, requested.width,
           requested.height);
  return true;
}

bool ProvisionUsbmmiddDisplay(VirtualDisplayMode mode, int* plugged,
                              std::string* error) {
  if (plugged != nullptr) *plugged = 0;
  const UsbmmiddPackage package = LocateUsbmmiddPackage();
  if (!package.Available()) {
    SetError(error, "usbmmidd_package_missing");
    return false;
  }
  if (!InstallUsbmmiddDriver(package, error)) return false;
  if (CountUsbmmiddMonitors() == 0) {
    if (!PlugUsbmmiddMonitor(package, 10 * 1000, error)) return false;
    if (plugged != nullptr) *plugged = 1;
  }
  std::string mode_error;
  if (!ApplyUsbmmiddDisplayMode(mode, &mode_error)) {
    LOG_WARN("usbmmidd: keeping the driver default mode, error={}", mode_error);
  }
  return true;
}

bool ReleaseUsbmmiddDisplays(int count, std::string* error) {
  const int to_unplug = (std::min)(count, CountUsbmmiddMonitors());
  if (to_unplug <= 0) return true;
  const UsbmmiddPackage package = LocateUsbmmiddPackage();
  if (!package.Available()) {
    SetError(error, "usbmmidd_package_missing");
    return false;
  }
  for (int index = 0; index < to_unplug; ++index) {
    if (!UnplugUsbmmiddMonitor(package, 10 * 1000, error)) return false;
  }
  return true;
}

}  // namespace crossdesk
