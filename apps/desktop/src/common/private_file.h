/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-01
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PRIVATE_FILE_H_
#define _PRIVATE_FILE_H_

#include <filesystem>
#include <string>

namespace crossdesk::private_file {
// User-only files, atomic replacement, and rejection of shared/linked stores.
void EnsureDirectory(const std::filesystem::path& directory,
                     bool protect_existing = false);
std::string Read(const std::filesystem::path& path);
void Write(const std::filesystem::path& path, const std::string& text);
}

#endif
