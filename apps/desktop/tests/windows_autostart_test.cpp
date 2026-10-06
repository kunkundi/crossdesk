// Run from an elevated Windows terminal (the Windows CI runner is elevated).
// Uses a unique registration name and removes it before returning.
#include <windows.h>
#include <sddl.h>
#include <taskschd.h>
#include <wrl/client.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "platform/autostart_backend.h"
#include "platform/windows/autostart_task.h"
#include "rd_log.h"

namespace {
using Microsoft::WRL::ComPtr;
namespace platform = crossdesk::platform;
constexpr wchar_t kRunKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool Expect(bool condition, const char* message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

std::wstring SidString(PSID sid) {
  LPWSTR text = nullptr;
  if (!IsValidSid(sid) || !ConvertSidToStringSidW(sid, &text)) return {};
  const std::wstring result(text);
  LocalFree(text);
  return result;
}

std::wstring ResolveUserSid(const wchar_t* user) {
  if (!user || !*user) return {};
  PSID sid = nullptr;
  if (ConvertStringSidToSidW(user, &sid)) {
    const auto result = SidString(sid);
    LocalFree(sid);
    return result;
  }

  // Task Scheduler may return DOMAIN\\user even when registered with a SID.
  // Compare the resolved identity, not the scheduler's display format.
  DWORD sid_size = 0, domain_size = 0;
  SID_NAME_USE use{};
  LookupAccountNameW(nullptr, user, nullptr, &sid_size, nullptr, &domain_size,
                     &use);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sid_size == 0) return {};
  std::vector<BYTE> buffer(sid_size);
  std::vector<wchar_t> domain(domain_size);
  if (!LookupAccountNameW(nullptr, user, buffer.data(), &sid_size,
                          domain.data(), &domain_size, &use))
    return {};
  return SidString(buffer.data());
}

bool ExpectUserSid(const wchar_t* user, const std::wstring& expected,
                   const char* message) {
  const auto actual = ResolveUserSid(user);
  if (!expected.empty() && actual == expected) return true;
  std::cerr << "FAIL: " << message << '\n';
  std::wcerr << L"  expected SID: " << expected << L", returned UserId: "
             << (user ? user : L"<null>") << L", resolved SID: " << actual
             << L'\n';
  return false;
}

std::wstring UserSid() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<BYTE> data(size);
  const BOOL read =
      GetTokenInformation(token, TokenUser, data.data(), size, &size);
  CloseHandle(token);
  if (!read) return {};
  return SidString(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid);
}

bool CheckUserIdentity(const std::wstring& sid) {
  bool ok = Expect(ResolveUserSid(sid.c_str()) == sid,
                   "SID-form user identity is preserved");
  ok &= Expect(ResolveUserSid(nullptr).empty() && ResolveUserSid(L"").empty(),
               "empty UserId must not match a user");
  const auto other = sid == L"S-1-5-18" ? L"S-1-5-19" : L"S-1-5-18";
  ok &= Expect(ResolveUserSid(other) != sid,
               "another account must not match the current user");

  PSID binary_sid = nullptr;
  if (!ConvertStringSidToSidW(sid.c_str(), &binary_sid)) return false;
  DWORD name_size = 0, domain_size = 0;
  SID_NAME_USE use{};
  LookupAccountSidW(nullptr, binary_sid, nullptr, &name_size, nullptr,
                    &domain_size, &use);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || name_size == 0) {
    LocalFree(binary_sid);
    return Expect(false, "look up current user account name");
  }
  std::vector<wchar_t> name(name_size), domain(domain_size);
  const BOOL found = LookupAccountSidW(nullptr, binary_sid, name.data(),
                                       &name_size, domain.data(), &domain_size,
                                       &use);
  LocalFree(binary_sid);
  if (!Expect(found != FALSE, "read current user account name")) return false;
  const auto account = domain_size ? std::wstring(domain.data()) + L"\\" +
                                        name.data()
                                  : std::wstring(name.data());
  ok &= ExpectUserSid(account.c_str(), sid,
                      "account-name and SID forms identify the same user");
  return ok;
}

bool SetRun(const std::wstring& name, const std::wstring& path) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE,
                      nullptr, &key, nullptr) != ERROR_SUCCESS)
    return false;
  const std::wstring command = L"\"" + path + L"\"";
  const LONG result = RegSetValueExW(
      key, name.c_str(), 0, REG_SZ,
      reinterpret_cast<const BYTE*>(command.c_str()),
      static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
  return result == ERROR_SUCCESS;
}

std::wstring ReadRun(const std::wstring& name) {
  wchar_t value[32768] = {};
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, name.c_str(), RRF_RT_REG_SZ,
                   nullptr, value, &size) != ERROR_SUCCESS)
    return {};
  return value;
}

bool CheckTask(ITaskFolder* folder, BSTR task_name,
               const std::filesystem::path& executable,
               const std::wstring& sid) {
  ComPtr<IRegisteredTask> task;
  if (!Expect(SUCCEEDED(folder->GetTask(task_name, task.GetAddressOf())),
              "task exists"))
    return false;
  ComPtr<ITaskDefinition> definition;
  if (FAILED(task->get_Definition(definition.GetAddressOf()))) return false;
  ComPtr<IPrincipal> principal;
  if (FAILED(definition->get_Principal(principal.GetAddressOf()))) return false;
  TASK_RUNLEVEL_TYPE level = TASK_RUNLEVEL_LUA;
  TASK_LOGON_TYPE logon = TASK_LOGON_NONE;
  bool ok = Expect(SUCCEEDED(principal->get_RunLevel(&level)) &&
                       level == TASK_RUNLEVEL_HIGHEST,
                   "task requests elevation");
  ok &= Expect(SUCCEEDED(principal->get_LogonType(&logon)) &&
                   logon == TASK_LOGON_INTERACTIVE_TOKEN,
               "task uses interactive user");
  BSTR user = nullptr;
  ok &= Expect(SUCCEEDED(principal->get_UserId(&user)), "read task user");
  ok &= ExpectUserSid(user, sid, "task belongs to current user SID");
  SysFreeString(user);

  ComPtr<ITriggerCollection> triggers;
  ComPtr<ITrigger> trigger;
  ComPtr<ILogonTrigger> logon_trigger;
  if (FAILED(definition->get_Triggers(triggers.GetAddressOf())) ||
      FAILED(triggers->get_Item(1, trigger.GetAddressOf())) ||
      FAILED(trigger.As(&logon_trigger)))
    return false;
  user = nullptr;
  ok &= Expect(SUCCEEDED(logon_trigger->get_UserId(&user)),
               "read logon trigger user");
  ok &= ExpectUserSid(user, sid,
                      "logon trigger is restricted to the same user");
  SysFreeString(user);

  ComPtr<ITaskSettings> settings;
  if (FAILED(definition->get_Settings(settings.GetAddressOf()))) return false;
  VARIANT_BOOL value = VARIANT_TRUE;
  ok &= Expect(SUCCEEDED(settings->get_DisallowStartIfOnBatteries(&value)) &&
                   value == VARIANT_FALSE,
               "starts on battery");
  ok &= Expect(SUCCEEDED(settings->get_StopIfGoingOnBatteries(&value)) &&
                   value == VARIANT_FALSE,
               "survives switching to battery");
  BSTR limit = nullptr;
  ok &= Expect(SUCCEEDED(settings->get_ExecutionTimeLimit(&limit)) && limit &&
                   std::wstring(limit) == L"PT0S",
               "has no three-day time limit");
  SysFreeString(limit);

  ComPtr<IActionCollection> actions;
  ComPtr<IAction> action;
  ComPtr<IExecAction> exec;
  if (FAILED(definition->get_Actions(actions.GetAddressOf())) ||
      FAILED(actions->get_Item(1, action.GetAddressOf())) ||
      FAILED(action.As(&exec)))
    return false;
  BSTR path = nullptr;
  ok &= Expect(
      SUCCEEDED(exec->get_Path(&path)) && path && executable.wstring() == path,
      "Unicode command round-trips");
  SysFreeString(path);
  path = nullptr;
  ok &= Expect(SUCCEEDED(exec->get_WorkingDirectory(&path)) && path &&
                   executable.parent_path().wstring() == path,
               "working directory is executable directory");
  SysFreeString(path);
  return ok;
}

int RunTests(ITaskFolder* folder) {
  const std::string app =
      "CrossDeskAutostartTest-" + std::to_string(GetCurrentProcessId());
  const std::wstring name(app.begin(), app.end());
  const auto sid = UserSid();
  if (sid.empty() || !CheckUserIdentity(sid)) return 1;
  const auto task_name = platform::AutostartTaskName(name, sid);
  BSTR task_bstr = SysAllocString(task_name.c_str());
  if (!task_bstr) return 1;
  const auto executable =
      std::filesystem::u8path(platform::GetAutostartExecutablePath());
  const auto temporary = std::filesystem::temp_directory_path() /
                         (name + L" \u81ea\u542f & spaces");
  std::filesystem::create_directories(temporary);
  const auto alternate = temporary / L"CrossDesk \u6d4b\u8bd5.exe";
  // Registration never executes this file. Exercise XML/path handling without
  // leaving another runnable test program in the login path.
  std::ofstream(alternate) << "autostart registration test";
  struct Cleanup {
    ITaskFolder* folder;
    BSTR task;
    std::wstring name;
    std::filesystem::path directory;
    ~Cleanup() {
      folder->DeleteTask(task, 0);
      SysFreeString(task);
      RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, name.c_str());
      std::error_code error;
      std::filesystem::remove_all(directory, error);
    }
  } cleanup{folder, task_bstr, name, temporary};

  bool ok = Expect(platform::DisableAutostart(app),
                   "disable absent entry is idempotent");
  ok &= Expect(!platform::IsAutostartEnabled(app), "initially disabled");
  ok &= Expect(platform::MigrateLegacyAutostart(app),
               "migration without a legacy entry is a no-op");
  ok &= Expect(!platform::IsAutostartEnabled(app),
               "migration never opts user in");
  ok &=
      Expect(SetRun(name, alternate.wstring()), "seed another copy's startup");
  ok &= Expect(platform::MigrateLegacyAutostart(app),
               "ignore another copy during migration");
  ok &= Expect(ReadRun(name) == L"\"" + alternate.wstring() + L"\"",
               "another copy's registration is unchanged");
  ok &= Expect(SetRun(name, executable.wstring()), "seed legacy Run entry");
  ok &= Expect(
      !platform::EnableAutostart(app, (temporary / L"missing.exe").u8string()),
      "reject missing executable");
  ok &= Expect(!ReadRun(name).empty(),
               "failed registration preserves legacy entry");
  ok &= Expect(platform::MigrateLegacyAutostart(app), "migrate legacy entry");
  ok &= Expect(platform::IsAutostartEnabled(app), "enabled after migration");

  if (platform::kAutostartRequiresElevation) {
    ok &= Expect(ReadRun(name).empty(),
                 "remove legacy entry after successful migration");
    ok &= CheckTask(folder, task_bstr, executable, sid);
  } else {
    ComPtr<IRegisteredTask> absent;
    ok &= Expect(FAILED(folder->GetTask(task_bstr, absent.GetAddressOf())),
                 "portable/debug does not create elevated task");
    ok &= Expect(!ReadRun(name).empty(),
                 "portable/debug retains ordinary startup");
  }
  ok &= Expect(platform::EnableAutostart(app, alternate.u8string()),
               "update executable path");
  ok &= Expect(platform::EnableAutostart(app, alternate.u8string()),
               "enable is idempotent");
  if (platform::kAutostartRequiresElevation) {
    ok &= CheckTask(folder, task_bstr, alternate, sid);
    ComPtr<IRegisteredTask> task;
    if (SUCCEEDED(folder->GetTask(task_bstr, task.GetAddressOf()))) {
      ok &= Expect(SUCCEEDED(task->put_Enabled(VARIANT_FALSE)),
                   "disable task externally");
      ok &= Expect(!platform::IsAutostartEnabled(app),
                   "disabled task is not reported enabled");
      ok &= Expect(platform::EnableAutostart(app, alternate.u8string()),
                   "reenable task");
    } else {
      ok = false;
    }
  } else {
    ok &= Expect(ReadRun(name) == L"\"" + alternate.wstring() + L"\"",
                 "Run command quotes Unicode path with spaces");
  }
  ok &= Expect(platform::RemoveInstalledAutostart(app),
               "uninstall skips another executable");
  ok &= Expect(platform::IsAutostartEnabled(app),
               "other executable keeps its startup");
  ok &= Expect(platform::EnableAutostart(app, executable.u8string()),
               "restore own executable");
  ok &= Expect(platform::RemoveInstalledAutostart(app),
               "uninstall removes own registration");
  ok &= Expect(!platform::IsAutostartEnabled(app),
               "uninstalled executable is no longer scheduled");
  ok &= Expect(platform::EnableAutostart(app, executable.u8string()),
               "reenable before disable test");
  // Disabling must clean up a leftover legacy entry as well as the task.
  ok &=
      Expect(SetRun(name, executable.wstring()), "seed leftover legacy entry");
  ok &=
      Expect(platform::DisableAutostart(app), "disable existing registration");
  ok &= Expect(ReadRun(name).empty() && !platform::IsAutostartEnabled(app),
               "all active registrations removed");
  ok &= Expect(platform::DisableAutostart(app), "disable twice succeeds");
  return ok ? 0 : 1;
}
}  // namespace

int main() {
  crossdesk::InitLogger(std::filesystem::temp_directory_path().string(),
                        "crossdesk-autostart-test");
  // Exercise the existing-STA case used by GUI settings as well as normal COM
  // cleanup: the backend must not uninitialize this caller-owned apartment.
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
  int result = 1;
  {
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    const VARIANT empty{};
    BSTR root = SysAllocString(L"\\");
    if (root &&
        SUCCEEDED(CoCreateInstance(CLSID_TaskScheduler, nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(service.GetAddressOf()))) &&
        SUCCEEDED(service->Connect(empty, empty, empty, empty)) &&
        SUCCEEDED(service->GetFolder(root, folder.GetAddressOf()))) {
      result = RunTests(folder.Get());
    }
    SysFreeString(root);
  }
  CoUninitialize();
  if (result == 0) std::cout << "Windows autostart integration tests passed\n";
  return result;
}
