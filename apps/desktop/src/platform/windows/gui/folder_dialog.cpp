#include "folder_dialog.h"

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

}  // namespace

std::string ChooseWindowsFolder(HWND owner, const std::string& title,
                               const std::string& initial_folder) {
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
  if (FAILED(dialog->Show(owner))) return {};

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
