#include <Windows.h>

#include <iostream>
#include <string>
#include <thread>

#include "service_lifetime.h"

int wmain(int argc, wchar_t** argv) {
  if (argc == 4 && std::wstring(argv[1]) == L"--child") {
    crossdesk::ServiceLifetimeWatcher watcher;
    if (!watcher.Start(argv[2])) return 2;
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[3]);
    if (!ready) return 3;
    SetEvent(ready);
    CloseHandle(ready);
    Sleep(INFINITE);  // The real watcher must terminate this child.
    return 4;
  }

  wchar_t executable[32768]{};
  if (!GetModuleFileNameW(nullptr, executable, 32768)) return 1;
  for (bool abandon : {false, true}) {
    const auto suffix = std::to_wstring(GetCurrentProcessId()) +
                        (abandon ? L"-abandon" : L"-release");
    const auto name = L"Global\\CrossDeskLifetimeTest-" + suffix;
    const auto ready_name = name + L"-ready";
    HANDLE owned = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE exit_owner = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str());
    if (!owned || !exit_owner || !ready) return 1;
    HANDLE lifetime = nullptr;
    std::thread owner([&] {
      lifetime = CreateMutexW(nullptr, TRUE, name.c_str());
      SetEvent(owned);
      WaitForSingleObject(exit_owner, INFINITE);
      if (!abandon && lifetime) ReleaseMutex(lifetime);
    });
    WaitForSingleObject(owned, INFINITE);
    bool ok = lifetime != nullptr;
    if (ok) {
      // Cancellation during an ordinary helper exit must not wait for or
      // release the service's ownership.
      crossdesk::ServiceLifetimeWatcher cancellation;
      ok = cancellation.Start(name);
    }
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --child \"" +
                           name + L"\" \"" + ready_name + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (ok)
      ok = CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                          &child) != FALSE;
    if (ok)
      ok = WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0 &&
           WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT;
    SetEvent(exit_owner);
    owner.join();
    if (ok) ok = WaitForSingleObject(child.hProcess, 5000) == WAIT_OBJECT_0;
    DWORD exit_code = 0;
    if (ok)
      ok = GetExitCodeProcess(child.hProcess, &exit_code) &&
           exit_code == ERROR_BROKEN_PIPE;
    if (child.hProcess) {
      if (!ok) TerminateProcess(child.hProcess, 1);
      WaitForSingleObject(child.hProcess, 5000);
      CloseHandle(child.hProcess);
      CloseHandle(child.hThread);
    }
    if (lifetime) CloseHandle(lifetime);
    CloseHandle(owned);
    CloseHandle(exit_owner);
    CloseHandle(ready);
    if (!ok) {
      std::cerr << "Service lifetime test failed, abandoned=" << abandon
                << '\n';
      return 1;
    }
  }
  std::cout
      << "Service release, crash and watcher cancellation checks passed\n";
}
