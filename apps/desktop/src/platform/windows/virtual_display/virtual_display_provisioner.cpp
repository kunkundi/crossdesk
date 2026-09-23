/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-20
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#include "virtual_display_provisioner.h"

#include <algorithm>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "display_presence.h"
#include "named_pipe_deadline.h"
#include "rd_log.h"
#include "session_helper_shared.h"
#include "usbmmidd_virtual_display.h"

namespace crossdesk {

namespace {

using Json = nlohmann::json;

constexpr DWORD kHelperStatusTimeoutMs = 500;
constexpr DWORD kHelperCommandTimeoutMs = 2000;
constexpr DWORD kHelperPollIntervalMs = 250;
constexpr DWORD kPlugTimeoutMs = 20 * 1000;
constexpr DWORD kInstallAndPlugTimeoutMs = 120 * 1000;
constexpr DWORD kUnplugTimeoutMs = 10 * 1000;
constexpr DWORD kPresenceSettleTimeoutMs = 5 * 1000;

struct HelperVirtualDisplayStatus {
  bool ok = false;
  bool package_available = false;
  bool driver_installed = false;
  int plugged_monitors = 0;
  bool busy = false;
  std::string last_error;
  std::string error;  // transport or helper-reported error
};

std::wstring OwnSessionHelperPipeName() {
  DWORD session_id = 0xFFFFFFFF;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id)) return {};
  return GetCrossDeskSessionHelperPipeName(session_id);
}

bool QueryHelper(const std::wstring& pipe, const std::string& command,
                 DWORD timeout_ms, Json* json, std::string* error) {
  std::vector<uint8_t> response;
  DWORD code = 0;
  if (!QueryNamedPipeWithDeadline(pipe, command, timeout_ms, &response, error,
                                  &code)) {
    *error += ":" + std::to_string(code);
    return false;
  }
  *json = Json::parse(response.begin(), response.end(), nullptr, false);
  if (json->is_discarded() || !json->is_object()) {
    *error = "invalid_helper_json";
    return false;
  }
  if (!json->value("ok", false)) {
    *error = json->value("error", std::string("helper_error"));
    return false;
  }
  return true;
}

HelperVirtualDisplayStatus QueryHelperStatus(const std::wstring& pipe) {
  HelperVirtualDisplayStatus status;
  Json json;
  if (!QueryHelper(pipe, kCrossDeskVirtualDisplayStatusCommand,
                   kHelperStatusTimeoutMs, &json, &status.error)) {
    return status;
  }
  status.ok = true;
  status.package_available = json.value("package_available", false);
  status.driver_installed = json.value("driver_installed", false);
  status.plugged_monitors = json.value("plugged_monitors", 0);
  status.busy = json.value("busy", false);
  status.last_error = json.value("last_error", std::string());
  return status;
}

// Waits for the helper job to finish; returns false with the job's error.
// |cancelled| stops the wait early (the helper job itself keeps running).
bool WaitForHelperJob(const std::wstring& pipe, DWORD timeout_ms,
                      const std::function<bool()>& cancelled,
                      HelperVirtualDisplayStatus* final_status,
                      std::string* error) {
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  while (true) {
    *final_status = QueryHelperStatus(pipe);
    if (!final_status->ok) {
      *error = final_status->error;
      return false;
    }
    if (!final_status->busy) {
      if (!final_status->last_error.empty()) {
        *error = final_status->last_error;
        return false;
      }
      return true;
    }
    if (cancelled()) {
      *error = "cancelled";
      return false;
    }
    if (GetTickCount64() >= deadline) {
      *error = "helper_job_timeout";
      return false;
    }
    Sleep(kHelperPollIntervalMs);
  }
}

bool WaitForDisplayPresence(DWORD timeout_ms,
                            const std::function<bool()>& cancelled) {
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  while (true) {
    if (ClassifyDisplayPresence(ProbeDisplayPresence()) ==
        DisplayPresence::present) {
      return true;
    }
    if (cancelled() || GetTickCount64() >= deadline) return false;
    Sleep(kHelperPollIntervalMs);
  }
}

}  // namespace

bool VirtualDisplayProvisioner::AcquireViaHelper(VirtualDisplayMode mode,
                                                 std::string* error,
                                                 bool* helper_reachable) {
  *helper_reachable = false;
  const std::wstring pipe = OwnSessionHelperPipeName();
  if (pipe.empty()) {
    *error = "session_id_unavailable";
    return false;
  }
  HelperVirtualDisplayStatus status = QueryHelperStatus(pipe);
  if (!status.ok) {
    *error = status.error;
    return false;
  }
  *helper_reachable = true;
  if (!status.package_available) {
    *error = "usbmmidd_package_missing";
    return false;
  }
  const auto cancelled = [this] { return Cancelled(); };
  if (status.busy) {
    // A previous unplug may still be running; let it finish first.
    if (!WaitForHelperJob(pipe, kUnplugTimeoutMs, cancelled, &status, error))
      return false;
  }
  Json json;
  if (!QueryHelper(pipe, BuildVirtualDisplayPlugCommand(mode),
                   kHelperCommandTimeoutMs, &json, error)) {
    return false;
  }
  const DWORD timeout =
      status.driver_installed ? kPlugTimeoutMs : kInstallAndPlugTimeoutMs;
  if (!WaitForHelperJob(pipe, timeout, cancelled, &status, error)) {
    // The helper finishes the plug on its own; Release() must undo it.
    if (*error == "cancelled") pending_helper_release_ = true;
    return false;
  }
  if (status.plugged_monitors <= 0) {
    *error = "usbmmidd_monitor_not_attached";
    return false;
  }
  return true;
}

bool VirtualDisplayProvisioner::AcquireLocally(VirtualDisplayMode mode,
                                               std::string* error) {
  if (!LocateUsbmmiddPackage().Available()) {
    *error = "usbmmidd_package_missing";
    return false;
  }
  return ProvisionUsbmmiddDisplay(mode, &plugged_locally_, error);
}

bool VirtualDisplayProvisioner::Acquire(VirtualDisplayMode mode,
                                        std::string* error) {
  std::string local_error;
  if (error == nullptr) error = &local_error;
  if (active_.load(std::memory_order_relaxed)) return true;
  if (Cancelled()) {
    *error = "cancelled";
    return false;
  }

  bool helper_reachable = false;
  const ULONGLONG started = GetTickCount64();
  bool ok = AcquireViaHelper(mode, error, &helper_reachable);
  if (ok) {
    via_helper_ = true;
  } else if (!helper_reachable && *error != "cancelled") {
    LOG_INFO("Virtual display: session helper unreachable ({}), trying "
             "in-process provisioning",
             *error);
    ok = AcquireLocally(mode, error);
    via_helper_ = false;
  }
  if (!ok) {
    LOG_WARN("Virtual display: provisioning failed after {} ms, error={}",
             GetTickCount64() - started, *error);
    return false;
  }
  // The desktop moves onto the new target asynchronously; DXGI needs it.
  if (!WaitForDisplayPresence(kPresenceSettleTimeoutMs,
                              [this] { return Cancelled(); })) {
    LOG_WARN("Virtual display: monitor plugged but no display target became "
             "available within {} ms",
             kPresenceSettleTimeoutMs);
  }
  active_.store(true, std::memory_order_relaxed);
  LOG_INFO("Virtual display: usbmmidd monitor ready via {} in {} ms "
           "(requested {}x{})",
           via_helper_ ? "session helper" : "this process",
           GetTickCount64() - started, mode.width, mode.height);
  return true;
}

void VirtualDisplayProvisioner::Release() {
  const bool was_active = active_.exchange(false, std::memory_order_relaxed);
  // via_helper_ is only set by a completed helper plug; a cancelled one is
  // tracked by pending_helper_release_ instead.
  const bool release_helper =
      (via_helper_ && was_active) || pending_helper_release_;
  const bool release_local = !release_helper && plugged_locally_ > 0;
  if (!release_helper && !release_local) return;
  std::string error;
  bool ok = false;
  if (release_helper) {
    const std::wstring pipe = OwnSessionHelperPipeName();
    Json json;
    HelperVirtualDisplayStatus status;
    ok = !pipe.empty() &&
         QueryHelper(pipe, kCrossDeskVirtualDisplayUnplugCommand,
                     kHelperCommandTimeoutMs, &json, &error);
    // Wait for the unplug so a Start() right after this Stop() probes the
    // desktop without the outgoing monitor. A plug still running in the helper
    // queues the unplug behind itself; the next Acquire() waits for that job.
    if (ok && !json.value("queued", false)) {
      ok = WaitForHelperJob(pipe, kUnplugTimeoutMs, [] { return false; },
                            &status, &error);
    }
    if (ok) pending_helper_release_ = false;
  } else {
    ok = ReleaseUsbmmiddDisplays(plugged_locally_, &error);
    if (ok) plugged_locally_ = 0;
  }
  if (ok) {
    LOG_INFO("Virtual display: usbmmidd monitor released");
  } else {
    LOG_WARN("Virtual display: release failed, error={}", error);
  }
}

}  // namespace crossdesk
