/*
 * @Author: DI JUNKUN
 * @Date: 2025-11-11
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VERSION_CHECKER_H_
#define _VERSION_CHECKER_H_

#include <nlohmann/json_fwd.hpp>
#include <app_version.h>

namespace crossdesk {

std::optional<VersionInfo> ParseVersionInfo(const nlohmann::json& json);
std::optional<VersionInfo> CheckUpdate();

}  // namespace crossdesk

#endif
