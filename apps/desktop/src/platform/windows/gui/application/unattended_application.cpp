#include "application/gui_application.h"

#include <Windows.h>
#include <SDL3/SDL.h>

#include "rd_log.h"
#include "unattended_config.h"

namespace crossdesk {

int GuiApplication::RunUnattended(void* stop_event) {
  if (!stop_event || !IsLocalSystemProcess() || !IsUnattendedEnabled() ||
      !PrepareUnattendedDirectory(false))
    return 1;
  unattended_host_ = true;
  DWORD session = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) || session == 0)
    return 1;

  path_manager_ = std::make_unique<PathManager>("CrossDesk");
  const auto root = UnattendedDataPath();
  cache_path_ = root.string();
  exec_log_path_ = (root / L"logs").string();
  dll_log_path_ = exec_log_path_;
  InitializeLogger();
  config_center_ = std::make_unique<ConfigCenter>(cache_path_ + "/config.ini");
  if (config_center_->SetPrivacyScreen(false) != 0 ||
      config_center_->SetSaveRemotePreviews(false) != 0)
    return 1;
  InitializeSettings();

  // The transport and device controllers do not need a Slint window, an SDL
  // video driver, a tray icon, or a logged-in user's audio/clipboard session.
  if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
  devices_.Initialize();
  ULONGLONG last_status = 0;
  ULONGLONG next_peer_attempt = 0;
  while (WaitForSingleObject(stop_event, 16) == WAIT_TIMEOUT) {
    HandleSessionCleanup();
    HandlePasswordChangeResult();
    HandleCredentialRecovery();
    const auto now = GetTickCount64();
    if (!peer_ && now >= next_peer_attempt) {
      next_peer_attempt = now + 3000;
      if (CreateConnectionPeer() != 0) CloseConnectionPeer();
    }
    HandleConnectionStatusChange();
    HandleWindowsServiceIntegration();
    devices_.UpdateInteractions();
    ShareLocalCursorState();
    if (now - last_status >= 1000) {
      last_status = now;
      if (!WriteUnattendedStatus(settings_.ActiveIdentity(),
                                 signal_connected_.load(), session)) {
        LOG_WARN("Could not publish unattended host status");
      }
    }
    // Pump native device messages without constructing an application window.
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }

  // Quiesce network callbacks before destroying the controllers they use.
  CloseAllRemoteSessions();
  WaitForSessionCleanup();
  keyboard_.ReleaseAllRemotePressedKeys("unattended_host_exit");
  devices_.DestroyDevices();
  devices_.DestroyFactories();
  privacy_.Shutdown();
  WriteUnattendedStatus(settings_.ActiveIdentity(), false, session);
  SDL_Quit();
  return 0;
}
}  // namespace crossdesk
