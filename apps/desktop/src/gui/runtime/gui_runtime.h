/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _GUI_RUNTIME_H_
#define _GUI_RUNTIME_H_

#include <atomic>
#include <deque>
#include <memory>
#include <string>

#include "features/clipboard/clipboard_controller.h"
#include "features/devices/session_device_manager.h"
#include "features/file_transfer/file_transfer_manager.h"
#include "features/input/keyboard_controller.h"
#include "features/settings/settings_manager.h"
#include "privacy_controller.h"
#include "runtime/gui_state.h"
#include "runtime/peer_event_handler.h"
#include "runtime/privacy_session_policy.h"

namespace crossdesk {

class VideoRenderer;

// Shared GUI runtime. It owns subsystem controllers and cross-cutting session
// state, but no window lifecycle, ImGui view, or transport callback methods.
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

  static void SdlCaptureAudioIn(void* userdata, Uint8* stream, int len);
  static void SdlCaptureAudioOut(void* userdata, Uint8* stream, int len);

  int CreateConnectionPeer();
  int ConnectTo(const std::string& remote_id, const char* password,
                bool remember_password, bool bypass_presence_check = false);
  int RequestSingleDevicePresence(const std::string& remote_id,
                                  const char* password, bool remember_password);

  void UpdateLabels();
  void HandleRecentConnections();
  void HandleConnectionStatusChange();
  void HandlePendingPresenceProbe();
  void HandlePresenceProbeTimeout();
  bool HasActiveSession();
  void HandleServerControllerDisconnected(const std::string& remote_id,
                                          const char* reason);
  void HandleWindowsServiceIntegration();
  void HandlePrivacy();
  bool IsAuthorizedController(const std::string& remote_id);
  void SetControllerPrivacySupport(const std::string& remote_id, bool supported);
  void QueuePrivacyCommand(const std::string& remote_id, const PrivacyCommand& command);

  void CloseRemoteSession(std::shared_ptr<RemoteSession> props);
  void CloseAllRemoteSessions();
  void CloseConnectionPeer();
  void CloseServerController(const std::string& remote_id);
  void ResetRemoteSessionResources(std::shared_ptr<RemoteSession> props);
  void HandleSessionCleanup();
  void WaitForSessionCleanup();
  std::shared_ptr<RemoteSession> FindRemoteSession(
      const std::string& remote_id);

  void ResetRemoteServiceStatus(RemoteSession& props);
  void ApplyRemoteServiceStatus(RemoteSession& props,
                                const ServiceStatus& status);
  RemoteUnlockState GetRemoteUnlockState(const RemoteSession& props) const;
#if _WIN32
  void ResetLocalWindowsServiceState(bool clear_pending_sas);
#endif

#ifdef __APPLE__
  bool CheckScreenRecordingPermission();
  bool CheckAccessibilityPermission();
  void OpenScreenRecordingPreferences();
  void OpenAccessibilityPreferences();
  void RefreshMacPermissionStatus(bool force);
  bool EnsureMacScreenRecordingPermission();
  bool EnsureMacAccessibilityPermission();
#endif

  PrivacyController privacy_;
  PrivacySessionPolicy privacy_sessions_;
  std::mutex privacy_commands_mutex_;
  std::deque<std::pair<std::string, PrivacyCommand>> privacy_commands_;
  uint32_t last_privacy_revision_ = 0;
  uint64_t last_privacy_status_tick_ = 0;
  ClipboardController clipboard_;
  SessionDeviceManager devices_;
  FileTransferManager transfers_;
  SettingsManager settings_;
  KeyboardController keyboard_;
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
