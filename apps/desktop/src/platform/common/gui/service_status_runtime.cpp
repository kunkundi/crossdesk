#include "runtime/gui_runtime.h"

namespace crossdesk {

void GuiRuntime::ResetRemoteServiceStatus(RemoteSession& props) {
  props.remote_service_status_received_ = false;
  props.remote_service_available_ = false;
  props.remote_interactive_stage_.clear();
}

void GuiRuntime::ApplyRemoteServiceStatus(RemoteSession& props,
                                          const ServiceStatus& status) {
  props.remote_service_status_received_ = true;
  props.remote_service_available_ = status.available;
  props.remote_interactive_stage_ = status.interactive_stage;
}

GuiRuntime::RemoteUnlockState GuiRuntime::GetRemoteUnlockState(
    const RemoteSession& props) const {
  if (!props.remote_service_status_received_) {
    return RemoteUnlockState::none;
  }
  if (!props.remote_service_available_) {
    return RemoteUnlockState::service_unavailable;
  }
  if (props.remote_interactive_stage_ == "credential-ui") {
    return RemoteUnlockState::credential_ui;
  }
  if (props.remote_interactive_stage_ == "lock-screen") {
    return RemoteUnlockState::lock_screen;
  }
  if (props.remote_interactive_stage_ == "secure-desktop") {
    return RemoteUnlockState::secure_desktop;
  }
  return RemoteUnlockState::none;
}

#if !defined(_WIN32)
void GuiRuntime::HandleWindowsServiceIntegration() {}
#endif

}  // namespace crossdesk
