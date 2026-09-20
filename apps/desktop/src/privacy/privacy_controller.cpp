/* Copyright (c) 2026 by DI JUNKUN, All Rights Reserved. */
#include "privacy_controller.h"
#include <chrono>
#include <cstring>
#include "rd_log.h"

namespace crossdesk {
uint64_t PrivacyController::Now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
PrivacyController::PrivacyController(Factory factory) {
  status_.state = PrivacyState::unsupported;
  std::strcpy(status_.reason, "Privacy screen is not available on this platform");
#ifdef _WIN32
  if (!factory) factory = CreateWindowsPrivacyBackend;
#elif defined(__APPLE__)
  if (!factory) factory = CreateMacPrivacyBackend;
#elif defined(__linux__)
  if (!factory) factory = CreateLinuxPrivacyBackend;
#endif
  thread_ = std::thread([this, factory] { Run(factory); });
}
PrivacyController::~PrivacyController() { Shutdown(); }
void PrivacyController::WakeLocked() {
  if (backend_) backend_->Wake();
  wake_.notify_all();
}
void PrivacyController::Shutdown() {
  {
    std::lock_guard lock(mutex_);
    quit_ = true;
    WakeLocked();
  }
  if (thread_.joinable()) thread_.join();
#ifdef __APPLE__
  FlushMacPrivacyTasks();
#endif
}
PrivacyStatus PrivacyController::Snapshot() const {
  std::lock_guard lock(mutex_);
  return status_;
}
void PrivacyController::SetText(const PrivacyScreenText& text) {
  std::lock_guard lock(mutex_);
  text_ = text;
}
void PrivacyController::SetState(PrivacyState state, const std::string& reason) {
  status_.state = state;
  status_.remote_paused = false; // Wire compatibility; privacy never pauses a session.
  ++status_.revision;
  std::strncpy(status_.reason, reason.c_str(), sizeof(status_.reason) - 1);
  status_.reason[sizeof(status_.reason) - 1] = '\0';
  LOG_INFO("Privacy: state={}, overlay={}, input_blocked={}, reason={}",
           static_cast<int>(state), status_.overlay_active, status_.input_blocked, reason);
}
void PrivacyController::Enable(bool block_input) {
  std::lock_guard lock(mutex_);
  if (quit_ || engaged_.load() || disable_pending_) return;
  if (!status_.supported || (block_input && !status_.input_block_supported)) {
    SetState(PrivacyState::unsupported, "Privacy screen is unavailable");
    return;
  }
  engaged_.store(true);
  block_requested_ = block_input;
  automatic_enable_ = false;
  enable_pending_ = true;
  failure_.clear();
  started_ = Now();
  SetState(PrivacyState::starting, "Enabling privacy screen");
  WakeLocked();
}
void PrivacyController::EnableOnConnection() {
  std::lock_guard lock(mutex_);
  if (quit_ || (engaged_.load() && !disable_pending_)) return;
  // A reconnect may arrive before asynchronous AppKit recovery has completed.
  engaged_.store(true);
  block_requested_ = true;
  automatic_enable_ = true;
  enable_pending_ = true;
  failure_.clear();
  started_ = Now();
  SetState(PrivacyState::starting, "Enabling privacy on connection");
  WakeLocked();
}
void PrivacyController::Disable() {
  std::lock_guard lock(mutex_);
  // Nothing to restore when privacy is already off and no cleanup is retrying.
  if (quit_ || (!engaged_.load() && !disable_pending_)) return;
  enable_pending_ = false;
  failure_.clear();
  disable_pending_ = true;
  SetState(PrivacyState::stopping, "Restoring local desktop and input");
  WakeLocked();
}
void PrivacyController::Disconnected() { Disable(); }
void PrivacyController::FailLocked(const std::string& reason) {
  if (!engaged_.load() || disable_pending_) return;
  failure_ = reason;
  enable_pending_ = false;
  disable_pending_ = true;
  SetState(PrivacyState::failed, reason);
  WakeLocked();
}
void PrivacyController::Fail(const std::string& reason) {
  std::lock_guard lock(mutex_);
  FailLocked(reason);
}
void PrivacyController::CaptureChanged(bool running) {
  std::lock_guard lock(mutex_);
  // Windows announces a stopped capture while preparing the first session.
  // Keep automatic privacy pending until capture starts, within its deadline.
  // Once capture was running or backend enable began, a stop remains a fault.
  const bool waiting_for_initial_capture =
      automatic_enable_ && enable_pending_ && !capture_running_;
  capture_running_ = running;
  if (!running && !waiting_for_initial_capture)
    FailLocked("Capture changed; privacy screen is being turned off");
  WakeLocked();
}
void PrivacyController::Run(Factory factory) {
  // Only this thread touches backend_; Wake() is safe to call concurrently.
  // Native enable/teardown can block for seconds, so those calls run with the
  // mutex released to keep Snapshot()/Enable()/Disable() responsive.
  std::unique_lock lock(mutex_);
  try {
    if (factory) backend_ = factory();
    while (!quit_) {
      if (last_capability_check_ == 0 ||
          (!engaged_.load() && Now() - last_capability_check_ >= 2000)) {
        last_capability_check_ = Now();
        const auto caps = backend_ ? backend_->Query() : PrivacyCapabilities{};
        const bool changed = status_.supported != caps.overlay ||
                             status_.input_block_supported != caps.block_input;
        status_.supported = caps.overlay;
        status_.input_block_supported = caps.block_input;
        if (!engaged_.load() && (changed || status_.revision == 0))
          SetState(caps.overlay ? PrivacyState::off : PrivacyState::unsupported, caps.reason);
      }
      if (disable_pending_) {
        if (backend_) {
          lock.unlock();
          backend_->Recover();
          lock.lock();
        }
        if (backend_ && backend_->RecoveryPending()) {
          lock.unlock();
          backend_->WaitForEvents(25);
          lock.lock();
          continue;
        }
        disable_pending_ = false;
        if (backend_ && !backend_->IsRecovered()) {
          enable_pending_ = false;
          disable_pending_ = true; // Retry native cleanup without affecting the session.
          // Report the stuck cleanup once; every retry would otherwise bump
          // the revision and rebroadcast the status to all controllers.
          if (status_.state != PrivacyState::failed)
            SetState(PrivacyState::failed, "Could not release every privacy resource");
        } else {
          status_.overlay_active = status_.input_blocked = false;
          if (!enable_pending_) {
            engaged_.store(false);
            SetState(failure_.empty() ? PrivacyState::off : PrivacyState::failed,
                     failure_.empty() ? "Privacy screen is off" : failure_);
          }
        }
      }
      if (enable_pending_) {
        const auto caps = backend_ ? backend_->Query() : PrivacyCapabilities{};
        status_.supported = caps.overlay;
        status_.input_block_supported = caps.block_input;
        if (!caps.overlay || (block_requested_ && !caps.block_input)) {
          FailLocked("Privacy screen unavailable: " + caps.reason);
        } else if (!capture_running_) {
          if (!automatic_enable_ || Now() - started_ > 10000)
            FailLocked("No desktop capture available for privacy screen");
        } else {
          enable_pending_ = false;
          const bool block_input = block_requested_;
          const PrivacyScreenText text = text_;
          std::string error;
          lock.unlock();
          const bool enabled = backend_->Enable(block_input, text, error);
          lock.lock();
          if (!enabled) {
            // No-op if a disable arrived meanwhile; that path recovers anyway.
            FailLocked(error);
          } else {
            status_.overlay_active = true;
            status_.input_blocked = block_input;
            started_ = Now();
          }
        }
      }
      const auto health = backend_ ? backend_->Poll() : PrivacyHealth{};
      if (engaged_.load() && !disable_pending_) {
        if (health.emergency_exit) {
          failure_.clear();
          enable_pending_ = false;
          disable_pending_ = true;
          SetState(PrivacyState::stopping, "Local shortcut: restoring desktop and input");
        } else if (!health.failure.empty()) {
          FailLocked(health.failure);
        } else if (!enable_pending_ && status_.state == PrivacyState::starting) {
          if (health.ready)
            SetState(PrivacyState::on, "Privacy screen is on");
          else if (Now() - started_ > 3000)
            FailLocked("Privacy screen did not become ready");
        }
      }
      if (backend_) {
        const unsigned timeout = engaged_.load() || disable_pending_ ? 25 : 2000;
        lock.unlock();
        backend_->WaitForEvents(timeout);
        lock.lock();
      } else {
        wake_.wait(lock, [this] { return quit_ || enable_pending_ || disable_pending_; });
      }
    }
  } catch (const std::exception& e) {
    if (!lock.owns_lock()) lock.lock();
    // The worker is gone: nothing can resolve a later Enable(), so report the
    // fault and stop accepting requests instead of parking them in "starting".
    status_.supported = status_.input_block_supported = false;
    status_.overlay_active = status_.input_blocked = false;
    engaged_.store(false);
    quit_ = true;
    SetState(PrivacyState::failed, std::string("Privacy component failed: ") + e.what());
  }
  if (backend_) {
    lock.unlock();
    backend_->Recover();
    lock.lock();
    backend_.reset();
  }
}
}  // namespace crossdesk
