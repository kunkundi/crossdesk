#include "runtime/gui_runtime.h"

#include "rd_log.h"

namespace crossdesk {

void GuiRuntime::ResetRemoteServiceStatus(RemoteSession& props) {
  {
    std::lock_guard lock(props.privacy_status_mutex_);
    props.privacy_status_ = {};
    props.privacy_status_received_ = false;
    props.privacy_command_pending_ = false;
    props.privacy_status_tick_ = 0;
  }
  props.remote_service_status_received_ = false;
  props.remote_service_available_ = false;
  props.remote_interactive_stage_.clear();
  props.remote_consent_pending_ = false;
}

void GuiRuntime::ApplyRemoteServiceStatus(RemoteSession& props,
                                          const ServiceStatus& status) {
  const bool changed =
      props.remote_service_available_ != status.available ||
      props.remote_interactive_stage_ != status.interactive_stage ||
      props.remote_consent_pending_ != status.consent_pending;
  if (changed) {
    LOG_INFO("Remote service status changed: id={}, available={}, stage={}, "
             "consent_pending={}",
             props.remote_id_, status.available, status.interactive_stage,
             status.consent_pending);
  }
  props.remote_service_status_received_ = true;
  props.remote_service_available_ = status.available;
  props.remote_interactive_stage_ = status.interactive_stage;
  props.remote_consent_pending_ = status.consent_pending;
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
