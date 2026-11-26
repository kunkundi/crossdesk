#ifndef CROSSDESK_WINDOWS_FOLDER_DIALOG_H_
#define CROSSDESK_WINDOWS_FOLDER_DIALOG_H_

#include <windows.h>

#include <string>

namespace crossdesk {

// Run the native modal dialog on a dedicated thread.
std::string ChooseWindowsFolder(HWND owner, const std::string& title,
                               const std::string& initial_folder);

}  // namespace crossdesk

#endif  // CROSSDESK_WINDOWS_FOLDER_DIALOG_H_
