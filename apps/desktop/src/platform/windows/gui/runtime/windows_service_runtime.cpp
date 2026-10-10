#include "runtime/gui_runtime.h"

#include <SDL3/SDL_timer.h>

#include <remote_action.h>

#include <algorithm>
#include <cstring>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string>

#include "rd_log.h"

#if _WIN32
#include "interactive_desktop.h"
#include "desktop_transition_policy.h"
#include "interactive_state.h"
#include "service_host.h"
#include "unattended_config.h"
#endif

namespace crossdesk {
namespace {
#if _WIN32
struct WindowsServiceInteractiveStatus {
  bool available = false;
  bool session_mismatch = false;
  bool sas_secure_desktop_grace_active = false;
  bool consent_ui_visible = false;
  unsigned int error_code = 0;
  std::string interactive_stage;
  std::string error;
};

constexpr uint32_t kWindowsServiceStatusIntervalMs = 250;
constexpr uint32_t kWindowsServiceSasSecureDesktopGraceMs = 2000;
constexpr DWORD kWindowsServiceQueryTimeoutMs = 500;
constexpr DWORD kWindowsServiceSasTimeoutMs = 500;

bool IsTransientWindowsServiceStatusError(const std::string &error) {
  return error == "pipe_unavailable" || error == "pipe_connect_failed" ||
         error == "pipe_read_failed";
}

RemoteAction
BuildWindowsServiceStatusAction(const WindowsServiceInteractiveStatus &status) {
  RemoteAction action{};
  action.type = ControlType::service_status;
  action.ss.available = status.available;
  std::strncpy(action.ss.interactive_stage, status.interactive_stage.c_str(),
               sizeof(action.ss.interactive_stage) - 1);
  action.ss.interactive_stage[sizeof(action.ss.interactive_stage) - 1] = '\0';
  action.ss.consent_pending =
      status.consent_ui_visible &&
      IsSecureDesktopInteractionRequired(status.interactive_stage);
  return action;
}

bool ParseWindowsServiceInteractiveStatus(const std::string& response,
    WindowsServiceInteractiveStatus *status) {
  if (status == nullptr) {
    return false;
  }

  *status = WindowsServiceInteractiveStatus{};
  auto json = nlohmann::json::parse(response, nullptr, false);
  if (json.is_discarded() || !json.is_object()) {
    status->error = "invalid_service_status_json";
    return false;
  }

  status->available = json.value("ok", false);
  if (!status->available) {
    status->error = json.value("error", std::string("service_unavailable"));
    status->error_code = json.value("code", 0u);
    status->session_mismatch = status->error == "service_session_mismatch";
    return true;
  }

  if (!IsCurrentProcessSession(json.value("active_session_id", 0xFFFFFFFFu))) {
    status->available = false;
    status->session_mismatch = true;
    status->error = "service_session_mismatch";
    return true;
  }

  status->interactive_stage = json.value("interactive_stage", std::string());
  status->sas_secure_desktop_grace_active =
      json.value("sas_secure_desktop_grace_active", false);
  status->consent_ui_visible = json.value("consent_ui_visible", false);

  if (ShouldNormalizeUnlockToUserDesktop(
          json.value("interactive_lock_screen_visible", false),
          status->interactive_stage, json.value("session_locked", false),
          json.value("interactive_logon_ui_visible", false),
          json.value("interactive_secure_desktop_active",
                     json.value("secure_desktop_active", false)),
          json.value("credential_ui_visible", false),
          json.value("password_box_visible", false),
          json.value("unlock_ui_visible", false),
          json.value("last_session_event", std::string()))) {
    status->interactive_stage = "user-desktop";
  }
  return true;
}
#endif
} // namespace

void GuiRuntime::HandleWindowsServiceIntegration() {
#if _WIN32
  static bool last_logged_service_available = true;
  static unsigned int last_logged_service_error_code = 0;
  static std::string last_logged_service_error;

  if (!is_server_mode_ || peer_ == nullptr) {
    ResetLocalWindowsServiceState(true);
    return;
  }

  const bool has_connected_remote = [&] {
    std::shared_lock lock(connection_status_mutex_);
    return std::any_of(connection_status_.begin(), connection_status_.end(),
                       [](const auto &entry) {
                         return entry.second == ConnectionStatus::Connected;
                       });
  }();
  if (!has_connected_remote) {
    ResetLocalWindowsServiceState(false);
    return;
  }

  if (!windows_service_worker_) {
    windows_service_worker_ = std::make_unique<ServiceQueryWorker>(
        [](const std::string& command) {
          ServiceQueryResult result;
          result.response = QueryCrossDeskService(
              command, command == "sas" ? kWindowsServiceSasTimeoutMs
                                        : kWindowsServiceQueryTimeoutMs);
          if (command == "status") {
            // A successful service reply can still carry a pre-unlock sample.
            result.user_desktop_recovered = IsCurrentSessionUserDesktopActive();
          }
          return result;
        });
  }
  if (pending_windows_service_sas_.exchange(false, std::memory_order_relaxed)) {
    if (privacy_.Engaged()) {
      privacy_.Fail("Security shortcut requested; privacy screen will turn off");
    }
    windows_service_worker_->RequestSas();
  }
  if (pending_windows_service_cancel_consent_.exchange(
          false, std::memory_order_relaxed)) {
    windows_service_worker_->RequestCancelConsent();
  }

  const uint32_t now = static_cast<uint32_t>(SDL_GetTicks());
  auto result = windows_service_worker_->Take();
  if (result && result->command == "sas") {
    const std::string& response = result->response;
    auto json = nlohmann::json::parse(response, nullptr, false);
    if (json.is_discarded() || !json.value("ok", false)) {
      LOG_WARN("Remote SAS request failed: {}", response);
    } else {
      LOG_INFO("Remote SAS request forwarded to local Windows service");
      optimistic_windows_secure_desktop_until_tick_ =
          static_cast<uint32_t>(SDL_GetTicks()) +
          kWindowsServiceSasSecureDesktopGraceMs;
      local_service_status_received_ = true;
      local_service_available_ = true;
      local_interactive_stage_ = "secure-desktop";
    }
    last_windows_service_status_tick_ = 0;
    PublishWindowsInputStage();
    if (local_service_status_received_) {
      WindowsServiceInteractiveStatus optimistic;
      optimistic.available = local_service_available_;
      optimistic.interactive_stage = local_interactive_stage_;
      const auto message = BuildWindowsServiceStatusAction(optimistic).to_json();
      SendReliableDataFrame(peer_, message.data(), message.size(),
                            control_data_label_.c_str());
    }
    result.reset();
  }

  if (result && result->command == "cancel-consent") {
    auto json = nlohmann::json::parse(result->response, nullptr, false);
    if (json.is_discarded() || !json.value("ok", false)) {
      LOG_WARN("Remote consent cancel request failed: {}", result->response);
    } else {
      LOG_INFO("Remote consent cancel request forwarded to local Windows "
               "service, dismissed={}",
               json.value("dismissed", false));
    }
    // The stage changes as soon as the dialog dies, so skip the status wait.
    last_windows_service_status_tick_ = 0;
    result.reset();
  }

  if (last_windows_service_status_tick_ == 0 ||
      now - last_windows_service_status_tick_ >=
          kWindowsServiceStatusIntervalMs) {
    windows_service_worker_->RequestStatus();
    last_windows_service_status_tick_ = now;
  }
  if (!result) return;

  WindowsServiceInteractiveStatus status;
  const bool status_ok = ParseWindowsServiceInteractiveStatus(result->response, &status);
  const bool user_desktop_recovered = PreferUserDesktopInput(
      result->user_desktop_recovered, status.consent_ui_visible);
  if (status.available || status.session_mismatch) {
    windows_consent_ui_.store(status.available && status.consent_ui_visible);
  }
  if (user_desktop_recovered) status.interactive_stage = "user-desktop";
  WindowsServiceInteractiveStatus broadcast_status = status;
  const bool previous_secure_desktop_interaction =
      IsSecureDesktopInteractionRequired(local_interactive_stage_);
  const bool optimistic_secure_desktop_active =
      optimistic_windows_secure_desktop_until_tick_ != 0 &&
      static_cast<int32_t>(optimistic_windows_secure_desktop_until_tick_ -
                           now) > 0;
  const bool keep_optimistic_secure_desktop =
      !user_desktop_recovered && status_ok && status.available && optimistic_secure_desktop_active &&
      status.sas_secure_desktop_grace_active &&
      status.interactive_stage == "user-desktop";
  local_service_status_received_ =
      status_ok || previous_secure_desktop_interaction;
  const bool service_was_available =
      local_service_available_.exchange(status.available);
  if (status.available && !service_was_available && !IsAdministratorProcess()) {
    LOG_INFO("Unelevated host input routed through Windows service, including "
             "elevated applications on the user desktop");
  }
  if (status.available) {
    if (keep_optimistic_secure_desktop) {
      local_interactive_stage_ = "secure-desktop";
      broadcast_status.interactive_stage = local_interactive_stage_;
    } else {
      local_interactive_stage_ = status.interactive_stage;
      optimistic_windows_secure_desktop_until_tick_ = 0;
    }
  } else if (status.session_mismatch || user_desktop_recovered ||
             !previous_secure_desktop_interaction) {
    // A verified local desktop or a different service session invalidates the
    // cached secure stage. An unexplained IPC failure alone still does not.
    local_interactive_stage_ = status.interactive_stage;
    optimistic_windows_secure_desktop_until_tick_ = 0;
  }

  if (status_ok) {
    const bool availability_changed =
        status.available != last_logged_service_available;
    const bool error_changed =
        !status.available &&
        (status.error != last_logged_service_error ||
         status.error_code != last_logged_service_error_code);
    if (availability_changed || error_changed) {
      if (status.available) {
        LOG_INFO(
            "Local Windows service available for secure desktop integration");
      } else if (IsTransientWindowsServiceStatusError(status.error)) {
        LOG_INFO("Local Windows service temporarily unavailable: error={}, "
                 "code={}, local_stage='{}'",
                 status.error, status.error_code, local_interactive_stage_);
      } else {
        LOG_WARN(
            "Local Windows service unavailable, secure desktop integration "
            "disabled: error={}, code={}",
            status.error, status.error_code);
      }
      last_logged_service_available = status.available;
      last_logged_service_error = status.error;
      last_logged_service_error_code = status.error_code;
    }
  } else if (last_logged_service_available ||
             last_logged_service_error != "invalid_service_status_json") {
    LOG_WARN(
        "Local Windows service status query failed, secure desktop integration "
        "disabled");
    last_logged_service_available = false;
    last_logged_service_error = "invalid_service_status_json";
    last_logged_service_error_code = 0;
  }

  PublishWindowsInputStage();
  RemoteAction remote_action =
      BuildWindowsServiceStatusAction(broadcast_status);
  std::string msg = remote_action.to_json();
  int ret = SendReliableDataFrame(peer_, msg.data(), msg.size(),
                                  control_data_label_.c_str());
  if (ret != 0) {
    LOG_WARN("Broadcast Windows service status failed, ret={}", ret);
  }
#endif
}

#if _WIN32
void GuiRuntime::ResetLocalWindowsServiceState(bool clear_pending_sas) {
  if (windows_service_worker_) windows_service_worker_->Reset();
  last_windows_service_status_tick_ = 0;
  if (clear_pending_sas) {
    pending_windows_service_sas_.store(false, std::memory_order_relaxed);
  }
  local_service_status_received_ = false;
  local_service_available_ = false;
  windows_consent_ui_.store(false);
  local_interactive_stage_.clear();
  optimistic_windows_secure_desktop_until_tick_ = 0;
  PublishWindowsInputStage();
}
#endif

} // namespace crossdesk
