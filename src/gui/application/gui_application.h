#ifndef _GUI_APPLICATION_H_
#define _GUI_APPLICATION_H_

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

#include "runtime/cursor_state_provider.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {

// Slint application shell. GuiRuntime remains the owner of transport, media,
// device, clipboard, transfer and settings behavior.
class GuiApplication final : private GuiRuntime {
public:
  GuiApplication();
  ~GuiApplication();

  int Run();

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
  bool MinimizeMainWindowToTray();
  void BindMainCallbacks();
  void BindStreamCallbacks();
  void BindServerCallbacks();
  void Tick();
  void ShareLocalCursorState();
  void HandlePasswordChangeResult();
  void HandleCredentialRecovery();
  void SyncMainWindow();
  void SyncConnectionDialog();
  void SyncPlatformDialogs();
  void SyncStreamWindow();
  void SyncStreamVideoFrame();
  void ScheduleNextVideoFrame();
  void ConfigureStreamVideoRenderer();
  void SyncStreamKeyboardFocus();
  void SetStreamKeyboardFocus(bool focused);
  void SyncServerWindow();
#if defined(__linux__) && !defined(__APPLE__)
  void SyncXWaylandWindowActivation();
#endif
  void UpdateLocalization();
  void ResetSettingsUi();
  void SaveSettingsFromUi();
#if _WIN32 && CROSSDESK_PORTABLE
  void CheckPortableWindowsService();
  void StartPortableWindowsServiceInstall();
  void JoinPortableWindowsServiceInstallThread();
#endif
  void ConnectFromUi(const std::string &remote_id);
  void SelectStreamTab(int index);
  void ReorderStreamTab(int from, float drop_x, float tab_width);
  void CloseStreamTab(const std::string &remote_id);
  void SendPointerInput(int button, int kind, float x, float y);
  void SendScrollInput(float delta_x, float delta_y, float x, float y);
  void SendKeyInput(const std::string &text, bool pressed, bool control,
                    bool alt, bool shift, bool meta);
  void Cleanup();

  std::shared_ptr<RemoteSession> SelectedSession();
  std::string OpenFileDialog(const std::string &title);
  bool OpenUrl(const std::string &url);

  std::unique_ptr<SlintUi> ui_;
  CursorStateProvider cursor_state_provider_;
  std::unordered_map<std::string, CursorDeliveryState>
      cursor_delivery_states_;
  uint32_t cursor_state_sequence_ = 0;
  std::chrono::steady_clock::time_point next_video_frame_time_{};
#if defined(__linux__) && !defined(__APPLE__)
  bool use_xwayland_gui_ = false;
  bool use_x11_custom_titlebar_ = false;
#endif
};

} // namespace crossdesk

#endif
