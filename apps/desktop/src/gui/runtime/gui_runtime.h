/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _GUI_RUNTIME_H_
#define _GUI_RUNTIME_H_

#include <atomic>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <string>

#include "desktop_automation.h"
#include "features/clipboard/clipboard_controller.h"
#include "features/devices/session_device_manager.h"
#include "features/file_transfer/file_transfer_manager.h"
#include "features/input/keyboard_controller.h"
#include "features/input/remote_input_dispatcher.h"
#include "features/settings/settings_manager.h"
#include "features/settings/remote_resolution_store.h"
#include "network/lan/lan_session_manager.h"
#include "privacy_controller.h"
#include "runtime/gui_state.h"
#include "runtime/disconnect_lock_policy.h"
#include "runtime/display_resolution_restoration.h"
#include "runtime/announcement_inbox.h"
#include "runtime/peer_event_handler.h"
#include "runtime/peer_transport.h"
#include "runtime/privacy_session_policy.h"
#include "runtime/trusted_devices.h"
#ifdef __APPLE__
#include "platform/macos/gui/runtime/mac_permission_migration.h"
#endif
#ifdef _WIN32
#include "local_host_channel.h"
#include "runtime/local_host_status.h"
#endif

namespace crossdesk {

class VideoRenderer;

// Shared GUI runtime. It owns subsystem controllers and cross-cutting session
// state, but no window lifecycle, UI view, or transport callback methods.
class GuiRuntime : protected gui_detail::GuiState {
 protected:
  using FileTransferState = gui_detail::FileTransferState;
  using RemoteSession = gui_detail::RemoteSession;

  enum class RemoteUnlockState {
    none,
    service_unavailable,
    lock_screen,
    credential_ui,
    secure_desktop,
  };

  GuiRuntime();
  ~GuiRuntime();

  static void SdlCaptureAudioIn(void* userdata, uint8_t* stream, int len);
  static void SdlCaptureAudioOut(void* userdata, uint8_t* stream, int len);

  void InitializeLan();
  void ApplyLanSettings();
  bool LanAvailable() const;
  void TickLan();
  void BeginShutdown();
  bool ShutdownDrained();
  void RegisterStreams(MiniRtcPeerPtr* peer, size_t display_count);
  void ConnectLan(const std::string& address, unsigned port,
                  std::string password, bool remember_password = false);
  int WithController(
      const std::string& id,
      const std::function<int(MiniRtcPeerPtr*, const std::string&)>& operation,
      bool require_trusted = true);
  int SendControllerData(const std::string& id, const char* data, size_t size,
                         const char* label);
  std::shared_ptr<PeerEventHandler> ControllerEvents(const std::string& id);
  std::vector<std::string> ConnectedControllers();
  std::unique_ptr<lan::SessionManager> lan_;
  std::string lan_error_;
  std::shared_ptr<lan::Session> lan_connecting_;
  bool lan_stopping_ = false;
  bool stopping_ = false;
  std::chrono::steady_clock::time_point shutdown_report_at_{};
  struct LanController {
    std::shared_ptr<lan::Session> session;
    std::shared_ptr<PeerEventHandler> events;
  };
  // Protected by remote_sessions_mutex_; snapshots acquire callback leases
  // before releasing that lock, so removal cannot race a raw Peer use.
  std::map<std::string, LanController> lan_controllers_;
  int CreateConnectionPeer();
  int ConnectTo(const std::string& remote_id, const char* password,
                bool remember_password, bool bypass_presence_check = false,
                bool background = false);
  // UI thread: mute this receiver without stopping audio for other controllers.
  void MuteRemoteAudio(const std::shared_ptr<RemoteSession>& props);
  void UnmuteRemoteAudio(const std::shared_ptr<RemoteSession>& props);
  int RequestSingleDevicePresence(const std::string& remote_id,
                                  const char* password, bool remember_password);
  int ForceSignalReconnect();

  void HandleRecentConnections();
  void HandleConnectionStatusChange();
  void HandlePendingRejoins();
  void CancelPendingConnection(const std::string& remote_id);
  void HandlePendingPresenceProbe();
  void HandlePresenceProbeTimeout();
  bool HasActiveSession();
  void HandleServerControllerDisconnected(const std::string& remote_id,
                                          const char* reason);
  void HandleWindowsServiceIntegration();
  void ProcessAutomationRequests();
  void HandlePrivacy();
  void HandleScreenLock();
  bool IsAuthorizedController(const std::string& remote_id);
  void InitializeTrustedDevices(const std::filesystem::path& directory);
  bool PublishSharedIdentity(bool include_config = false);
  bool PrepareSharedGuiIdentity(bool import_existing);
  bool TrustedControllerAllowed(const std::string& remote_id);
  bool BeginControllerTrust(const std::string& remote_id, PeerEventHandler* events);
  void ForgetControllerTrust(const std::string& remote_id, PeerEventHandler* events);
  bool HandleDeviceTrustMessage(const std::string& remote_id, PeerEventHandler* events,
                               const char* data, size_t size);
  void TickControllerTrust();
  std::optional<TrustedDevices::Device> VerifiedController(const std::string& remote_id);
  bool TrustController(const std::string& remote_id, const std::string& expected_key);
  bool AddTrustedDevice(const TrustedDevices::Device& device);
  bool RemoveTrustedDevice(const std::string& public_key);
  int SendTrustMessage(const std::string& remote_id, const nlohmann::json& message);
  std::unique_ptr<TrustedDevices> trusted_devices_;
  struct ControllerTrust {
    PeerEventHandler* events = nullptr;
    std::string nonce;
    std::optional<TrustedDevices::Device> verified;
    std::string early_host_info;
    std::chrono::steady_clock::time_point deadline{}, next_challenge{};
    bool pending = false, invalid = false, closing = false, capable = false;
    bool IsTrusted(const TrustedDevices* devices) const {
      return devices && !invalid && !closing && verified &&
             devices->Contains(verified->public_key);
    }
  };
  std::mutex controller_trust_mutex_;
  std::map<std::string, ControllerTrust> controller_trust_;
  void SetControllerPrivacySupport(const std::string& remote_id, bool supported);
  // Caller holds connection_status_mutex_ exclusively. Turns privacy off when
  // any connected controller lacks support, or on for the first automatic one.
  void ApplyPrivacyAdmission(bool automatic_enable);
  void QueuePrivacyCommand(const std::string& remote_id, const PrivacyCommand& command);
  std::vector<DisplayResolutionInfo> QueryHostDisplayResolutions();
  void QueueDisplayResolutionChange(const std::string& id,
                                    PeerEventHandler* source,
                                    const DisplayResolutionChange& change);
  void HandleDisplayResolutionChanges();
  void RestoreHostDisplayResolutions(bool force = false);
  bool ApplyHostDisplayResolution(const DisplayResolutionChange& change);
  void BeginDisplayResolutionStartup(const std::string& id, PeerEventHandler* source);
  void ReceiveDisplayResolutionStartup(const std::string& id, PeerEventHandler* source,
      std::vector<DisplayResolutionPreference> preferences);
  bool DisplayResolutionsReadyForCapture(const std::string& id = {});

  void CloseRemoteSession(std::shared_ptr<RemoteSession> props);
  void CloseAllRemoteSessions();
  void CloseConnectionPeer();
  void CloseServerController(const std::string& remote_id);
  void ResetRemoteSessionResources(std::shared_ptr<RemoteSession> props);
  void HandleSessionCleanup();
  void WaitForSessionCleanup();
  std::shared_ptr<RemoteSession> FindRemoteSession(
      const std::string& remote_id);
  // UI thread: send mapped mouse/keyboard input to the current control target,
  // or every connected session while that target leads DeskHub group control.
  void SendRemoteInput(const RemoteAction& action);
  InputOutcome ApplyRemoteInput(const std::string& remote_id, const RemoteAction& action);
  void HandleAutomation(const std::string& remote_id, const nlohmann::json& request);
  DesktopAutomation desktop_automation_;
  std::mutex automation_mutex_;
  std::deque<std::function<void()>> automation_tasks_;
  bool automation_closing_ = false;
  InputOutcome InjectRemoteMouse(const RemoteAction& action, int display);
  std::string gallery_group_control_id_;

  void ResetRemoteServiceStatus(RemoteSession& props);
  void ApplyRemoteServiceStatus(RemoteSession& props,
                                const ServiceStatus& status);
  RemoteUnlockState GetRemoteUnlockState(const RemoteSession& props) const;
#if _WIN32
  void ResetLocalWindowsServiceState(bool clear_pending_sas);
  std::string WindowsInputStage() const {
    std::lock_guard lock(windows_input_stage_mutex_);
    return windows_input_stage_;
  }
  void PublishWindowsInputStage() {
    std::lock_guard lock(windows_input_stage_mutex_);
    windows_input_stage_ = local_service_status_received_
                               ? local_interactive_stage_ : std::string{};
  }
#endif

#ifdef __APPLE__
  void StartMacPermissionMigration(bool retry);
  std::future<MacPermissionMigrationResult> mac_permission_migration_task_;
  bool mac_permission_migration_failed_ = false;
  bool mac_permission_dialog_dismissed_ = false;
  bool mac_permission_dialog_requested_ = false;
  bool CheckScreenRecordingPermission();
  bool CheckAccessibilityPermission();
  void OpenScreenRecordingPreferences();
  void OpenAccessibilityPreferences();
  void RefreshMacPermissionStatus(bool force);
  bool EnsureMacScreenRecordingPermission();
  bool EnsureMacAccessibilityPermission();
#endif

  PrivacyController privacy_;
  // Set before initializing callbacks; immutable for the process lifetime.
  bool unattended_host_ = false;
  bool managed_host_frontend_ = false;
  std::string BuildHostInformation(bool advertise_trust = false,
      const std::vector<DisplayResolutionPreference>& preferences = {});
  void AnnounceHostCapabilities(const std::string& id);
#ifdef _WIN32
  std::shared_ptr<LocalHostChannel> local_host_;
  std::atomic<bool> local_gui_ready_{false};
  std::mutex local_host_state_mutex_;
  std::map<std::string, nlohmann::json> local_controller_info_;
  std::map<std::string, uint64_t> mirrored_controllers_;
  uint64_t next_controller_epoch_ = 0;
  uint64_t next_local_host_status_ = 0;
  LocalHostStatus local_host_status_;
  std::atomic<bool> local_host_profile_dirty_{true};
  nlohmann::json pending_local_config_;
  uint64_t next_local_config_request_ = 0;
  bool local_config_sent_ = false;
  std::optional<bool> local_cursor_hidden_sent_;
  std::optional<bool> local_cursor_request_sent_;
  bool local_cursor_policy_ready_ = false;
  bool host_cursor_default_ = true, host_cursor_preference_ = true;
  std::atomic<uint64_t> host_capture_tick_{0};
  bool ForwardUserSessionData(const std::string& id, const std::string& stream,
                              const char* data, size_t size);
  void PublishLocalHostStatus();
  bool SendLocalHostCommand(nlohmann::json message);
#endif
  bool UserSessionFeaturesAvailable() const {
#ifdef _WIN32
    return !unattended_host_ || local_gui_ready_.load();
#else
    return !unattended_host_;
#endif
  }
  AnnouncementInbox announcements_;
  PrivacySessionPolicy privacy_sessions_;
  DisconnectLockPolicy disconnect_lock_;
  std::mutex privacy_commands_mutex_;
  std::deque<std::pair<std::string, PrivacyCommand>> privacy_commands_;
  struct PendingDisplayResolution {
    std::string remote_id;
    std::weak_ptr<PeerEventHandler> source;
    DisplayResolutionChange change;
    uint64_t epoch;
  };
  std::mutex display_resolution_commands_mutex_;
  std::deque<PendingDisplayResolution> display_resolution_commands_;
  struct InitialDisplayResolutions {
    std::weak_ptr<PeerEventHandler> source;
    DisplayResolutionStartup state;
    uint64_t generation = 0;
  };
  std::map<std::string, InitialDisplayResolutions> initial_display_resolutions_;
  uint64_t next_display_resolution_startup_ = 0;
  std::vector<DisplayResolutionInfo> last_display_resolutions_;
  uint64_t next_display_resolution_poll_ = 0;
  // Incremented when the last incoming controller disconnects. Keep the
  // boundary even if a new connection arrives before the application tick.
  std::atomic<uint64_t> display_resolution_epoch_{0};
  uint64_t handled_display_resolution_epoch_ = 0;
  uint64_t next_display_resolution_restore_ = 0;
  DisplayResolutionRestoration display_resolution_restoration_;
  bool privacy_settings_changed_ = false;
  uint32_t last_privacy_revision_ = 0;
  uint64_t last_privacy_status_tick_ = 0;
  ClipboardController clipboard_;
  SessionDeviceManager devices_;
  FileTransferManager transfers_;
  SettingsManager settings_;
  RemoteResolutionStore remote_resolutions_;
  KeyboardController keyboard_;
  RemoteInputDispatcher remote_input_;
  std::shared_ptr<PeerEventHandler> peer_events_;
  std::atomic<bool> video_frame_dirty_{false};
  std::unique_ptr<VideoRenderer> video_renderer_;

 private:
  friend class ClipboardController;
  friend class SessionDeviceManager;
  friend class FileTransferManager;
  friend class SettingsManager;
  friend class KeyboardController;
  friend class PeerEventHandler;
};

}  // namespace crossdesk

#endif
