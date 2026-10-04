#include "platform/autostart_backend.h"

#include <windows.h>
#include <sddl.h>
#include <taskschd.h>
#include <wrl/client.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "autostart_task.h"
#include "rd_log.h"

namespace crossdesk::platform {
namespace {

using Microsoft::WRL::ComPtr;
using Bstr = std::unique_ptr<OLECHAR, decltype(&SysFreeString)>;

constexpr wchar_t kRunKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

Bstr MakeBstr(const std::wstring& value) {
  return Bstr(SysAllocString(value.c_str()), SysFreeString);
}

std::wstring FromUtf8(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       value.c_str(), -1, nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(size, L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1,
                           result.data(), size))
    return {};
  result.pop_back();
  return result;
}

std::wstring CurrentUserSid() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<BYTE> data(size);
  const bool read =
      GetTokenInformation(token, TokenUser, data.data(), size, &size) != FALSE;
  CloseHandle(token);
  if (!read) return {};
  LPWSTR sid = nullptr;
  if (!ConvertSidToStringSidW(
          reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid))
    return {};
  const std::wstring result(sid);
  LocalFree(sid);
  return result;
}

bool Check(HRESULT result, const char* operation) {
  if (SUCCEEDED(result)) return true;
  LOG_ERROR("Windows autostart {} failed: HRESULT=0x{:08x}", operation,
            static_cast<unsigned long>(result));
  return false;
}

class ComApartment {
 public:
  ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~ComApartment() {
    if (SUCCEEDED(result_)) CoUninitialize();
  }
  bool ready() const {
    // Settings can run on a UI thread that already uses a different apartment.
    return result_ == RPC_E_CHANGED_MODE || Check(result_, "initialize COM");
  }

 private:
  HRESULT result_;
};

class TaskScheduler {
 private:
  // COM interfaces must be released before the apartment is uninitialized.
  ComApartment apartment_;
  ComPtr<ITaskService> service_;

 public:
  ComPtr<ITaskFolder> folder;

  bool Open() {
    if (!apartment_.ready()) return false;
    if (!Check(
            CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(service_.GetAddressOf())),
            "create scheduler"))
      return false;
    const VARIANT empty{};
    if (!Check(service_->Connect(empty, empty, empty, empty), "connect"))
      return false;
    auto root = MakeBstr(L"\\");
    return root && Check(service_->GetFolder(root.get(), folder.GetAddressOf()),
                         "open task folder");
  }
};

LONG ReadRunEntry(const std::wstring& app_name, std::wstring* command) {
  DWORD size = 0;
  LONG result = RegGetValueW(HKEY_CURRENT_USER, kRunKey, app_name.c_str(),
                             RRF_RT_REG_SZ, nullptr, nullptr, &size);
  if (result != ERROR_SUCCESS) return result;
  std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
  result = RegGetValueW(HKEY_CURRENT_USER, kRunKey, app_name.c_str(),
                        RRF_RT_REG_SZ, nullptr, buffer.data(), &size);
  if (result == ERROR_SUCCESS) *command = buffer.data();
  return result;
}

bool RemoveRunEntry(const std::wstring& app_name) {
  const LONG result =
      RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, app_name.c_str());
  if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND ||
      result == ERROR_PATH_NOT_FOUND)
    return true;
  LOG_ERROR("Windows autostart remove Run entry failed: error={}", result);
  return false;
}

bool EnableRunEntry(const std::wstring& app_name,
                    const std::wstring& executable) {
  HKEY key = nullptr;
  const LONG opened = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                                      KEY_SET_VALUE, nullptr, &key, nullptr);
  if (opened != ERROR_SUCCESS) {
    LOG_ERROR("Windows autostart open Run key failed: error={}", opened);
    return false;
  }
  const std::wstring command = L"\"" + executable + L"\"";
  const LONG result = RegSetValueExW(
      key, app_name.c_str(), 0, REG_SZ,
      reinterpret_cast<const BYTE*>(command.c_str()),
      static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
  if (result != ERROR_SUCCESS)
    LOG_ERROR("Windows autostart set Run entry failed: error={}", result);
  return result == ERROR_SUCCESS;
}

bool RegisterLogonTask(const std::wstring& app_name,
                       const std::filesystem::path& executable) {
  const auto sid = CurrentUserSid();
  if (sid.empty()) return false;
  TaskScheduler scheduler;
  if (!scheduler.Open()) return false;
  auto name = MakeBstr(AutostartTaskName(app_name, sid));
  auto xml = MakeBstr(BuildAutostartTaskXml(
      sid, executable.wstring(), executable.parent_path().wstring()));
  auto user = MakeBstr(sid);
  if (!name || !xml || !user) return false;
  VARIANT user_id{};
  user_id.vt = VT_BSTR;
  user_id.bstrVal = user.get();  // Borrowed for this synchronous call.
  const VARIANT empty{};
  ComPtr<IRegisteredTask> task;
  return Check(scheduler.folder->RegisterTask(
                   name.get(), xml.get(), TASK_CREATE_OR_UPDATE, user_id, empty,
                   TASK_LOGON_INTERACTIVE_TOKEN, empty, task.GetAddressOf()),
               "register elevated logon task");
}

bool RemoveLogonTask(const std::wstring& app_name) {
  const auto sid = CurrentUserSid();
  if (sid.empty()) return false;
  TaskScheduler scheduler;
  if (!scheduler.Open()) return false;
  auto name = MakeBstr(AutostartTaskName(app_name, sid));
  if (!name) return false;
  const HRESULT result = scheduler.folder->DeleteTask(name.get(), 0);
  return result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
         result == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) ||
         Check(result, "delete logon task");
}

}  // namespace

std::string GetAutostartExecutablePath() {
  std::vector<wchar_t> path(32768);
  const DWORD length =
      GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) return {};
  return std::filesystem::path(path.data(), path.data() + length).u8string();
}

bool EnableAutostart(const std::string& app_name,
                     const std::string& executable_path) {
  const auto name = FromUtf8(app_name);
  const auto path = FromUtf8(executable_path);
  if (name.empty() || path.empty()) return false;
  const std::filesystem::path executable(path);
  std::error_code error;
  if (!executable.is_absolute() ||
      !std::filesystem::is_regular_file(executable, error))
    return false;
  if (!kAutostartRequiresElevation) return EnableRunEntry(name, path);
  // Do not remove the old entry unless registering its replacement succeeded.
  if (!RegisterLogonTask(name, executable)) return false;
  RemoveRunEntry(name);
  return true;
}

bool DisableAutostart(const std::string& app_name) {
  const auto name = FromUtf8(app_name);
  if (name.empty()) return false;
  // Portable builds must not modify an installed build's elevated task.
  if (kAutostartRequiresElevation && !RemoveLogonTask(name)) return false;
  return RemoveRunEntry(name);
}

bool IsAutostartEnabled(const std::string& app_name) {
  const auto app = FromUtf8(app_name);
  if (app.empty()) return false;
  if (!kAutostartRequiresElevation) {
    std::wstring command;
    return ReadRunEntry(app, &command) == ERROR_SUCCESS;
  }
  const auto sid = CurrentUserSid();
  if (sid.empty()) return false;
  TaskScheduler scheduler;
  if (!scheduler.Open()) return false;
  auto name = MakeBstr(AutostartTaskName(app, sid));
  if (!name) return false;
  ComPtr<IRegisteredTask> task;
  if (FAILED(scheduler.folder->GetTask(name.get(), task.GetAddressOf())))
    return false;
  VARIANT_BOOL enabled = VARIANT_FALSE;
  return SUCCEEDED(task->get_Enabled(&enabled)) && enabled == VARIANT_TRUE;
}

bool MigrateLegacyAutostart(const std::string& app_name) {
  if (!kAutostartRequiresElevation) return true;
  std::wstring command;
  const auto name = FromUtf8(app_name);
  if (name.empty()) return false;
  const LONG result = ReadRunEntry(name, &command);
  if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
    return true;
  if (result != ERROR_SUCCESS) return false;
  if (command.size() >= 2 && command.front() == L'"' && command.back() == L'"')
    command = command.substr(1, command.size() - 2);
  const auto executable = GetAutostartExecutablePath();
  if (executable.empty()) return false;
  if (_wcsicmp(command.c_str(), FromUtf8(executable).c_str()) != 0) return true;
  return EnableAutostart(app_name, executable);
}

bool RemoveInstalledAutostart(const std::string& app_name) {
  const auto name = FromUtf8(app_name);
  const auto executable = FromUtf8(GetAutostartExecutablePath());
  if (name.empty() || executable.empty()) return false;
  TaskScheduler scheduler;
  if (!scheduler.Open()) return false;
  ComPtr<IRegisteredTaskCollection> tasks;
  if (!Check(scheduler.folder->GetTasks(TASK_ENUM_HIDDEN, tasks.GetAddressOf()),
             "enumerate logon tasks"))
    return false;
  LONG count = 0;
  if (!Check(tasks->get_Count(&count), "count logon tasks")) return false;
  const auto prefix = name + L" Autostart ";
  std::vector<std::wstring> remove;
  for (LONG index = 1; index <= count; ++index) {
    VARIANT item{};
    item.vt = VT_I4;
    item.lVal = index;
    ComPtr<IRegisteredTask> task;
    if (!Check(tasks->get_Item(item, task.GetAddressOf()), "read logon task"))
      return false;
    BSTR raw_name = nullptr;
    if (!Check(task->get_Name(&raw_name), "read task name")) return false;
    Bstr task_name(raw_name, SysFreeString);
    const std::wstring task_text(task_name.get() ? task_name.get() : L"");
    if (task_text.compare(0, prefix.size(), prefix) != 0) continue;
    ComPtr<ITaskDefinition> definition;
    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    LONG action_count = 0;
    if (!Check(task->get_Definition(definition.GetAddressOf()),
               "read task definition") ||
        !Check(definition->get_Actions(actions.GetAddressOf()),
               "read task actions") ||
        !Check(actions->get_Count(&action_count), "count task actions"))
      return false;
    if (action_count != 1) continue;
    if (!Check(actions->get_Item(1, action.GetAddressOf()), "read task action"))
      return false;
    if (FAILED(action.As(&exec))) continue;
    BSTR raw_path = nullptr;
    if (!Check(exec->get_Path(&raw_path), "read task executable")) return false;
    Bstr path(raw_path, SysFreeString);
    if (path && _wcsicmp(path.get(), executable.c_str()) == 0)
      remove.push_back(task_text);
  }
  // Collect names first; deleting during enumeration can change its indices.
  for (const auto& task : remove) {
    auto task_name = MakeBstr(task);
    if (!task_name || !Check(scheduler.folder->DeleteTask(task_name.get(), 0),
                             "remove installed logon task"))
      return false;
  }
  std::wstring command;
  const LONG result = ReadRunEntry(name, &command);
  if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
    return true;
  if (result != ERROR_SUCCESS) return false;
  if (command.size() >= 2 && command.front() == L'"' && command.back() == L'"')
    command = command.substr(1, command.size() - 2);
  return _wcsicmp(command.c_str(), executable.c_str()) != 0 ||
         RemoveRunEntry(name);
}

}  // namespace crossdesk::platform
