/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _GUI_APPLICATION_H_
#define _GUI_APPLICATION_H_

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <remote_action.h>

#include "application/signal_connection_indicator.h"
#include "runtime/cursor_state_provider.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {

class UpdateChecker;
namespace updater { class Controller; }

// Slint application shell. GuiRuntime remains the owner of transport, media,
// device, clipboard, transfer and settings behavior.
class GuiApplication final : private GuiRuntime {
public:
  GuiApplication();
  ~GuiApplication();

  int Run();
#ifdef _WIN32
  int RunUnattended(void* stop_event);
#endif

private:
  struct SlintUi;
  struct CursorDeliveryState {
    CursorState last_sent{};
    bool has_sent = false;
    bool feedback_pending = false;
    std::chrono::steady_clock::time_point last_sent_time{};
  };

  void InitializeLogger();
  void InitializeSettings();
  bool InitializeSDL();
  void InitializeModules();
  void InitializeUi();
  void InitializeSystemTray();
  void ShowMainWindow(const std::string& activation_token = {});
  bool MinimizeMainWindowToTray();
  void BindMainCallbacks(bool gallery_window = false);
  void BindTrustedDeviceCallbacks(bool gallery_window = false);
  void BindStreamCallbacks();
  void BindServerCallbacks();
  void Tick();
#ifdef _WIN32
  void TickLocalHost();
#endif
  void PollUpdateCheck();
  void ShareLocalCursorState();
  bool RequestPasswordChange(const std::string &password);
  void HandlePasswordChangeResult();
  void HandleCredentialRecovery();
  void SyncMainWindow();
  void ShowGalleryWindow();
  void CloseGalleryWindow();
  void SyncWindowGeometry(bool gallery_window);
  void ToggleGalleryFullscreen();
  void RestoreWindowGeometry(bool before_show = false,
                             bool gallery_window = false);
  void SyncGallery();
  void GalleryAction(const std::string& action, const std::string& value,
                     bool gallery_window = true);
  bool AddGalleryDevice(const std::string& id, const std::string& password,
                        const std::string& group, bool remember_password);
  bool ImportGalleryRecent(const std::string& group);
  void DeviceGroupAction(const std::string& action, const std::string& value,
                         bool gallery_window);
  bool SaveDeviceGroups(const std::string& new_group, bool gallery_window);
  void ReleasePressedInput();
  void ReleaseControl();
  void SyncAudioPlayback();
  void SaveGalleryWindowState();
  void SyncAnnouncements();
  void SyncConnectionDialog();
  void SyncPlatformDialogs();
  void SyncStreamWindow();
  void SyncStreamVideoFrame();
  void ScheduleNextVideoFrame();
  void ConfigureStreamVideoRenderer();
  void SyncStreamKeyboardFocus();
  void SetStreamKeyboardFocus(bool focused);
  void SyncServerWindow();
  void SyncTrustedDevices();
  void SyncFileTransferNotifications();
  void OpenFileManager(const std::string& remote);
  std::string FileManagerPeerName(const std::string& remote);
  void CancelFileTransfer(uint32_t file_id);
  void SetServerPanelExpanded(bool expanded);
  void PositionServerWindows();
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
  void SyncXWaylandWindowActivation();
#endif
  void UpdateLocalization();
  void ResetSettingsUi(bool gallery_window = false);
  bool ValidateSelfHostedSettingsFromUi(bool gallery_window = false);
  bool SaveSettingsFromUi(bool gallery_window = false);
  void ApplySettingsFromConfig(bool reconnect);
#if _WIN32 && CROSSDESK_PORTABLE
  void CheckPortableWindowsService();
  void StartPortableWindowsServiceInstall();
  void JoinPortableWindowsServiceInstallThread();
#endif
  void ConnectFromUi(const std::string &remote_id);
  void SelectStreamTab(int index);
  void ReorderStreamTab(int from, float drop_x, float tab_width);
  void CloseStreamTab(const std::string &remote_id);
  void CloseRemoteClosedTabs();
  void SendPointerInput(int button, int kind, float x, float y);
  void SendScrollInput(float delta_x, float delta_y, float x, float y);
  void SendKeyInput(const std::string &text, bool pressed, bool control,
                    bool alt, bool shift, bool meta);
  void Cleanup();

  std::shared_ptr<RemoteSession> SelectedSession();
  void OpenFileDialog(const std::string& title,
                      std::function<void(std::string)> completion);
  bool OpenUrl(const std::string &url);

  std::unique_ptr<SlintUi> ui_;
  std::unique_ptr<UpdateChecker> update_checker_;
#if CROSSDESK_HAS_UPDATER
  std::unique_ptr<updater::Controller> updater_;
#endif
  CursorStateProvider cursor_state_provider_;
  std::unordered_map<std::string, CursorDeliveryState>
      cursor_delivery_states_;
  uint32_t cursor_state_sequence_ = 0;
  std::chrono::steady_clock::time_point next_video_frame_time_{};
  SignalConnectionIndicator signal_connection_indicator_;
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
  bool use_xwayland_gui_ = false;
  bool use_x11_custom_titlebar_ = false;
#endif
};

} // namespace crossdesk

#endif
