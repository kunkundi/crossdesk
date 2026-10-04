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
#include "service_host.h"
#include "service_lifetime.h"
#include "unattended_config.h"
#endif

#include "config_center.h"
#include "daemon.h"
#include "path_manager.h"
#include "platform/single_instance.h"
#include "rd_log.h"
#include "render.h"

#ifdef __linux__
#include "SimpleIni.h"
#include "platform/linux/headless/headless_console.h"
#include "platform/linux/headless/headless_session.h"
#include "platform/linux/privacy/privacy_guard.h"
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
      << "  --service-uninstall  Remove the installed Windows service\n"
      << "  --service-start      Start the Windows service\n"
      << "  --service-stop       Stop the Windows service\n"
      << "  --service-sas        Ask the service to send Secure Attention "
         "Sequence\n"
      << "  --service-ping       Ping the service over named pipe IPC\n"
      << "  --service-status     Query service runtime status\n"
      << "  --service-help       Show this help\n";
  std::cout
      << "  --unattended-enable  Enable a separate machine host before login\n"
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
    const auto service = GetSiblingServiceExecutablePath();
    if (service.empty() || !std::filesystem::exists(service)) {
      std::cerr << "Install CrossDesk with its Windows service first.\n";
      return 1;
    }
    if (command == "--unattended-disable") {
      // Disarm boot startup even if stopping the current process fails.
      const bool saved = crossdesk::SetUnattendedEnabled(false);
      const bool configured =
          saved && crossdesk::InstallCrossDeskService(service.wstring());
      const bool stopped = crossdesk::StopCrossDeskService(20000);
      std::cout << (configured && stopped
                        ? "Unattended host disabled.\n"
                        : "Failed to disable unattended host.\n");
      return configured && stopped ? 0 : 1;
    }
#if defined(CROSSDESK_DEBUG) || CROSSDESK_PORTABLE
    std::cerr << "Unattended mode requires the installed release build.\n";
    return 1;
#else
    const bool was_enabled = crossdesk::IsUnattendedEnabled();
    if (!crossdesk::PrepareUnattendedDirectory(true)) {
      std::cerr
          << "Cannot create a private machine profile. Existing permissive "
             "directories and links are rejected.\n";
      return 1;
    }
    if (crossdesk::IsCrossDeskServiceInstalled() &&
        !crossdesk::StopCrossDeskService(20000)) {
      std::cerr << "Could not stop the service to configure unattended mode.\n";
      return 1;
    }
    crossdesk::PathManager paths("CrossDesk");
    crossdesk::ConfigCenter source(
        (paths.GetCachePath() / "config.ini").string());
    const auto machine_config = crossdesk::UnattendedDataPath() / "config.ini";
    const auto staged_config =
        crossdesk::UnattendedDataPath() / "config.pending.ini";
    // Import only server/media preferences. Never share the GUI identity or
    // consume its file-transfer paths in the SYSTEM process.
    const auto host = source.IsSelfHosted() ? source.GetSignalServerHost()
                                            : source.GetDefaultServerHost();
    const int port = source.IsSelfHosted()
                         ? source.GetSignalServerPort()
                         : source.GetDefaultSignalServerPort();
    bool configured = std::filesystem::exists(machine_config);
    if (!configured) {
      crossdesk::ConfigCenter destination(staged_config.string());
      configured =
          destination.SetServerHost(host) == 0 &&
          destination.SetServerPort(port) == 0 &&
          destination.SetSelfHosted(source.IsSelfHosted()) == 0 &&
          destination.SetTurnMode(source.GetTurnMode()) == 0 &&
          destination.SetVideoEncodeFormat(source.GetVideoEncodeFormat()) ==
              0 &&
          destination.SetHardwareVideoCodec(source.IsHardwareVideoCodec()) ==
              0 &&
          destination.SetScreenCaptureMethod(
              crossdesk::ScreenCaptureMethod::Auto) == 0 &&
          destination.SetPrivacyScreen(false) == 0 &&
          destination.SetSaveRemotePreviews(false) == 0;
    }
    if (configured && !std::filesystem::exists(machine_config)) {
      configured = MoveFileExW(staged_config.c_str(), machine_config.c_str(),
                               MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!configured || !crossdesk::SetUnattendedEnabled(true) ||
        !crossdesk::InstallCrossDeskService(service.wstring()) ||
        !crossdesk::StartCrossDeskService()) {
      crossdesk::SetUnattendedEnabled(was_enabled);
      crossdesk::InstallCrossDeskService(service.wstring());
      if (was_enabled) crossdesk::StartCrossDeskService();
      std::cerr << "Failed to enable unattended host.\n";
      return 1;
    }
    std::cout << "Unattended host enabled for boot startup.\n"
                 "After it connects, use --unattended-status to obtain its "
                 "separate device ID and password.\n";
    return 0;
#endif
  }

  if (command == "--service-install") {
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

    bool success = crossdesk::InstallCrossDeskService(service_path.wstring());
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

static int RunApplication(
    int argc, char* argv[],
    crossdesk::platform::SingleInstanceGuard& client_instance) {
  // The instance guards are held before any configuration or logger is opened.
  auto path_manager = std::make_unique<crossdesk::PathManager>("CrossDesk");
  crossdesk::InitLogger(path_manager->GetLogPath().string());

  if (IsDaemonChild(argc, argv)) {
    // child process: run render directly
    crossdesk::Render render;
    return render.Run();
  }

#ifdef _WIN32
  if (!crossdesk::platform::MigrateLegacyAutostart("CrossDesk")) {
    crossdesk::get_logger()->warn(
        "Could not migrate the legacy Windows autostart entry");
  }
  TryStartManagedWindowsService();
#endif

  bool enable_daemon = false;
  if (path_manager) {
    std::string cache_path = path_manager->GetCachePath().string();
    crossdesk::ConfigCenter config_center(cache_path + "/config.ini");
    enable_daemon = config_center.IsEnableDaemon();
  }

#ifdef __linux__
  // The headless supervisor owns the display and application lifetime. A
  // detached daemon would outlive its display; systemd can restart the whole
  // session instead.
  if (std::getenv("CROSSDESK_HEADLESS_ACTIVE") ||
      crossdesk::HeadlessConsole::Instance().active()) enable_daemon = false;
#endif

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
}

int main(int argc, char* argv[]) {
#ifdef __linux__
  // Run before GUI, configuration, logging or display initialization.
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
    console.SetLanguage(static_cast<int>(config.GetLongValue("Settings", "language", 0)));
    std::cout << console.Text("console_cli_help");
    return 0;
  }
#endif
#ifdef _WIN32
  if (argc > 1 && std::strcmp(argv[1], "--unattended-host") == 0) {
    // Internal service entry point: never elevate an ordinary invocation into
    // a SYSTEM host, and never open the GUI or the per-user daemon here.
    if (argc != 4 || !crossdesk::IsLocalSystemProcess() ||
        !crossdesk::IsUnattendedEnabled())
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
            ? crossdesk::platform::MigrateLegacyAutostart("CrossDesk")
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
    if (AcquireInstance(cli_instance, crossdesk::platform::InstanceRole::kServiceCli) !=
        crossdesk::platform::InstanceResult::kAcquired) return 1;
    crossdesk::PathManager paths("CrossDesk");
    crossdesk::InitLogger(paths.GetLogPath().string(), "crossdesk-service-cli");
    return HandleServiceCliCommand(argv[1]);
  }
#endif

  using crossdesk::platform::InstanceResult;
  using crossdesk::platform::InstanceRole;
  crossdesk::platform::SingleInstanceGuard launcher_instance;
  crossdesk::platform::SingleInstanceGuard client_instance;
  if (!IsDaemonChild(argc, argv)) {
    const auto result = AcquireInstance(launcher_instance, InstanceRole::kLauncher);
    if (result != InstanceResult::kAcquired)
      return result == InstanceResult::kAlreadyRunning ? 0 : 1;
  }
  const auto result = AcquireInstance(client_instance, InstanceRole::kClient);
  if (result != InstanceResult::kAcquired)
    return result == InstanceResult::kAlreadyRunning ? 0 : 1;

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
