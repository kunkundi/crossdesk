/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-20
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef CROSSDESK_VIRTUAL_DISPLAY_PROVISIONER_H_
#define CROSSDESK_VIRTUAL_DISPLAY_PROVISIONER_H_

#include <Windows.h>

#include <atomic>
#include <string>

#include "usbmmidd_policy.h"

namespace crossdesk {

// Session-scoped usbmmidd virtual display for a host without any monitor.
// Acquire() asks the session helper (SYSTEM in the console session) to
// install/plug the driver; without a reachable helper an elevated GUI does it
// itself. Release() unplugs only what this object plugged.
class VirtualDisplayProvisioner {
 public:
  VirtualDisplayProvisioner() = default;
  ~VirtualDisplayProvisioner() { Release(); }
  VirtualDisplayProvisioner(const VirtualDisplayProvisioner&) = delete;
  VirtualDisplayProvisioner& operator=(const VirtualDisplayProvisioner&) =
      delete;

  bool Acquire(VirtualDisplayMode mode, std::string* error);
  // Owner calls this before launching an Acquire worker. Clearing cancellation
  // inside the worker would lose a Stop() racing with thread startup.
  void PrepareAcquire() { cancel_.store(false, std::memory_order_relaxed); }
  void Release();
  bool active() const { return active_.load(std::memory_order_relaxed); }
  // True when Release() has something to undo. Read on the owner's thread
  // after the provisioning worker has been joined.
  bool NeedsRelease() const {
    return active() || pending_helper_release_ || plugged_locally_ > 0;
  }
  // Makes a blocking Acquire() return early with "cancelled" at its next poll
  // (helper install/plug can take up to two minutes).
  void RequestCancel() { cancel_.store(true, std::memory_order_relaxed); }

 private:
  // |helper_reachable| tells the caller whether a local fallback makes sense.
  bool AcquireViaHelper(VirtualDisplayMode mode, std::string* error,
                        bool* helper_reachable);
  bool AcquireLocally(VirtualDisplayMode mode, std::string* error);
  bool Cancelled() const { return cancel_.load(std::memory_order_relaxed); }

  // Acquire/Release run on the owner's provisioning worker; active() and
  // RequestCancel() are used from other threads.
  std::atomic<bool> active_{false};
  std::atomic<bool> cancel_{false};
  bool via_helper_ = false;
  // A cancelled helper plug still completes in the helper; Release() undoes it.
  bool pending_helper_release_ = false;
  // Monitors plugged by this process (helper-plugged ones are tracked there).
  int plugged_locally_ = 0;
};

}  // namespace crossdesk

#endif  // CROSSDESK_VIRTUAL_DISPLAY_PROVISIONER_H_
