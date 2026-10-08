/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-08
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _LINUX_UPDATE_DOWNLOAD_H_
#define _LINUX_UPDATE_DOWNLOAD_H_

#include <filesystem>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "app_version.h"

namespace crossdesk {

// Uses package-name beside the executable, so source builds cannot inherit an
// unrelated system installation's variant. Missing/invalid downloads open the
// release list rather than silently offering a different package or old release.
std::string LinuxUpdateDownloadUrl(const nlohmann::json& manifest,
                                   const VersionInfo& latest,
                                   const std::string& architecture,
                                   const std::filesystem::path& executable_dir);

}  // namespace crossdesk

#endif