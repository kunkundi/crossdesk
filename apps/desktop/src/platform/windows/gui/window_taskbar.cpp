#include "window_taskbar.h"

#include <commctrl.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <wrl/client.h>

namespace crossdesk {
namespace {

constexpr wchar_t kStreamAppId[] = L"CrossDesk.RemoteControl";
constexpr UINT_PTR kTaskbarSubclassId = 1;

void ClearTaskbarProperties(IPropertyStore* properties) {
  const PROPVARIANT empty{};
  properties->SetValue(PKEY_AppUserModel_ID, empty);
  properties->SetValue(PKEY_AppUserModel_PreventPinning, empty);
}

LRESULT CALLBACK TaskbarSubclassProc(HWND window, UINT message, WPARAM wparam,
                                     LPARAM lparam, UINT_PTR subclass_id,
                                     DWORD_PTR reference_data) {
  auto* properties = reinterpret_cast<IPropertyStore*>(reference_data);
  if (message == WM_DESTROY) {
    // Window property stores require explicit clearing before destruction.
    ClearTaskbarProperties(properties);
  }
  if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(window, TaskbarSubclassProc, subclass_id);
    properties->Release();
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

}  // namespace

bool ConfigureWindowsStreamTaskbar(HWND window) {
  if (!window || !IsWindow(window)) return false;
  DWORD_PTR existing = 0;
  if (GetWindowSubclass(window, TaskbarSubclassProc, kTaskbarSubclassId,
                        &existing)) {
    return true;
  }

  Microsoft::WRL::ComPtr<IPropertyStore> properties;
  if (FAILED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&properties)))) {
    return false;
  }
  if (!SetWindowSubclass(window, TaskbarSubclassProc, kTaskbarSubclassId,
                         reinterpret_cast<DWORD_PTR>(properties.Get()))) {
    return false;
  }

  // A live session cannot be recreated by launching the executable. Keep
  // pinning on the main application's button instead of creating a shortcut
  // that claims to reopen a remote session but only opens the connection page.
  PROPVARIANT prevent_pinning{};
  InitPropVariantFromBoolean(TRUE, &prevent_pinning);
  HRESULT result = properties->SetValue(PKEY_AppUserModel_PreventPinning,
                                        prevent_pinning);
  PROPVARIANT app_id{};
  if (SUCCEEDED(result)) result = InitPropVariantFromString(kStreamAppId, &app_id);
  if (SUCCEEDED(result)) result = properties->SetValue(PKEY_AppUserModel_ID, app_id);
  PropVariantClear(&app_id);
  if (FAILED(result)) {
    ClearTaskbarProperties(properties.Get());
    RemoveWindowSubclass(window, TaskbarSubclassProc, kTaskbarSubclassId);
    return false;
  }
  // SetValue applies immediately; this store does not require Commit(). The
  // subclass retains the store until this HWND is destroyed, even if Slint
  // later creates a new native window for the same component.
  properties.Detach();
  return true;
}

}  // namespace crossdesk
