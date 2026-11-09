#include "application/gui_application.h"

#include <Windows.h>
#include <SDL3/SDL.h>

#include "rd_log.h"
#include "unattended_config.h"
#include "shared_identity.h"
#include "platform/windows/winrt_apartment.h"

namespace crossdesk {

int GuiApplication::RunUnattended(void* stop_event) {
  if (!stop_event || !IsLocalSystemProcess() ||
      (!IsUnattendedEnabled() && !SharedHostIdentity::GuiPresent()) ||
      !PrepareUnattendedDirectory(false))
    return 1;
  unattended_host_ = true;
  DWORD session = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) || session == 0)
    return 1;

  path_manager_ = std::make_unique<PathManager>("CrossDesk");
  const auto root = UnattendedDataPath();
  std::string identity_error;
  shared_identity_ = SharedHostIdentity::AcquireHost(identity_error);
  if (!shared_identity_) {
    LOG_ERROR("Cannot acquire the unattended host identity: {}", identity_error);
    return 1;
  }
  cache_path_ = shared_identity_->CachePath().string();
  exec_log_path_ = (root / L"logs").string();
  dll_log_path_ = exec_log_path_;
  InitializeLogger();
  LOG_INFO("Unattended host initialized in session {}", session);
  // Reconnect starts WGC on this main thread, whereas the first unlock can
  // start it on the capture recovery thread. Both need their own WinRT scope.
  ScopedWinRtApartment apartment;
  if (!apartment.ready()) return 1;
  config_center_ = std::make_unique<ConfigCenter>(cache_path_ + "/config.ini");
  InitializeTrustedDevices(shared_identity_->TrustedDevicesPath());
  if (config_center_->SetPrivacyScreen(false) != 0 ||
      config_center_->SetSaveRemotePreviews(false) != 0)
    return 1;
  InitializeSettings();

  // The transport and device controllers do not need a Slint window, an SDL
  // video driver, a tray icon, or a logged-in user's audio/clipboard session.
  if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
  devices_.Initialize();
  local_host_ = std::make_shared<LocalHostChannel>(true);
  ULONGLONG last_status = 0;
  ULONGLONG next_peer_attempt = 0;
  while (WaitForSingleObject(stop_event, 16) == WAIT_TIMEOUT) {
    HandleSessionCleanup();
    HandlePasswordChangeResult();
    HandleCredentialRecovery();
    TickLocalHost();
    const auto now = GetTickCount64();
    if (!peer_ && now >= next_peer_attempt) {
      next_peer_attempt = now + 3000;
      if (CreateConnectionPeer() != 0) CloseConnectionPeer();
    }
    TickControllerTrust();
    // The GUI owns presence/announcement subscriptions on this transport.
    // Publishing an empty headless subscription would replace its device list.
    HandleScreenLock();
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
  remote_input_.Stop();
  CloseAllRemoteSessions();
  WaitForSessionCleanup();
  local_host_.reset();
  keyboard_.ReleaseAllRemotePressedKeys("unattended_host_exit");
  devices_.DestroyDevices();
  devices_.DestroyFactories();
  privacy_.Shutdown();
  HandleScreenLock();
  WriteUnattendedStatus(settings_.ActiveIdentity(), false, session);
  SDL_Quit();
  return 0;
}
}  // namespace crossdesk
