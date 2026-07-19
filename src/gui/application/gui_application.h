#ifndef CROSSDESK_GUI_APPLICATION_H_
#define CROSSDESK_GUI_APPLICATION_H_

#include <filesystem>
#include <string>
#include <vector>

#include "IconsFontAwesome6.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_internal.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {

// SDL/ImGui application shell. Feature state and protocol behavior live in
// GuiRuntime and its composed controllers.
class GuiApplication final : private GuiRuntime {
public:
  GuiApplication();
  ~GuiApplication();

  int Run();

private:
  // Native window integration.
  static SDL_HitTestResult HitTestCallback(SDL_Window *window,
                                           const SDL_Point *area, void *data);

  // Application lifecycle.
  void InitializeLogger();
  void InitializeSettings();
  void InitializeSDL();
  void InitializeModules();
  void InitializeMainWindow();
  void MainLoop();
  void HandleStreamWindow();
  void HandleServerWindow();
  void Cleanup();

  // Window lifecycle and rendering.
  int CreateMainWindow();
  int DestroyMainWindow();
  int CreateStreamWindow();
  int DestroyStreamWindow();
  int CreateServerWindow();
  int DestroyServerWindow();
  int SetupFontAndStyle(ImFont **system_chinese_font_out);
  int DestroyMainWindowContext();
  int DestroyStreamWindowContext();
  int DestroyServerWindowContext();
  int DrawMainWindow();
  int DrawStreamWindow();
  int DrawServerWindow();

  // Views and dialogs.
  int TitleBar(bool main_window);
  int MainWindow();
  int UpdateNotificationWindow();
  int StreamWindow();
  int ServerWindow();
  int RemoteClientInfoWindow();
  int LocalWindow();
  int RemoteWindow();
  int RecentConnectionsWindow();
  int SettingWindow();
  int SelfHostedServerWindow();
  int ControlWindow(std::shared_ptr<RemoteSession> &props);
  int ControlBar(std::shared_ptr<RemoteSession> &props);
  int AboutWindow();
  int StatusBar();
  bool
  ConnectionStatusWindow(std::shared_ptr<RemoteSession> &props);
  int ShowRecentConnections();
  int ConfirmDeleteConnection();
  int EditRecentConnectionAliasWindow();
  int OfflineWarningWindow();
  int NetTrafficStats(std::shared_ptr<RemoteSession> &props);
  int FileTransferWindow(std::shared_ptr<RemoteSession> &props);
  void
  DrawConnectionStatusText(std::shared_ptr<RemoteSession> &props);
  void
  DrawReceivingScreenText(std::shared_ptr<RemoteSession> &props);
  bool OpenUrl(const std::string &url);
  void Hyperlink(const std::string &label, const std::string &url,
                 float window_width);
  std::string OpenFileDialog(std::string title);
  bool MinimizeMainWindowToTray();

  // SDL event dispatch and input translation.
  void UpdateRenderRect();
  void ProcessSdlEvent(const SDL_Event &event);
  int ProcessKeyboardEvent(const SDL_Event &event);
  int ProcessMouseEvent(const SDL_Event &event);
  void CloseTab(decltype(remote_sessions_)::iterator &it);

#if _WIN32 && CROSSDESK_PORTABLE
  void CheckPortableWindowsService();
  int PortableServiceInstallWindow();
  void StartPortableWindowsServiceInstall();
  void JoinPortableWindowsServiceInstallThread();
#endif

#ifdef __APPLE__
  int RequestPermissionWindow();
  bool DrawToggleSwitch(const char *id, bool active, bool enabled);
#endif
};

} // namespace crossdesk

#endif // CROSSDESK_GUI_APPLICATION_H_
