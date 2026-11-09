#ifdef _WIN32
#ifdef CROSSDESK_DEBUG
#pragma comment(linker, "/subsystem:\"console\"")
#else
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
#endif
#endif

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <cstdio>

#include "platform/windows/autostart_task.h"
#include "platform/windows/gui/slint_backend.h"
#include "platform/windows/runtime_daemon.h"
#include "platform/windows/service/autostart_control.h"
#include "service_host.h"
#include "service_lifetime.h"
#include "shutdown_order.h"
#include "unattended_config.h"
#include "shared_identity.h"
#endif

#include "config_center.h"
#include "daemon.h"
#include "path_manager.h"
#include "platform/instance_activation.h"
#include "platform/single_instance.h"
#include "rd_log.h"
#include "render.h"
#if CROSSDESK_HAS_UPDATER
#include "updater/update_platform.h"
#endif

#ifdef __linux__
#include "SimpleIni.h"
#include "platform/linux/autostart.h"
#include "platform/linux/headless/headless_console.h"
#include "platform/linux/headless/headless_session.h"
#include "platform/linux/privacy/privacy_guard.h"
#include "platform/linux/runtime_daemon.h"
#endif

#ifdef _WIN32
namespace {

void EnsureConsoleForCli() {
  static bool console_ready = false;
  if (console_ready) {
    return;
  }

  if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
    DWORD error = GetLastError();
    if (error != ERROR_ACCESS_DENIED) {
      AllocConsole();
    }
  }

  FILE* stream = nullptr;
  freopen_s(&stream, "CONOUT$", "w", stdout);
  freopen_s(&stream, "CONOUT$", "w", stderr);
  freopen_s(&stream, "CONIN$", "r", stdin);
  SetConsoleOutputCP(CP_UTF8);
  console_ready = true;
}

void PrintServiceCliUsage() {
  std::cout
      << "CrossDesk service management commands\n"
      << "  --service-install    Install the sibling crossdesk_service.exe\n"
      << "  --service-repair     Update the service path and restart it\n"
      << "  --service-uninstall  Remove the installed Windows service\n"
      << "  --service-start      Start the Windows service\n"
      << "  --service-stop       Stop the Windows service\n"
      << "  --service-sas        Ask the service to send Secure Attention "
         "Sequence\n"
      << "  --service-ping       Ping the service over named pipe IPC\n"
      << "  --service-status     Query service runtime status\n"
      << "  --service-help       Show this help\n";
  std::cout
      << "  --unattended-enable  Enable the shared device identity before login\n"
      << "  --unattended-disable Stop the machine host and disable boot "
         "startup\n"
      << "  --unattended-status  Show machine host status and connection "
         "credentials\n";
}

std::wstring GetCurrentExecutablePathW() {
  wchar_t path[MAX_PATH] = {0};
  DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return L"";
  }
  return std::wstring(path, length);
}

std::filesystem::path GetSiblingServiceExecutablePath() {
  std::wstring current_executable = GetCurrentExecutablePathW();
  if (current_executable.empty()) {
    return {};
  }

  return std::filesystem::path(current_executable).parent_path() /
         L"crossdesk_service.exe";
}

bool IsServiceCliCommand(const char* arg) {
  if (arg == nullptr) {
    return false;
  }

  return std::strcmp(arg, "--service-install") == 0 ||
         std::strcmp(arg, "--service-repair") == 0 ||
         std::strcmp(arg, "--unattended-enable") == 0 ||
         std::strcmp(arg, "--unattended-disable") == 0 ||
         std::strcmp(arg, "--unattended-status") == 0 ||
         std::strcmp(arg, "--service-uninstall") == 0 ||
         std::strcmp(arg, "--service-start") == 0 ||
         std::strcmp(arg, "--service-stop") == 0 ||
         std::strcmp(arg, "--service-sas") == 0 ||
         std::strcmp(arg, "--service-ping") == 0 ||
         std::strcmp(arg, "--service-status") == 0 ||
         std::strcmp(arg, "--service-help") == 0;
}

void TryStartManagedWindowsService() {
  std::filesystem::path service_path = GetSiblingServiceExecutablePath();
  if (service_path.empty() || !std::filesystem::exists(service_path)) {
    return;
  }

  if (!crossdesk::IsCrossDeskServiceInstalled()) {
    return;
  }

  if (crossdesk::IsUnattendedEnabled() && !crossdesk::IsSharedIdentityServiceConfigured()) {
    if (!crossdesk::RepairCrossDeskService(service_path.wstring()))
      crossdesk::get_logger()->warn("Could not prepare the service for shared-identity migration");
    return;
  }
  crossdesk::StartCrossDeskService();
}

int HandleServiceCliCommand(const std::string& command) {
  EnsureConsoleForCli();

  if (command == "--service-help") {
    PrintServiceCliUsage();
    return 0;
  }

  if (command.rfind("--unattended-", 0) == 0) {
    if (!crossdesk::IsAdministratorProcess()) {
      std::cerr << "Run this command from an administrator terminal.\n";
      return 1;
    }
    if (command == "--unattended-status") {
      std::cout << crossdesk::ReadUnattendedStatus() << '\n';
      return 0;
    }
    const bool enabled = command == "--unattended-enable";
    std::string error;
    if (!crossdesk::SetServiceAutostart(enabled, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    std::cout << (enabled
        ? "Unattended host enabled for boot startup with the GUI's device ID and password.\n"
        : "Unattended host disabled.\n");
    return 0;
  }

  if (command == "--service-install" || command == "--service-repair") {
    std::filesystem::path service_path = GetSiblingServiceExecutablePath();
    if (service_path.empty()) {
      std::cerr << "Failed to locate crossdesk_service.exe" << std::endl;
      return 1;
    }
    if (!std::filesystem::exists(service_path)) {
      std::cerr << "Service binary not found: " << service_path.string()
                << std::endl;
      return 1;
    }

    bool success = command == "--service-repair"
                       ? crossdesk::RepairCrossDeskService(service_path.wstring())
                       : crossdesk::InstallCrossDeskService(service_path.wstring());
    std::cout << (success ? "install ok" : "install failed") << std::endl;
    return success ? 0 : 1;
  }

  if (command == "--service-uninstall") {
    bool success = crossdesk::UninstallCrossDeskService();
    std::cout << (success ? "uninstall ok" : "uninstall failed") << std::endl;
    return success ? 0 : 1;
  }

  if (command == "--service-start") {
    bool success = crossdesk::StartCrossDeskService();
    std::cout << (success ? "start ok" : "start failed") << std::endl;
    return success ? 0 : 1;
  }

  if (command == "--service-stop") {
    bool success = crossdesk::StopCrossDeskService();
    std::cout << (success ? "stop ok" : "stop failed") << std::endl;
    return success ? 0 : 1;
  }

  if (command == "--service-sas") {
    std::cout << crossdesk::QueryCrossDeskService("sas") << std::endl;
    return 0;
  }

  if (command == "--service-ping") {
    std::cout << crossdesk::QueryCrossDeskService("ping") << std::endl;
    return 0;
  }

  if (command == "--service-status") {
    std::cout << crossdesk::QueryCrossDeskService("status") << std::endl;
    return 0;
  }

  PrintServiceCliUsage();
  return 1;
}

}  // namespace
#endif

static bool IsDaemonChild(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--child") == 0) return true;
  }
  return false;
}

static crossdesk::platform::InstanceResult AcquireInstance(
    crossdesk::platform::SingleInstanceGuard& guard,
    crossdesk::platform::InstanceRole role) {
  std::string error;
  const auto result = guard.TryAcquire(role, error);
  if (result == crossdesk::platform::InstanceResult::kAlreadyRunning) {
    std::cerr << "CrossDesk is already running for this user.\n";
  } else if (result == crossdesk::platform::InstanceResult::kError) {
    std::cerr << "CrossDesk cannot start: " << error << '\n';
  }
  return result;
}

static int ExitForExistingInstance(
    crossdesk::platform::InstanceResult result, bool interactive_launch) {
  if (interactive_launch &&
      result == crossdesk::platform::InstanceResult::kAlreadyRunning) {
    std::string error;
    if (!crossdesk::platform::RequestInstanceActivation(error)) {
      std::cerr << error << '\n';
#ifdef _WIN32
      MessageBoxW(nullptr,
                  L"CrossDesk is already running, but its window could not be "
                  L"activated.\n\nIt may still be starting or be running in "
                  L"another Windows session. Please check the system tray.",
                  L"CrossDesk", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
#endif
    }
  }
  return result == crossdesk::platform::InstanceResult::kAlreadyRunning ? 0 : 1;
}

static int RunApplication(
    int argc, char* argv[],
    crossdesk::platform::SingleInstanceGuard& client_instance) {
  // The instance guards are held before any configuration or logger is opened.
  auto path_manager = std::make_unique<crossdesk::PathManager>("CrossDesk");
  crossdesk::InitLogger(path_manager->GetLogPath().string());

#ifdef _WIN32
  crossdesk::get_logger()->info(
      "CrossDesk executable: {}",
      std::filesystem::path(GetCurrentExecutablePathW()).u8string());
#endif

  if (IsDaemonChild(argc, argv)) {
    // child process: run render directly
    crossdesk::Render render;
    return render.Run();
  }

#ifdef _WIN32
  if (!crossdesk::CleanupLegacyServiceAutostart()) {
    crossdesk::get_logger()->warn(
        "Could not remove legacy Windows logon startup");
  }
  TryStartManagedWindowsService();
#endif

  bool enable_daemon = false;
  if (path_manager) {
    std::string cache_path = path_manager->GetCachePath().string();
    crossdesk::ConfigCenter config_center(cache_path + "/config.ini");
    enable_daemon = config_center.IsEnableDaemon();
#ifdef __linux__
    if (config_center.IsEnableAutostart() &&
        !crossdesk::platform::MigrateLegacyAutostart("CrossDesk")) {
      crossdesk::get_logger()->warn(
          "Could not migrate the legacy Linux autostart entry");
    }
#endif
  }

#ifdef __linux__
  // The headless supervisor owns the display and application lifetime. A
  // detached daemon would outlive its display; systemd can restart the whole
  // session instead.
  if (std::getenv("CROSSDESK_HEADLESS_ACTIVE") ||
      crossdesk::HeadlessConsole::Instance().active()) enable_daemon = false;

  auto& runtime_daemon = crossdesk::platform::LinuxRuntimeDaemon::Instance();
  runtime_daemon.Initialize(
      argc, argv, !std::getenv("CROSSDESK_HEADLESS_ACTIVE") &&
                      !crossdesk::HeadlessConsole::Instance().active());
  std::string daemon_error;
  if (!runtime_daemon.SetEnabled(enable_daemon, daemon_error)) {
    crossdesk::get_logger()->warn("Could not start daemon: {}", daemon_error);
  }
  crossdesk::Render render;
  const int result = render.Run();
  runtime_daemon.Shutdown(result != 0);
  return result;
#elif defined(_WIN32)
  auto& runtime_daemon = crossdesk::platform::WindowsRuntimeDaemon::Instance();
  runtime_daemon.Initialize();
  std::string daemon_error;
  if (!runtime_daemon.SetEnabled(enable_daemon, daemon_error)) {
    crossdesk::get_logger()->warn("Could not start daemon: {}", daemon_error);
  }
  crossdesk::Render render;
  const int result = render.Run();
  runtime_daemon.Shutdown(result != 0);
  return result;
#else

  if (enable_daemon) {
    // The launcher lease stays in main for the supervisor's entire lifetime.
    // Each --child must acquire its own client lease, so it is neither blocked
    // by its supervisor nor able to bypass the single-client restriction.
    client_instance.Release();
    // start daemon with restart monitoring
    Daemon daemon("CrossDesk");

    // define main loop function: run render and stop daemon on normal exit
    Daemon::MainLoopFunc main_loop = [&daemon, &client_instance]() {
      // Executable discovery can fail and make the supervisor run the GUI in
      // process. This fallback must retain the same client ownership rule.
      if (AcquireInstance(client_instance,
                          crossdesk::platform::InstanceRole::kClient) !=
          crossdesk::platform::InstanceResult::kAcquired) {
        daemon.stop();
        return;
      }
      crossdesk::Render render;
      render.Run();
      daemon.stop();
    };

    // start daemon and return result
    bool success = daemon.start(main_loop);
    return success ? 0 : 1;
  }

  // run without daemon: direct execution
  crossdesk::Render render;
  return render.Run();
#endif
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
  if (argc >= 2 &&
      std::strcmp(argv[1], crossdesk::platform::kWindowsDaemonArgument) == 0) {
    return crossdesk::platform::RunWindowsRuntimeDaemon();
  }
  // Apply in every GUI process, including legacy --child launches. The watcher
  // keeps the default shutdown priority so it ends before this remote host.
  const DWORD shutdown_error = crossdesk::ConfigureWindowsShutdownOrder(
      crossdesk::WindowsShutdownRole::host);
  if (shutdown_error != ERROR_SUCCESS) {
    std::cerr << "Could not configure Windows host shutdown order, error="
              << shutdown_error << '\n';
  }
#endif
#ifdef __linux__
  // Run before GUI, configuration, logging or display initialization.
  if (argc >= 2 &&
      std::strcmp(argv[1], crossdesk::platform::kLinuxDaemonArgument) == 0) {
    return crossdesk::platform::RunLinuxRuntimeDaemon(argc, argv);
  }
  if (argc == 2 &&
      std::strcmp(argv[1], crossdesk::kLinuxPrivacyGuardArgument) == 0) {
    return crossdesk::RunLinuxPrivacyGuard();
  }
  auto& console = crossdesk::HeadlessConsole::Instance();
  if (argc == 2 && std::strcmp(argv[1], "--headless-help") == 0) {
    crossdesk::PathManager paths("CrossDesk");
    CSimpleIniA config;
    config.SetUnicode(true);
    config.LoadFile((paths.GetCachePath() / "config.ini").string().c_str());
    console.SetLanguage(static_cast<int>(config.GetLongValue("Settings", "language", 10)));
    std::cout << console.Text("console_cli_help");
    return 0;
  }
#endif
#ifdef _WIN32
  if (argc > 1 && std::strcmp(argv[1], "--unattended-host") == 0) {
    // Internal service entry point: never elevate an ordinary invocation into
    // a SYSTEM host, and never open the GUI or the per-user daemon here.
    if (argc != 4 || !crossdesk::IsLocalSystemProcess() ||
        (!crossdesk::IsUnattendedEnabled() && !crossdesk::SharedHostIdentity::GuiPresent()) ||
        !crossdesk::PrepareUnattendedDirectory(false))
      return 1;
    const std::string name(argv[2]);
    if (name.rfind("Global\\CrossDeskUnattendedStop-", 0) != 0) return 1;
    const std::wstring wide_name(name.begin(), name.end());
    HANDLE stop = OpenEventW(SYNCHRONIZE, FALSE, wide_name.c_str());
    if (!stop) return 1;
    const std::string lifetime_name(argv[3]);
    crossdesk::ServiceLifetimeWatcher lifetime;
    if (!lifetime.Start(
            std::wstring(lifetime_name.begin(), lifetime_name.end()))) {
      CloseHandle(stop);
      return 1;
    }
    // Runtime members can log during construction. Select the protected host
    // log before constructing Render, not after its logger has already opened.
    crossdesk::InitLogger((crossdesk::UnattendedDataPath() / L"logs").string());
    crossdesk::Render render;
    const int result = render.RunUnattended(stop);
    CloseHandle(stop);
    return result;
  }
  // Installer maintenance must not start the GUI or the process supervisor.
  if (argc == 2 && (std::strcmp(argv[1], "--autostart-migrate") == 0 ||
                    std::strcmp(argv[1], "--autostart-uninstall") == 0)) {
    crossdesk::PathManager paths("CrossDesk");
    crossdesk::InitLogger(paths.GetLogPath().string(),
                          "crossdesk-autostart-cli");
    const bool success =
        std::strcmp(argv[1], "--autostart-migrate") == 0
            ? crossdesk::CleanupLegacyServiceAutostart()
            : crossdesk::platform::RemoveInstalledAutostart("CrossDesk");
    return success ? 0 : 1;
  }
  if (argc == 2 &&
      std::strcmp(argv[1], crossdesk::kSlintRendererProbeArgument) == 0) {
    return crossdesk::RunSlintRendererProbe();
  }
  if (argc > 1 && IsServiceCliCommand(argv[1])) {
    EnsureConsoleForCli();
    crossdesk::platform::SingleInstanceGuard cli_instance;
    // Unattended changes acquire this lease in the shared GUI/CLI entry point.
    const bool unattended_change = std::strcmp(argv[1], "--unattended-enable") == 0 ||
        std::strcmp(argv[1], "--unattended-disable") == 0;
    if (!unattended_change &&
        AcquireInstance(cli_instance, crossdesk::platform::InstanceRole::kServiceCli) !=
            crossdesk::platform::InstanceResult::kAcquired) return 1;
    crossdesk::PathManager paths("CrossDesk");
    crossdesk::InitLogger(paths.GetLogPath().string(), "crossdesk-service-cli");
    return HandleServiceCliCommand(argv[1]);
  }
#endif

  using crossdesk::platform::InstanceResult;
#if CROSSDESK_HAS_UPDATER
  // Installer CLI commands above stay available during maintenance. Ordinary
  // launches (including daemon restarts) must not reopen the installation.
  try {
    if (crossdesk::updater::MaintenanceActive(crossdesk::updater::ExecutablePath()))
      return 0;
  } catch (const std::exception& error) {
    std::cerr << "Cannot check update maintenance: " << error.what() << '\n';
    return 1;
  }
#endif
  using crossdesk::platform::InstanceRole;
  crossdesk::platform::SingleInstanceGuard launcher_instance;
  crossdesk::platform::SingleInstanceGuard client_instance;
  if (!IsDaemonChild(argc, argv)) {
    const auto result = AcquireInstance(launcher_instance, InstanceRole::kLauncher);
    if (result != InstanceResult::kAcquired)
      return ExitForExistingInstance(result, true);
  }
  const auto result = AcquireInstance(client_instance, InstanceRole::kClient);
  if (result != InstanceResult::kAcquired)
    return ExitForExistingInstance(result, !IsDaemonChild(argc, argv));

#ifdef __linux__
  const int exit_code = crossdesk::RunWithLinuxDisplay(
      argc, argv, [&] { return RunApplication(argc, argv, client_instance); }, [&] {
        crossdesk::PathManager paths("CrossDesk");
        if (console.Enable(paths.GetLogPath(), paths.GetCachePath() / "config.ini")) return true;
        std::cerr << "无法初始化日志或控制台，启动失败。\n";
        return false;
      });
  if (console.active()) {
    console.Notify(exit_code == 0 ? console.Text("console_exited")
        : console.Text("console_stopped") + " " + std::to_string(exit_code) +
          "; " + console.Text("console_log_file") + ": " + console.diagnostic_path());
  }
  return exit_code;
#else
  return RunApplication(argc, argv, client_instance);
#endif
}
