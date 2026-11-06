#include "desktop_automation.h"

#include <Windows.h>
#include <UIAutomation.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <deque>
#include <memory>
#include <stdexcept>

namespace crossdesk {
namespace {
using nlohmann::json;
using Microsoft::WRL::ComPtr;
using Bstr = std::unique_ptr<OLECHAR, decltype(&SysFreeString)>;
std::string Utf8(BSTR value) {
  if (!value) return {};
  const int length = std::min<int>(int(SysStringLen(value)), 256);
  const int size = WideCharToMultiByte(CP_UTF8, 0, value, length, nullptr, 0, nullptr, nullptr);
  std::string text(size > 0 ? size : 0, '\0');
  if (size) WideCharToMultiByte(CP_UTF8, 0, value, length, text.data(), size, nullptr, nullptr);
  SysFreeString(value);
  return DesktopText(std::move(text));
}
std::wstring Wide(const std::string& value) {
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
  if (!size && !value.empty()) throw std::invalid_argument("INVALID_ARGUMENT");
  std::wstring text(size, L'\0');
  if (size) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), text.data(), size);
  return text;
}
ComPtr<IUIAutomation> Automation() {
  struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
  };
  static thread_local Apartment apartment;
  ComPtr<IUIAutomation> automation;
  if (FAILED(CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) &&
      FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))))
    throw std::runtime_error("UI_AUTOMATION_UNAVAILABLE");
  ComPtr<IUIAutomation2> timed;
  if (SUCCEEDED(automation.As(&timed))) {
    timed->put_ConnectionTimeout(200);
    timed->put_TransactionTimeout(200);
  }
  return automation;
}
json Perform(ComPtr<IUIAutomationElement> element, const std::string& operation, const std::string& value) {
  BOOL secure = TRUE;
  if (FAILED(element->get_CurrentIsPassword(&secure))) throw std::runtime_error("STALE_ELEMENT");
  if (secure) throw std::runtime_error("SECURE_ELEMENT");
  HRESULT result = E_NOTIMPL;
  if (operation == "focus" || operation == "activate") result = element->SetFocus();
  else if (operation == "press") {
    ComPtr<IUIAutomationInvokePattern> invoke;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke)))) result = invoke->Invoke();
    else {
      ComPtr<IUIAutomationSelectionItemPattern> select;
      if (SUCCEEDED(element->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&select)))) result = select->Select();
      else {
        ComPtr<IUIAutomationTogglePattern> toggle;
        if (SUCCEEDED(element->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggle)))) result = toggle->Toggle();
      }
    }
  } else if (operation == "set_value") {
    ComPtr<IUIAutomationValuePattern> pattern;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&pattern)))) {
      const auto text = Wide(value);
      Bstr automation_value(
          SysAllocStringLen(text.data(), static_cast<UINT>(text.size())),
          SysFreeString);
      if (!automation_value) throw std::runtime_error("UI_ACTION_FAILED");
      result = pattern->SetValue(automation_value.get());
    }
  }
  if (FAILED(result)) throw std::runtime_error(result == UIA_E_ELEMENTNOTAVAILABLE ? "STALE_ELEMENT" : "UI_ACTION_FAILED");
  return {{"accepted", true}};
}
DesktopSnapshot Tree(ComPtr<IUIAutomationElement> root, int limit, int depth) {
  auto automation = Automation();
  ComPtr<IUIAutomationTreeWalker> walker;
  if (FAILED(automation->get_ControlViewWalker(&walker))) throw std::runtime_error("UI_AUTOMATION_UNAVAILABLE");
  struct Entry { ComPtr<IUIAutomationElement> element; int depth, parent; };
  std::deque<Entry> queue{{root, 0, -1}};
  DesktopSnapshot result;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
  while (!queue.empty() && int(result.items.size()) < limit && std::chrono::steady_clock::now() < deadline) {
    auto entry = std::move(queue.front()); queue.pop_front();
    auto element = entry.element;
    BOOL secure = TRUE, enabled = FALSE;
    if (FAILED(element->get_CurrentIsPassword(&secure))) continue;
    element->get_CurrentIsEnabled(&enabled);
    BSTR name = nullptr, role = nullptr;
    element->get_CurrentName(&name);
    element->get_CurrentLocalizedControlType(&role);
    RECT bounds{};
    element->get_CurrentBoundingRectangle(&bounds);
    if (secure) { SysFreeString(name); name = nullptr; }
    json data{{"name", Utf8(name)}, {"role", Utf8(role)},
      {"secure", bool(secure)}, {"enabled", bool(enabled)}, {"parent", entry.parent},
      {"bounds", {{"x", bounds.left}, {"y", bounds.top}, {"width", bounds.right - bounds.left}, {"height", bounds.bottom - bounds.top}}}};
    if (!secure) {
      ComPtr<IUIAutomationValuePattern> pattern;
      if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&pattern)))) {
        BSTR value = nullptr;
        if (SUCCEEDED(pattern->get_CurrentValue(&value))) data["value"] = Utf8(value);
      }
    }
    const int parent = int(result.items.size());
    result.items.push_back({data,
      [element](const auto& operation, const auto& value) { return Perform(element, operation, value); },
      [element](int max, int levels) { return Tree(element, max, levels); }});
    ComPtr<IUIAutomationElement> child;
    walker->GetFirstChildElement(element.Get(), &child);
    if (entry.depth >= depth) { result.truncated |= bool(child); continue; }
    while (child && int(queue.size() + result.items.size()) < limit && std::chrono::steady_clock::now() < deadline) {
      queue.push_back({child, entry.depth + 1, parent});
      ComPtr<IUIAutomationElement> next;
      walker->GetNextSiblingElement(child.Get(), &next);
      child = std::move(next);
    }
    result.truncated |= bool(child);
  }
  result.truncated |= !queue.empty();
  return result;
}
DesktopSnapshot WindowTree(HWND window, int limit, int depth) {
  ComPtr<IUIAutomationElement> root;
  if (!IsWindow(window) || FAILED(Automation()->ElementFromHandle(window, &root)) || !root)
    throw std::runtime_error("STALE_ELEMENT");
  return Tree(root, limit, depth);
}
} // namespace
nlohmann::json DesktopCapabilities() {
  return {{"available", true}, {"backend", "windows-uia"}, {"windows", true}, {"tree", true}, {"launch", true}, {"open_url", true}};
}
DesktopSnapshot DesktopWindows() {
  DesktopSnapshot result;
  EnumWindows([](HWND window, LPARAM context) -> BOOL {
    auto& result = *reinterpret_cast<DesktopSnapshot*>(context);
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
    wchar_t title[257]{};
    GetWindowTextW(window, title, 257);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    const auto valid = [window, pid] {
      DWORD current = 0;
      GetWindowThreadProcessId(window, &current);
      if (!IsWindow(window) || current != pid) throw std::runtime_error("STALE_ELEMENT");
    };
    RECT bounds{}; GetWindowRect(window, &bounds);
    result.items.push_back({{{"name", Utf8(SysAllocString(title))}, {"role", "window"}, {"pid", pid},
        {"foreground", window == GetForegroundWindow()},
        {"bounds", {{"x", bounds.left}, {"y", bounds.top}, {"width", bounds.right - bounds.left}, {"height", bounds.bottom - bounds.top}}}},
      [window, valid](const std::string& operation, const std::string&) -> json {
        valid();
        if (operation != "activate" && operation != "focus") throw std::runtime_error("UNSUPPORTED_OPERATION");
        if (IsIconic(window)) ShowWindowAsync(window, SW_RESTORE);
        if (!SetForegroundWindow(window)) throw std::runtime_error("UI_ACTION_FAILED");
        return {{"accepted", true}};
      }, [window, valid](int limit, int depth) { valid(); return WindowTree(window, limit, depth); }});
    result.truncated = result.items.size() >= 128;
    return result.truncated ? FALSE : TRUE;
  }, reinterpret_cast<LPARAM>(&result));
  return result;
}
DesktopSnapshot DesktopTree(int pid, int limit, int depth) {
  HWND window = GetForegroundWindow();
  if (pid) {
    struct Search { DWORD pid; HWND window = nullptr; } search{DWORD(pid)};
    EnumWindows([](HWND candidate, LPARAM context) -> BOOL {
      auto& search = *reinterpret_cast<Search*>(context);
      DWORD current = 0; GetWindowThreadProcessId(candidate, &current);
      if (current == search.pid && IsWindowVisible(candidate) && !GetWindow(candidate, GW_OWNER)) {
        search.window = candidate; return FALSE;
      }
      return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    window = search.window;
  }
  return WindowTree(window, limit, depth);
}
nlohmann::json DesktopLaunch(const std::string& application) {
  const auto path = Wide(application);
  if (path.size() < 7 || path[1] != L':' || (path[2] != L'\\' && path[2] != L'/') ||
      _wcsicmp(path.c_str() + path.size() - 4, L".exe") != 0 || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    throw std::invalid_argument("APPLICATION_UNAVAILABLE");
  if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
    throw std::runtime_error("UI_ACTION_FAILED");
  return {{"accepted", true}};
}
nlohmann::json DesktopOpenUrl(const std::string& url) {
  if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", Wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
    throw std::runtime_error("UI_ACTION_FAILED");
  return {{"accepted", true}};
}
} // namespace crossdesk
