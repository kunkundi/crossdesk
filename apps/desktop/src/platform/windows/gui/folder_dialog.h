/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-26
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _FOLDER_DIALOG_H_
#define _FOLDER_DIALOG_H_

#include <windows.h>

#include <stop_token>
#include <string>

namespace crossdesk {

// Run the native modal dialog on a dedicated STA thread.
std::string ChooseWindowsFolder(HWND owner, const std::string& title,
                               const std::string& initial_folder,
                               std::stop_token stop);

}  // namespace crossdesk

#endif
