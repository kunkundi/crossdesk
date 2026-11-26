#include "folder_dialog.h"

#include <commctrl.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <memory>

#include "filesystem_utf8.h"

namespace crossdesk {
namespace {

std::wstring DialogTitle(const std::string& title) {
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       title.c_str(), -1, nullptr, 0);
  if (size <= 1) return {};
  std::wstring wide_title(size, L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title.c_str(), -1,
                           wide_title.data(), size))
    return {};
  wide_title.pop_back();
  return wide_title;
}

constexpr HRESULT kCancelled = HRESULT_FROM_WIN32(ERROR_CANCELLED);

LRESULT CALLBACK CancelFolderDialog(HWND window, UINT message, WPARAM wparam,
                                    LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
  if (message == WM_CLOSE || (message == WM_TIMER && wparam == id)) {
    // Show can pump messages before the shell finishes creating the dialog.
    // Retry on this STA until Show returns and the notification window is removed.
    if (message == WM_CLOSE) SetTimer(window, id, 50, nullptr);
    reinterpret_cast<IFileDialog*>(data)->Close(kCancelled);
    return 0;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

HRESULT ShowFolderDialog(IFileDialog* dialog, HWND owner, std::stop_token stop) {
  if (stop.stop_requested()) return kCancelled;
  const HWND window = CreateWindowExW(0, L"STATIC", nullptr, 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, nullptr, nullptr);
  if (!window) return E_FAIL;
  HRESULT result = E_FAIL;
  if (SetWindowSubclass(window, CancelFolderDialog, 1,
                        reinterpret_cast<DWORD_PTR>(dialog))) {
    // Only the worker apartment touches the COM interface. The UI thread posts
    // cancellation even if the shell is currently inside its modal message loop.
    std::stop_callback on_stop(stop, [window] {
      PostMessageW(window, WM_CLOSE, 0, 0);
    });
    result = stop.stop_requested() ? kCancelled : dialog->Show(owner);
    RemoveWindowSubclass(window, CancelFolderDialog, 1);
  }
  DestroyWindow(window);
  return result;
}

}  // namespace

std::string ChooseWindowsFolder(HWND owner, const std::string& title,
                               const std::string& initial_folder,
                               std::stop_token stop) {
  if (stop.stop_requested()) return {};
  struct Apartment {
    const HRESULT result =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ~Apartment() {
      if (SUCCEEDED(result)) CoUninitialize();
    }
  } apartment;
  if (FAILED(apartment.result)) return {};

  Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    return {};
  FILEOPENDIALOGOPTIONS options{};
  if (FAILED(dialog->GetOptions(&options)) ||
      FAILED(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                               FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR)))
    return {};

  const auto wide_title = DialogTitle(title);
  if (!wide_title.empty()) dialog->SetTitle(wide_title.c_str());
  if (!initial_folder.empty()) {
    Microsoft::WRL::ComPtr<IShellItem> folder;
    const auto path = PathFromUtf8(initial_folder).wstring();
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr,
                                             IID_PPV_ARGS(&folder))))
      dialog->SetFolder(folder.Get());
  }
  if (FAILED(ShowFolderDialog(dialog.Get(), owner, stop))) return {};

  Microsoft::WRL::ComPtr<IShellItem> selection;
  PWSTR path = nullptr;
  if (FAILED(dialog->GetResult(&selection)) ||
      FAILED(selection->GetDisplayName(SIGDN_FILESYSPATH, &path)))
    return {};
  const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> selected_path(
      path, CoTaskMemFree);
  const auto utf8 = std::filesystem::path(selected_path.get()).u8string();
  return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

}  // namespace crossdesk
