#ifdef _WIN32
#ifdef CROSSDESK_DEBUG
#pragma comment(linker, "/subsystem:\"console\"")
#else
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
#endif
#endif

#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <cstdio>
#include "service_host.h"
#endif

#include "config_center.h"
#include "daemon.h"
#include "path_manager.h"
#include "render.h"

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
  << "  --service-sas        Ask the service to send Secure Attention Sequence\n"
      << "  --service-ping       Ping the service over named pipe IPC\n"
      << "  --service-status     Query service runtime status\n"
      << "  --service-help       Show this help\n";
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
         std::strcmp(arg, "--service-uninstall") == 0 ||
         std::strcmp(arg, "--service-start") == 0 ||
         std::strcmp(arg, "--service-stop") == 0 ||
      std::strcmp(arg, "--service-sas") == 0 ||
         std::strcmp(arg, "--service-ping") == 0 ||
         std::strcmp(arg, "--service-status") == 0 ||
         std::strcmp(arg, "--service-help") == 0;
}

int HandleServiceCliCommand(const std::string& command) {
  EnsureConsoleForCli();

  if (command == "--service-help") {
    PrintServiceCliUsage();
    return 0;
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
    std::cout << (success ? "uninstall ok" : "uninstall failed")
              << std::endl;
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

int main(int argc, char* argv[]) {
#ifdef _WIN32
  if (argc > 1 && IsServiceCliCommand(argv[1])) {
    return HandleServiceCliCommand(argv[1]);
  }
#endif

  // check if running as child process
  bool is_child = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--child") == 0) {
      is_child = true;
      break;
    }
  }

  if (is_child) {
    // child process: run render directly
    crossdesk::Render render;
    render.Run();
    return 0;
  }

  bool enable_daemon = false;
  auto path_manager = std::make_unique<crossdesk::PathManager>("CrossDesk");
  if (path_manager) {
    std::string cache_path = path_manager->GetCachePath().string();
    crossdesk::ConfigCenter config_center(cache_path + "/config.ini");
    enable_daemon = config_center.IsEnableDaemon();
  }

  if (enable_daemon) {
    // start daemon with restart monitoring
    Daemon daemon("CrossDesk");

    // define main loop function: run render and stop daemon on normal exit
    Daemon::MainLoopFunc main_loop = [&daemon]() {
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
  render.Run();
  return 0;
}