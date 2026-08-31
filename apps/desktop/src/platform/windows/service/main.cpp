#include <Windows.h>

#include <iostream>
#include <string>

#include "service_host.h"

namespace {

std::wstring GetExecutablePath() {
  wchar_t path[MAX_PATH] = {0};
  DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return L"";
  }
  return std::wstring(path, length);
}

void PrintUsage() {
  std::cout
      << "CrossDesk Windows service skeleton\n"
      << "  --service    Run under the Windows Service Control Manager\n"
      << "  --console    Run the service loop in console mode\n"
      << "  --install    Install the service for the current executable\n"
      << "  --uninstall  Remove the installed service\n"
      << "  --start      Start the installed service\n"
      << "  --stop       Stop the installed service\n"
      << "  --sas        Ask the service to send Secure Attention Sequence\n"
      << "  --ping       Ping the running service over named pipe IPC\n"
      << "  --status     Query runtime status over named pipe IPC\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  crossdesk::CrossDeskServiceHost host;

  if (argc <= 1) {
    PrintUsage();
    return 0;
  }

  std::string command = argv[1];
  if (command == "--service") {
    return host.RunAsService();
  }
  if (command == "--console") {
    return host.RunInConsole();
  }
  if (command == "--install") {
    std::wstring executable_path = GetExecutablePath();
    bool success = !executable_path.empty() &&
                   crossdesk::InstallCrossDeskService(executable_path);
    std::cout << (success ? "install ok" : "install failed") << std::endl;
    return success ? 0 : 1;
  }
  if (command == "--uninstall") {
    bool success = crossdesk::UninstallCrossDeskService();
    std::cout << (success ? "uninstall ok" : "uninstall failed") << std::endl;
    return success ? 0 : 1;
  }
  if (command == "--start") {
    bool success = crossdesk::StartCrossDeskService();
    std::cout << (success ? "start ok" : "start failed") << std::endl;
    return success ? 0 : 1;
  }
  if (command == "--stop") {
    bool success = crossdesk::StopCrossDeskService();
    std::cout << (success ? "stop ok" : "stop failed") << std::endl;
    return success ? 0 : 1;
  }
  if (command == "--sas") {
    std::cout << crossdesk::QueryCrossDeskService("sas") << std::endl;
    return 0;
  }
  if (command == "--ping") {
    std::cout << crossdesk::QueryCrossDeskService("ping") << std::endl;
    return 0;
  }
  if (command == "--status") {
    std::cout << crossdesk::QueryCrossDeskService("status") << std::endl;
    return 0;
  }

  PrintUsage();
  return 1;
}