#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "interactive_state.h"

namespace {

std::filesystem::path FindRepoRoot() {
  std::filesystem::path current = std::filesystem::current_path();
  while (!current.empty()) {
    if (std::filesystem::exists(current / "xmake.lua") &&
        std::filesystem::exists(current /
                                "src/service/windows/service_host.cpp")) {
      return current;
    }
    current = current.parent_path();
  }
  return {};
}

std::string ReadFile(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }

  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

bool ExpectContains(const char *name, const std::string &value,
                    const std::string &expected) {
  if (value.find(expected) != std::string::npos) {
    return true;
  }

  std::cerr << name << " missing expected text: " << expected << "\n";
  return false;
}

bool ExpectNotContains(const char *name, const std::string &value,
                       const std::string &unexpected) {
  if (value.find(unexpected) == std::string::npos) {
    return true;
  }

  std::cerr << name << " contains unexpected text: " << unexpected << "\n";
  return false;
}

bool ExpectTrue(const char *name, bool value) {
  if (value) {
    return true;
  }

  std::cerr << name << " expected true\n";
  return false;
}

}  // namespace

int main() {
  const std::filesystem::path repo_root = FindRepoRoot();
  if (repo_root.empty()) {
    std::cerr << "failed to locate repository root\n";
    return 1;
  }

  const std::string stream_window =
      ReadFile(repo_root / "src/gui/ui/stream_window.slint");
  const std::string gui_application =
      ReadFile(repo_root / "src/gui/application/gui_application.cpp");
  const std::string windows_service_runtime =
      ReadFile(repo_root / "src/gui/runtime/windows_service_runtime.cpp");
  const std::string runtime_state_h =
      ReadFile(repo_root / "src/gui/runtime/runtime_state.h");
  const std::string service_host =
      ReadFile(repo_root / "src/service/windows/service_host.cpp");
  const std::string service_host_h =
      ReadFile(repo_root / "src/service/windows/service_host.h");
  const std::string session_helper =
      ReadFile(repo_root / "src/service/windows/session_helper_main.cpp");

  bool ok = true;
  ok &= ExpectTrue(
      "secure desktop input routing",
      crossdesk::IsSecureDesktopInteractionRequired("secure-desktop"));
  ok &= ExpectContains("stream_window.slint", stream_window,
                       "for shortcut in [\"Ctrl+Alt+Del\", \"Win+L\"]");
  ok &= ExpectContains("stream_window.slint", stream_window,
                       "root.send-shortcut(shortcut)");
  ok &= ExpectContains("gui_application.cpp", gui_application,
                       "std::string(shortcut) == \"Ctrl+Alt+Del\"");
  ok &= ExpectContains("gui_application.cpp", gui_application,
                       "ServiceCommandFlag::send_sas");
  ok &= ExpectContains("gui_application.cpp", gui_application,
                       "ServiceCommandFlag::lock_workstation");
  ok &= ExpectNotContains("windows_service_runtime.cpp",
                          windows_service_runtime, "sas_requires_lock_screen");
  ok &= ExpectContains("runtime_state.h", runtime_state_h,
                       "optimistic_windows_secure_desktop_until_tick_");
  ok &= ExpectContains("windows_service_runtime.cpp", windows_service_runtime,
                       "kWindowsServiceSasSecureDesktopGraceMs");
  ok &= ExpectContains("windows_service_runtime.cpp", windows_service_runtime,
                       "status->sas_secure_desktop_grace_active");
  ok &=
      ExpectContains("windows_service_runtime.cpp", windows_service_runtime,
                     "json.value(\"sas_secure_desktop_grace_active\", false)");
  ok &= ExpectContains("windows_service_runtime.cpp", windows_service_runtime,
                       "status.sas_secure_desktop_grace_active");
  ok &= ExpectContains("windows_service_runtime.cpp", windows_service_runtime,
                       "local_interactive_stage_ = \"secure-desktop\"");
  ok &= ExpectContains("service_host.h", service_host_h,
                       "sas_secure_desktop_until_tick_");
  ok &= ExpectContains("service_host.h", service_host_h,
                       "sas_secure_desktop_seen_");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "kSasSecureDesktopGraceMs");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "IsSasSecureDesktopGraceActiveLocked()");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "UpdateSasSecureDesktopGraceLocked("
                       "session_helper_report_interactive_stage_)");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "sas_secure_desktop_seen_ = true");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "sas_secure_desktop_until_tick_ = 0");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "sas_secure_desktop_until_tick_ =");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "now + kSasSecureDesktopGraceMs");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "\\\"sas_secure_desktop_grace_active\\\"");
  ok &=
      ExpectContains("service_host.cpp", service_host,
                     "raw_interactive_stage = ResolveInteractiveStageLocked()");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "kSessionHelperStatePollMs = 1000");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "EVENT_SYSTEM_DESKTOPSWITCH");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "SetWinEventHook(");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "MsgWaitForMultipleObjects");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "WaitForSessionHelperStateChange(stop_event, "
                       "desktop_switch_event)");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "inaccessible_secure_input_desktop");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "desktop_info.error_code == ERROR_ACCESS_DENIED");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "secure_desktop_active = input_desktop_is_winlogon ||");
  return ok ? 0 : 1;
}
