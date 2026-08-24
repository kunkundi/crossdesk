#ifndef CROSSDESK_GUI_RUNTIME_H_
#define CROSSDESK_GUI_RUNTIME_H_

#include <atomic>
#include <memory>
#include <string>

#include "features/clipboard/clipboard_controller.h"
#include "features/devices/session_device_manager.h"
#include "features/file_transfer/file_transfer_manager.h"
#include "features/input/keyboard_controller.h"
#include "features/settings/settings_manager.h"
#include "runtime/gui_state.h"
#include "runtime/peer_event_handler.h"

namespace crossdesk {

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
  void HandleServerControllerDisconnected(const std::string& remote_id,
                                          const char* reason);
  void HandleWindowsServiceIntegration();

  void CloseRemoteSession(std::shared_ptr<RemoteSession> props);
  void CloseAllRemoteSessions();
  void ResetRemoteSessionResources(std::shared_ptr<RemoteSession> props);
  void WaitForThumbnailSaveTasks();
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

  ClipboardController clipboard_;
  SessionDeviceManager devices_;
  FileTransferManager transfers_;
  SettingsManager settings_;
  KeyboardController keyboard_;
  PeerEventHandler peer_events_;
  std::atomic<bool> video_frame_dirty_{false};

 private:
  friend class ClipboardController;
  friend class SessionDeviceManager;
  friend class FileTransferManager;
  friend class SettingsManager;
  friend class KeyboardController;
  friend class PeerEventHandler;
};

}  // namespace crossdesk

#endif  // CROSSDESK_GUI_RUNTIME_H_
