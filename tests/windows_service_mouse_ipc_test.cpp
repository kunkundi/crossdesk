#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::filesystem::path FindRepoRoot() {
  std::filesystem::path current = std::filesystem::current_path();
  while (!current.empty()) {
    if (std::filesystem::exists(current / "xmake.lua") &&
        std::filesystem::exists(current / "src/service/windows/service_host.cpp")) {
      return current;
    }
    current = current.parent_path();
  }
  return {};
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }

  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

bool ExpectContains(const char* name, const std::string& value,
                    const std::string& expected) {
  if (value.find(expected) != std::string::npos) {
    return true;
  }

  std::cerr << name << " missing expected text: " << expected << "\n";
  return false;
}

bool ExpectNotContains(const char* name, const std::string& value,
                       const std::string& unexpected) {
  if (value.find(unexpected) == std::string::npos) {
    return true;
  }

  std::cerr << name << " contains unexpected text: " << unexpected << "\n";
  return false;
}

}  // namespace

int main() {
  const std::filesystem::path repo_root = FindRepoRoot();
  if (repo_root.empty()) {
    std::cerr << "failed to locate repository root\n";
    return 1;
  }

  const std::string service_host =
      ReadFile(repo_root / "src/service/windows/service_host.cpp");
  const std::string service_host_h =
      ReadFile(repo_root / "src/service/windows/service_host.h");
  const std::string session_helper =
      ReadFile(repo_root / "src/service/windows/session_helper_main.cpp");
  const std::string targets =
      ReadFile(repo_root / "xmake/targets.lua");
  const std::string interactive_state =
      ReadFile(repo_root / "src/service/windows/interactive_state.h");
  const std::string render_callback =
      ReadFile(repo_root / "src/gui/render_callback.cpp");
  const std::string render = ReadFile(repo_root / "src/gui/render.cpp");
  const std::string screen_capturer_h =
      ReadFile(repo_root / "src/screen_capturer/windows/screen_capturer_win.h");
  const std::string screen_capturer_cpp =
      ReadFile(repo_root / "src/screen_capturer/windows/screen_capturer_win.cpp");

  bool ok = true;
  ok &= ExpectContains("service_host.cpp", service_host,
                       "ParseSecureDesktopMouseIpcCommand");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "BuildSecureInputHelperMouseCommand");
  ok &= ExpectContains("targets.lua", targets,
                       "target(\"crossdesk_session_helper\")");
  ok &= ExpectContains("targets.lua", targets,
                       "add_files(crossdesk_windows_resource)");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "EnablePerMonitorDpiAwareness");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "SetProcessDpiAwarenessContext(\n"
                       "      DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "EnablePerMonitorDpiAwareness();\n\n"
                       "  InitializeHelperLogger();");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "const ULONGLONG deadline_tick = GetTickCount64() + timeout_ms");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "while (GetTickCount64() <= deadline_tick)");
  ok &= ExpectNotContains("service_host.cpp", service_host,
                          "constexpr int kPipeConnectRetryCount = 3");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "BuildSecureInputHelperKeyboardCommand(");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "const std::string& interactive_stage");
  ok &= ExpectContains("service_host.h", service_host_h,
                       "bool LaunchSecureInputHelper(DWORD session_id,\n"
                       "                               const std::string& interactive_stage,\n"
                       "                               const std::string& interactive_desktop)");
  ok &= ExpectContains("service_host.h", service_host_h,
                       "std::string secure_input_helper_interactive_stage_");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "SecureInputHelperDesktopForStage");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "IsConsentUiRunningInSession");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "L\"Consent.exe\"");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "IsConsentUiRunningInCurrentSession");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "L\"Consent.exe\"");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "desktop_info.available && consent_ui_visible");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "session_helper_report_input_desktop_available_ &&");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "session_helper_report_consent_ui_visible_");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "secure_input_helper_interactive_desktop_");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "SecureInputHelperDesktopForStage("
                       "interactive_stage, interactive_desktop)");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "return L\"winsta0\\\\\" + interactive_desktop_w");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "return L\"winsta0\\\\Winlogon\"");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "return L\"winsta0\\\\default\"");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "secure_input_helper_interactive_stage_ == interactive_stage");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "secure_input_helper_interactive_stage_ = interactive_stage");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "secure_input_helper_interactive_stage_.clear()");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "LaunchSecureInputHelper(target_session_id, interactive_stage,\n"
                       "                                 interactive_desktop)");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "\\\"secure_input_helper_stage\\\":\\\"");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "session_helper_report_interactive_stage_");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "return SendSecureDesktopMouseInput");
  ok &= ExpectContains("render.cpp", render,
                       "constexpr DWORD kWindowsServiceQueryTimeoutMs = 500");
  ok &= ExpectContains("screen_capturer_win.cpp", screen_capturer_cpp,
                       "constexpr DWORD kSecureDesktopStatusPipeTimeoutMs = 500");
  ok &= ExpectContains("render.cpp", render,
                       "IsTransientWindowsServiceStatusError(status.error)");
  ok &= ExpectContains("screen_capturer_win.cpp", screen_capturer_cpp,
                       "IsTransientWindowsServiceStatusError(status.error)");
  ok &= ExpectContains("render.cpp", render,
                       "Local Windows service temporarily unavailable");
  ok &= ExpectContains("screen_capturer_win.cpp", screen_capturer_cpp,
                       "Windows capturer secure desktop service temporarily unavailable");
  ok &= ExpectContains("screen_capturer_win.cpp", screen_capturer_cpp,
                       "Windows capturer secure desktop transient frame query failed");
  ok &= ExpectContains("screen_capturer_win.cpp", screen_capturer_cpp,
                       "if (transient_error) {\n"
                       "          LOG_INFO(");
  ok &= ExpectContains("render_callback.cpp", render_callback,
                       "IsTransientSecureDesktopInputFailure");
  ok &= ExpectContains("render_callback.cpp", render_callback,
                       "Secure desktop keyboard injection transient failure");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "MOUSEEVENTF_VIRTUALDESK");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "std::vector<INPUT> inputs");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "SendInput(static_cast<UINT>(inputs.size())");
  ok &= ExpectNotContains("session_helper_main.cpp", session_helper,
                          "SetCursorPos(request.x, request.y)");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "NormalizeAbsoluteMouseCoordinate");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "EnsureThreadInteractiveDesktop");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "OpenInputDesktop");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "DesktopNameForInteractiveStage");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "interactive_stage == \"credential-ui\"");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "return L\"Winlogon\"");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "interactive_stage == \"lock-screen\"");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "return L\"Default\"");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "EnsureThreadInteractiveDesktopForStage");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "switch_interactive_desktop_failed");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "Json BuildInputFailureJson");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "json[\"target_desktop\"]");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "json[\"current_desktop\"]");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "json[\"stage\"]");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "ParseSecureInputKeyboardCommand(command, &key_code, &is_down, &scan_code,\n"
                       "                                      &extended, &interactive_stage,\n"
                       "                                      &interactive_desktop)");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "InjectKeyboardInput(key_code, is_down, scan_code, extended,\n"
                       "                            interactive_stage, interactive_desktop)");
  ok &= ExpectContains("session_helper_main.cpp", session_helper,
                       "InjectMouseInput(mouse_request)");
  ok &= ExpectNotContains("session_helper_main.cpp", session_helper,
                          "EnsureThreadDesktop(L\"Winlogon\", &secure_desktop)");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "winsta0\\\\default");
  ok &= ExpectNotContains("service_host.cpp", service_host,
                          "startup_info.lpDesktop = const_cast<LPWSTR>(L\"winsta0\\\\Winlogon\")");
  ok &= ExpectContains("interactive_state.h", interactive_state,
                       "interactive_stage == \"lock-screen\"");
  ok &= ExpectContains("render_callback.cpp", render_callback,
                       "RemoteAction remote_action{};");
  ok &= ExpectContains("render.cpp", render,
                       "previous_secure_desktop_interaction");
  ok &= ExpectNotContains(
      "render_callback.cpp", render_callback,
      "render->local_service_available_ &&\n"
      "        IsSecureDesktopInteractionRequired(render->local_interactive_stage_)");
  ok &= ExpectContains("screen_capturer_win.h", screen_capturer_h,
                       "std::string secure_shared_stage_;");
  ok &= ExpectContains("screen_capturer_win.cpp", screen_capturer_cpp,
                       "const std::string& stage");
  ok &= ExpectContains("screen_capturer_win.cpp", screen_capturer_cpp,
                       "secure_shared_stage_ == stage");
  ok &= ExpectContains("screen_capturer_win.cpp", screen_capturer_cpp,
                       "secure_shared_stage_ = stage");
  ok &= ExpectContains("screen_capturer_win.cpp", screen_capturer_cpp,
                       "secure_shared_stage_.clear()");
  return ok ? 0 : 1;
}
