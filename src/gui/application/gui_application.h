#ifndef CROSSDESK_GUI_APPLICATION_H_
#define CROSSDESK_GUI_APPLICATION_H_

#include <memory>
#include <string>

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
  void SyncMainWindow();
  void SyncConnectionDialog();
  void SyncPlatformDialogs();
  void SyncStreamWindow();
  void SyncServerWindow();
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
};

} // namespace crossdesk

#endif // CROSSDESK_GUI_APPLICATION_H_
