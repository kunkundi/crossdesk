#include "runtime/gui_runtime.h"

#include <SDL3/SDL_timer.h>

#include "rd_log.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#include <cstdlib>
#include <string>

namespace crossdesk {

namespace {
constexpr uint32_t kPermissionRefreshIntervalVisibleMs = 500;

void OpenPrivacyPreferences(const char *pane) {
  if (pane == nullptr || pane[0] == '\0') {
    return;
  }

  std::string command =
      "open \"x-apple.systempreferences:com.apple.preference.security?";
  command += pane;
  command += "\"";
  system(command.c_str());
}
} // namespace

void GuiRuntime::StartMacPermissionMigration(bool retry) {
  if (mac_permission_migration_task_.valid()) return;
  mac_permission_migration_failed_ = false;
  try {
    mac_permission_migration_task_ = std::async(
        std::launch::async, [retry] { return MigrateMacPermissions(retry); });
  } catch (const std::exception& error) {
    mac_permission_migration_failed_ = true;
    LOG_ERROR("Could not start macOS permission migration: {}", error.what());
  }
}

bool GuiRuntime::CheckScreenRecordingPermission() {
  // CGPreflightScreenCaptureAccess is available on macOS 10.15+
  if (@available(macOS 10.15, *)) {
    bool granted = CGPreflightScreenCaptureAccess();
    return granted;
  }
  // for older macOS versions, assume permission is granted
  return true;
}

bool GuiRuntime::CheckAccessibilityPermission() {
  NSDictionary *options = @{(__bridge id)kAXTrustedCheckOptionPrompt : @NO};
  bool trusted =
      AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
  return trusted;
}

void GuiRuntime::OpenAccessibilityPreferences() {
  if (mac_permission_migration_task_.valid()) return;
  if (!mac_accessibility_permission_requested_) {
    NSDictionary *options = @{(__bridge id)kAXTrustedCheckOptionPrompt : @YES};
    AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
  } else {
    OpenPrivacyPreferences("Privacy_Accessibility");
  }
}

void GuiRuntime::OpenScreenRecordingPreferences() {
  if (mac_permission_migration_task_.valid()) return;
  if (@available(macOS 10.15, *)) {
    if (!mac_screen_recording_permission_requested_) {
      CGRequestScreenCaptureAccess();
    } else {
      OpenPrivacyPreferences("Privacy_ScreenCapture");
    }
  } else {
    OpenPrivacyPreferences("Privacy_ScreenCapture");
  }
}

void GuiRuntime::RefreshMacPermissionStatus(bool force) {
  if (mac_permission_migration_task_.valid() &&
      mac_permission_migration_task_.wait_for(std::chrono::seconds(0)) ==
          std::future_status::ready) {
    try {
      const auto result = mac_permission_migration_task_.get();
      mac_permission_migration_failed_ = result.needs_repair;
      if (result.needs_repair) LOG_WARN("macOS permission migration: {}", result.detail);
    } catch (const std::exception& error) {
      mac_permission_migration_failed_ = true;
      LOG_ERROR("macOS permission migration failed: {}", error.what());
    }
    force = true;
  }
  const uint32_t now = static_cast<uint32_t>(SDL_GetTicks());
  if (!force && mac_permission_status_initialized_ &&
      now - mac_permission_last_check_tick_ <
          kPermissionRefreshIntervalVisibleMs) {
    return;
  }

  const bool old_screen_recording_granted =
      mac_screen_recording_permission_granted_;
  const bool old_accessibility_granted = mac_accessibility_permission_granted_;

  mac_screen_recording_permission_granted_ = CheckScreenRecordingPermission();
  mac_accessibility_permission_granted_ = CheckAccessibilityPermission();
  mac_permission_last_check_tick_ = now;
  mac_permission_status_initialized_ = true;

  if (old_screen_recording_granted !=
          mac_screen_recording_permission_granted_ ||
      old_accessibility_granted != mac_accessibility_permission_granted_) {
    LOG_INFO("macOS permission status: screen_recording={}, accessibility={}",
             mac_screen_recording_permission_granted_,
             mac_accessibility_permission_granted_);
  }
}

bool GuiRuntime::EnsureMacScreenRecordingPermission() {
  RefreshMacPermissionStatus(false);
  if (mac_screen_recording_permission_granted_) {
    return true;
  }

  show_request_permission_window_ = !mac_permission_dialog_dismissed_;
  return false;
}

bool GuiRuntime::EnsureMacAccessibilityPermission() {
  RefreshMacPermissionStatus(false);
  if (mac_accessibility_permission_granted_) {
    return true;
  }

  show_request_permission_window_ = !mac_permission_dialog_dismissed_;
  return false;
}

} // namespace crossdesk
